// ======================================================================
// \title  LinuxCanDriverLinux.cpp
// \author zach
// \brief  Linux (SocketCAN) implementation of LinuxCanDriver
//
// Built only on Linux, when FPRIME_USE_STUBBED_DRIVERS is off. Every
// member defined here that the common code or topology can call needs a
// matching definition in LinuxCanDriverStub.cpp.
//
// System calls go through CanSyscalls, which returns -errno on failure;
// this file never reads errno itself.
// ======================================================================

#include "SocketCan/Components/LinuxCanDriver/LinuxCanDriver.hpp"

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <Fw/Types/Assert.hpp>
#include <Fw/Types/StringUtils.hpp>
#include <Os/TaskString.hpp>

#include "SocketCan/Components/LinuxCanDriver/CanSyscalls.hpp"
#include "SocketCan/Ports/FppConstantsAc.hpp"

namespace SocketCan {

static_assert(CAN_INTERFACE_NAME_MAX_LENGTH + 1 == IFNAMSIZ, "Interface name length must match IFNAMSIZ");
static_assert(CAN_FRAME_MAX_LEN == CAN_MAX_DLEN, "CanFrame data must match classic CAN");
static_assert(static_cast<canid_t>(CAN_FRAME_MAX_STD_ID) == CAN_SFF_MASK,
              "Standard identifier range must match SocketCAN");
static_assert(static_cast<canid_t>(CAN_FRAME_MAX_EXT_ID) == CAN_EFF_MASK,
              "Extended identifier range must match SocketCAN");

namespace {

//! Error classes delivered to the read task as error frames
constexpr can_err_mask_t ERROR_FILTER =
    CAN_ERR_CRTL | CAN_ERR_PROT | CAN_ERR_ACK | CAN_ERR_BUSOFF | CAN_ERR_BUSERROR | CAN_ERR_RESTARTED;

//! How long the read task waits after a failed read, as LinuxUartDriver does
const Fw::TimeInterval READ_BACKOFF(0, 50000);

//! Convert a validated CanFrame to the kernel's frame layout
can_frame toKernelFrame(const CanFrame& in) {
    can_frame out{};
    const U8 flags = in.get_flags();
    const canid_t idFlags = (((flags & CAN_FRAME_FLAG_IDE) != 0) ? CAN_EFF_FLAG : 0U) |
                            (((flags & CAN_FRAME_FLAG_RTR) != 0) ? CAN_RTR_FLAG : 0U);
    out.can_id = in.get_id() | idFlags;
    // can_dlc rather than len: len only exists in kernel headers from 5.11
    out.can_dlc = in.get_len();
    if ((flags & CAN_FRAME_FLAG_RTR) == 0) {
        const U8* const source = in.get_data();
        std::copy(source, source + in.get_len(), out.data);
    }
    return out;
}

CanFrame fromKernelFrame(const can_frame& in) {
    const bool extended = (in.can_id & CAN_EFF_FLAG) != 0;
    const bool remote = (in.can_id & CAN_RTR_FLAG) != 0;
    const U32 id = in.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    const U8 flags = static_cast<U8>((extended ? CAN_FRAME_FLAG_IDE : 0) | (remote ? CAN_FRAME_FLAG_RTR : 0));
    // can_dlc should never exceed 8 for classic CAN, but it is set by the
    // controller driver, so clamp it before using it as a copy length
    const U8 len = (in.can_dlc > CAN_MAX_DLEN) ? static_cast<U8>(CAN_MAX_DLEN) : in.can_dlc;
    U8 data[CAN_FRAME_MAX_LEN] = {};
    if (!remote) {
        std::copy(in.data, in.data + len, data);
    }
    return CanFrame(id, flags, len, data);
}

}  // namespace

// ----------------------------------------------------------------------
// Setup and teardown
// ----------------------------------------------------------------------

bool LinuxCanDriver ::open(const Fw::String& interfaceName, const U32 receiveBufferBytes) {
    FW_ASSERT(this->m_fd < 0, static_cast<FwAssertArgType>(this->m_fd));

    // A name too long for Linux is truncated here, but open() rejects it
    // below, so the truncated form is only ever shown in the event
    this->m_interfaceName = interfaceName;
    if (interfaceName.length() > CAN_INTERFACE_NAME_MAX_LENGTH) {
        return this->openFailed(-1, ENAMETOOLONG);
    }

    const int fd = CanSyscalls::socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC, CAN_RAW);
    if (fd < 0) {
        return this->openFailed(-1, -fd);
    }

    // Look up the interface and check that the system configuration has
    // brought it up
    ifreq request{};
    // The name fits, since its length was checked above, and the returned
    // pointer is just ifr_name
    (void)Fw::StringUtils::string_copy(request.ifr_name, this->m_interfaceName.toChar(), sizeof(request.ifr_name));
    const int indexResult = CanSyscalls::ioctl(fd, SIOCGIFINDEX, &request);
    if (indexResult < 0) {
        return this->openFailed(fd, -indexResult);
    }
    const int interfaceIndex = request.ifr_ifindex;
    const int flagsResult = CanSyscalls::ioctl(fd, SIOCGIFFLAGS, &request);
    if (flagsResult < 0) {
        return this->openFailed(fd, -flagsResult);
    }
    if ((request.ifr_flags & IFF_UP) == 0) {
        // The socket is abandoned either way; a failed close changes nothing
        (void)CanSyscalls::close(fd);
        this->log_WARNING_HI_InterfaceDown(this->m_interfaceName);
        return false;
    }

    const can_err_mask_t errorMask = ERROR_FILTER;
    const int filterResult =
        CanSyscalls::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &errorMask, sizeof(errorMask));
    if (filterResult < 0) {
        return this->openFailed(fd, -filterResult);
    }
    const int enable = 1;
    const int overflowResult = CanSyscalls::setsockopt(fd, SOL_SOCKET, SO_RXQ_OVFL, &enable, sizeof(enable));
    if (overflowResult < 0) {
        return this->openFailed(fd, -overflowResult);
    }
    if (receiveBufferBytes != 0) {
        const int bufferBytes = static_cast<int>(receiveBufferBytes);
        const int bufferResult =
            CanSyscalls::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufferBytes, sizeof(bufferBytes));
        if (bufferResult < 0) {
            return this->openFailed(fd, -bufferResult);
        }
    }
    // CAN_RAW_FILTER stays at its default (accept everything),
    // CAN_RAW_LOOPBACK at its default (on), and CAN_RAW_RECV_OWN_MSGS at its
    // default (off)

    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = interfaceIndex;
    // bind() takes the generic sockaddr; sockaddr_can is the CAN family's
    // form of it, identified by can_family
    const int bindResult = CanSyscalls::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (bindResult < 0) {
        return this->openFailed(fd, -bindResult);
    }

    this->m_lastDropCount = 0;
    this->m_fd = fd;
    this->log_ACTIVITY_HI_InterfaceOpened(this->m_interfaceName);
    return true;
}

bool LinuxCanDriver ::openFailed(const int fd, const I32 error) const {
    if (fd >= 0) {
        // The socket is abandoned either way; a failed close changes nothing
        (void)CanSyscalls::close(fd);
    }
    this->log_WARNING_HI_InterfaceOpenFailed(this->m_interfaceName, error);
    return false;
}

void LinuxCanDriver ::start(const FwTaskPriorityType priority,
                            const Os::Task::ParamType stackSize,
                            const Os::Task::ParamType cpuAffinity) {
    const Os::TaskString taskName("CanReader");
    const Os::Task::Arguments arguments(taskName, readTaskEntry, this, priority, stackSize, cpuAffinity);
    const Os::Task::Status status = this->m_readTask.start(arguments);
    FW_ASSERT(status == Os::Task::OP_OK, static_cast<FwAssertArgType>(status));
}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

SocketCan::CanStatus LinuxCanDriver ::canSend_handler(const FwIndexType portNum, const SocketCan::CanFrame& frame) {
    if (this->m_fd < 0) {
        return SocketCan::CanStatus::NOT_OPEN;
    }
    if (!isValidFrame(frame)) {
        return SocketCan::CanStatus::INVALID_FRAME;
    }

    const can_frame kernelFrame = toKernelFrame(frame);
    const FwSignedSizeType sent = CanSyscalls::send(this->m_fd, &kernelFrame, sizeof(kernelFrame), MSG_DONTWAIT);

    if (sent == static_cast<FwSignedSizeType>(sizeof(kernelFrame))) {
        this->m_framesTx++;
        if (this->m_sendErrorLatched) {
            this->m_sendErrorLatched = false;
            this->log_WARNING_HI_SendError_ThrottleClear();
        }
        return SocketCan::CanStatus::OK;
    }
    if ((sent == -ENOBUFS) || (sent == -EAGAIN) || (sent == -EWOULDBLOCK)) {
        // The interface TX queue is full; the caller decides what to do
        this->m_txBusy++;
        return SocketCan::CanStatus::TX_BUSY;
    }
    // Any other failure. A CAN frame is never partly sent, so a short write
    // has no errno of its own and is reported as EIO.
    this->m_txErrors++;
    this->m_sendErrorLatched = true;
    this->log_WARNING_HI_SendError((sent < 0) ? static_cast<I32>(-sent) : EIO);
    return SocketCan::CanStatus::ERROR;
}

// ----------------------------------------------------------------------
// Read task
// ----------------------------------------------------------------------

void LinuxCanDriver ::readTaskEntry(void* const ptr) {
    FW_ASSERT(ptr != nullptr);
    LinuxCanDriver* const component = static_cast<LinuxCanDriver*>(ptr);
    while (!component->m_quitReadThread) {
        if (component->readOnce() == ReadResult::FAILED) {
            // If the delay fails, the loop only retries sooner
            (void)Os::Task::delay(READ_BACKOFF);
        }
    }
}

LinuxCanDriver::ReadResult LinuxCanDriver ::readOnce() {
    // Wait with a timeout so the quit flag is checked regularly. A closed
    // socket (fd -1) is ignored by poll(), which then just times out.
    pollfd waitFor{};
    waitFor.fd = this->m_fd;
    waitFor.events = POLLIN;
    const int ready = CanSyscalls::poll(&waitFor, 1, READ_TIMEOUT_MS);
    if (ready == 0) {
        return ReadResult::IDLE;
    }
    if (ready < 0) {
        if (ready == -EINTR) {
            return ReadResult::IDLE;
        }
        this->reportReadError(-ready);
        return ReadResult::FAILED;
    }
    if ((waitFor.revents & POLLIN) == 0) {
        // POLLERR, POLLHUP, or POLLNVAL without data
        this->reportReadError(EIO);
        return ReadResult::FAILED;
    }

    can_frame kernelFrame{};
    iovec vector{};
    vector.iov_base = &kernelFrame;
    vector.iov_len = sizeof(kernelFrame);
    // Room for the SO_RXQ_OVFL drop counter
    alignas(cmsghdr) U8 control[CMSG_SPACE(sizeof(U32))] = {};
    msghdr message{};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);

    const FwSignedSizeType received = CanSyscalls::recvmsg(this->m_fd, &message, MSG_DONTWAIT);
    if (received < 0) {
        if ((received == -EAGAIN) || (received == -EWOULDBLOCK) || (received == -EINTR)) {
            return ReadResult::IDLE;
        }
        this->reportReadError(static_cast<I32>(-received));
        return ReadResult::FAILED;
    }

    // The control buffer holds at most one message, so this loop is bounded
    for (cmsghdr* header = CMSG_FIRSTHDR(&message); header != nullptr;
         header = CMSG_NXTHDR(&message, header)) {
        if ((header->cmsg_level == SOL_SOCKET) && (header->cmsg_type == SO_RXQ_OVFL) &&
            (header->cmsg_len >= CMSG_LEN(sizeof(U32)))) {
            U32 dropCount = 0;
            std::memcpy(&dropCount, CMSG_DATA(header), sizeof(dropCount));
            this->updateSocketDrops(dropCount);
        }
    }

    if ((received != static_cast<FwSignedSizeType>(CAN_MTU)) || ((message.msg_flags & MSG_TRUNC) != 0)) {
        this->reportReadError(EMSGSIZE);
        return ReadResult::HANDLED;
    }
    this->clearReadError();

    if ((kernelFrame.can_id & CAN_ERR_FLAG) != 0) {
        this->handleErrorFrame(kernelFrame);
    } else {
        this->handleDataFrame(kernelFrame);
    }
    return ReadResult::HANDLED;
}

void LinuxCanDriver ::handleDataFrame(const can_frame& kernelFrame) {
    const Fw::Time rxTime = this->getTime();
    this->m_framesRx++;
    if (this->isConnected_canReceive_OutputPort(0)) {
        this->canReceive_out(0, fromKernelFrame(kernelFrame), rxTime);
    }
}

void LinuxCanDriver ::handleErrorFrame(const can_frame& kernelFrame) {
    this->m_errorFrames++;
    const canid_t errorClass = kernelFrame.can_id & CAN_ERR_MASK;
    CanBusState::T state = this->m_busState.load();

    if ((errorClass & CAN_ERR_CRTL) != 0) {
        const U8 controller = kernelFrame.data[1];
        if ((controller & CAN_ERR_CRTL_RX_OVERFLOW) != 0) {
            this->log_WARNING_HI_ControllerOverflow(CanOverflowDirection::RX);
        }
        if ((controller & CAN_ERR_CRTL_TX_OVERFLOW) != 0) {
            this->log_WARNING_HI_ControllerOverflow(CanOverflowDirection::TX);
        }
        // Passive outranks warning when both are set. An overflow alone
        // leaves the state unchanged.
        if ((controller & (CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE)) != 0) {
            state = CanBusState::ERROR_PASSIVE;
        } else if ((controller & (CAN_ERR_CRTL_RX_WARNING | CAN_ERR_CRTL_TX_WARNING)) != 0) {
            state = CanBusState::ERROR_WARNING;
        }
#ifdef CAN_ERR_CRTL_ACTIVE
        // Newer kernels also report the return to error-active here
        else if ((controller & CAN_ERR_CRTL_ACTIVE) != 0) {
            state = CanBusState::ERROR_ACTIVE;
        }
#endif
    }
    if ((errorClass & CAN_ERR_PROT) != 0) {
        this->m_protocolErrors++;
    }
    if ((errorClass & CAN_ERR_ACK) != 0) {
        this->log_WARNING_LO_NoAck();
    }
    if ((errorClass & CAN_ERR_BUSOFF) != 0) {
        state = CanBusState::BUS_OFF;
        this->log_WARNING_HI_BusOff();
    }
    if ((errorClass & CAN_ERR_RESTARTED) != 0) {
        state = CanBusState::ERROR_ACTIVE;
        this->log_ACTIVITY_HI_BusRestarted();
    }
#ifdef CAN_ERR_CNT
    // Newer kernels carry TEC and REC in error frames flagged CAN_ERR_CNT
    if ((errorClass & CAN_ERR_CNT) != 0) {
        this->m_tec = kernelFrame.data[6];
        this->m_rec = kernelFrame.data[7];
        this->m_errorCountersValid = true;
    }
#endif
    this->setBusState(state);
}

}  // namespace SocketCan
