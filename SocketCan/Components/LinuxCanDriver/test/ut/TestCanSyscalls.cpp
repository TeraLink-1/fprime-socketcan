// ======================================================================
// \title  TestCanSyscalls.cpp
// \author zach
// \brief  Scriptable fake of the CanSyscalls seam for unit tests
//
// Defines every function in CanSyscalls.hpp. Leaving one out would pull
// CanSyscallsLinux.cpp in from the component library and fail the link
// with duplicate symbols.
// ======================================================================

#include "TestCanSyscalls.hpp"

#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

#include "SocketCan/Components/LinuxCanDriver/CanSyscalls.hpp"

namespace SocketCan {

// ----------------------------------------------------------------------
// Fake control
// ----------------------------------------------------------------------

namespace TestCanSyscalls {

State& state() {
    static State fakeState;
    return fakeState;
}

void reset() {
    State& s = state();
    s.socketResult = 3;
    s.interfaceIndex = 7;
    s.interfaceIndexError = 0;
    s.interfaceFlags = IFF_UP | IFF_RUNNING;
    s.setsockoptFailName = -1;
    s.setsockoptError = 0;
    s.bindError = 0;
    s.ioctlInterfaceName.clear();
    s.errorFilter = 0;
    s.errorFilterSet = false;
    s.dropCounterEnabled = false;
    s.receiveBufferBytes = 0;
    s.boundInterfaceIndex = -1;
    s.closedFds.clear();
    s.pollResults.clear();
    s.pollRevents = POLLIN;
    s.rxMessages.clear();
    s.sendResults.clear();
    s.sentFrames.clear();
    s.lastSendFlags = 0;
    s.statistics.clear();
}

void queueFrame(const canid_t canId, const U8 len, const U8* const data) {
    RxMessage message{};
    message.frame.can_id = canId;
    message.frame.can_dlc = len;
    if (data != nullptr) {
        const U8 copyLength = (len > CAN_MAX_DLEN) ? static_cast<U8>(CAN_MAX_DLEN) : len;
        std::copy(data, data + copyLength, message.frame.data);
    }
    message.result = CAN_MTU;
    queueMessage(message);
}

void queueErrorFrame(const canid_t errorClass, const U8 (&data)[CAN_MAX_DLEN]) {
    queueFrame(CAN_ERR_FLAG | errorClass, CAN_ERR_DLC, data);
}

void queueMessage(const RxMessage& message) {
    state().rxMessages.push_back(message);
}

}  // namespace TestCanSyscalls

// ----------------------------------------------------------------------
// Seam implementation
// ----------------------------------------------------------------------

namespace CanSyscalls {

using TestCanSyscalls::State;
using TestCanSyscalls::state;

int socket(const int domain, const int type, const int protocol) {
    return state().socketResult;
}

int bind(const int fd, const sockaddr* const addr, const U32 addrLen) {
    if (state().bindError != 0) {
        return -state().bindError;
    }
    if ((addr != nullptr) && (addrLen >= sizeof(sockaddr_can))) {
        // The driver passes a sockaddr_can as the generic sockaddr bind() takes
        state().boundInterfaceIndex = reinterpret_cast<const sockaddr_can*>(addr)->can_ifindex;
    }
    return 0;
}

int setsockopt(const int fd, const int level, const int name, const void* const value, const U32 valueLen) {
    State& s = state();
    if (name == s.setsockoptFailName) {
        return -s.setsockoptError;
    }
    // The driver passes each option as a pointer to a variable of the
    // option's type
    if ((level == SOL_CAN_RAW) && (name == CAN_RAW_ERR_FILTER) && (valueLen == sizeof(can_err_mask_t))) {
        s.errorFilter = *static_cast<const can_err_mask_t*>(value);
        s.errorFilterSet = true;
    } else if ((level == SOL_SOCKET) && (name == SO_RXQ_OVFL) && (valueLen == sizeof(int))) {
        s.dropCounterEnabled = (*static_cast<const int*>(value) != 0);
    } else if ((level == SOL_SOCKET) && (name == SO_RCVBUF) && (valueLen == sizeof(int))) {
        s.receiveBufferBytes = *static_cast<const int*>(value);
    }
    return 0;
}

int ioctl(const int fd, const unsigned long request, void* const arg) {
    State& s = state();
    ifreq* const interfaceRequest = static_cast<ifreq*>(arg);
    s.ioctlInterfaceName = interfaceRequest->ifr_name;
    if (request == SIOCGIFINDEX) {
        if (s.interfaceIndexError != 0) {
            return -s.interfaceIndexError;
        }
        interfaceRequest->ifr_ifindex = s.interfaceIndex;
        return 0;
    }
    if (request == SIOCGIFFLAGS) {
        interfaceRequest->ifr_flags = s.interfaceFlags;
        return 0;
    }
    return -EINVAL;
}

int poll(pollfd* const fds, const U32 count, const int timeoutMs) {
    State& s = state();
    if (!s.pollResults.empty()) {
        const int result = s.pollResults.front();
        s.pollResults.pop_front();
        fds[0].revents = (result > 0) ? s.pollRevents : 0;
        return result;
    }
    if (!s.rxMessages.empty()) {
        fds[0].revents = POLLIN;
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
    fds[0].revents = 0;
    return 0;
}

FwSignedSizeType recvmsg(const int fd, msghdr* const msg, const int flags) {
    State& s = state();
    if (s.rxMessages.empty()) {
        return -EAGAIN;
    }
    const TestCanSyscalls::RxMessage message = s.rxMessages.front();
    s.rxMessages.pop_front();

    msg->msg_flags = message.truncated ? MSG_TRUNC : 0;
    if (message.result < 0) {
        msg->msg_controllen = 0;
        return message.result;
    }

    // Copy only as many bytes as the scripted result, so a short read
    // delivers a partial frame as the kernel would
    const FwSizeType copyLength = (static_cast<FwSizeType>(message.result) < msg->msg_iov[0].iov_len)
                                      ? static_cast<FwSizeType>(message.result)
                                      : msg->msg_iov[0].iov_len;
    std::memcpy(msg->msg_iov[0].iov_base, &message.frame,
                (copyLength < sizeof(message.frame)) ? copyLength : sizeof(message.frame));

    if (message.hasDropCount && (msg->msg_controllen >= CMSG_SPACE(sizeof(U32)))) {
        cmsghdr* const header = CMSG_FIRSTHDR(msg);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SO_RXQ_OVFL;
        header->cmsg_len = CMSG_LEN(sizeof(U32));
        std::memcpy(CMSG_DATA(header), &message.dropCount, sizeof(message.dropCount));
        msg->msg_controllen = CMSG_SPACE(sizeof(U32));
    } else {
        msg->msg_controllen = 0;
    }
    return message.result;
}

FwSignedSizeType send(const int fd, const void* const buffer, const FwSizeType length, const int flags) {
    State& s = state();
    s.lastSendFlags = flags;
    if (length == sizeof(can_frame)) {
        // The driver sends a pointer to a can_frame
        s.sentFrames.push_back(*static_cast<const can_frame*>(buffer));
    }
    if (!s.sendResults.empty()) {
        const FwSignedSizeType result = s.sendResults.front();
        s.sendResults.pop_front();
        return result;
    }
    return static_cast<FwSignedSizeType>(length);
}

int close(const int fd) {
    state().closedFds.push_back(fd);
    return 0;
}

bool readInterfaceStatistic(const char* const interfaceName, const char* const statistic, U64& value) {
    const auto entry = state().statistics.find(statistic);
    if (entry == state().statistics.end()) {
        return false;
    }
    value = entry->second;
    return true;
}

}  // namespace CanSyscalls
}  // namespace SocketCan
