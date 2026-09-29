// ======================================================================
// \title  CanSyscallsStub.cpp
// \author zach
// \brief  Stub implementation of the CanSyscalls seam for non-Linux hosts
//
// Built when FPRIME_USE_STUBBED_DRIVERS is on.
// ======================================================================

#include "SocketCan/Components/LinuxCanDriver/CanSyscalls.hpp"

#include <cerrno>

namespace SocketCan {
namespace CanSyscalls {

int socket(const int domain, const int type, const int protocol) {
    return -ENOSYS;
}

int bind(const int fd, const sockaddr* const addr, const U32 addrLen) {
    return -ENOSYS;
}

int setsockopt(const int fd, const int level, const int name, const void* const value, const U32 valueLen) {
    return -ENOSYS;
}

int ioctl(const int fd, const unsigned long request, void* const arg) {
    return -ENOSYS;
}

int poll(pollfd* const fds, const U32 count, const int timeoutMs) {
    return -ENOSYS;
}

FwSignedSizeType recvmsg(const int fd, msghdr* const msg, const int flags) {
    return -ENOSYS;
}

FwSignedSizeType send(const int fd, const void* const buffer, const FwSizeType length, const int flags) {
    return -ENOSYS;
}

int close(const int fd) {
    return -ENOSYS;
}

bool readInterfaceStatistic(const char* const interfaceName, const char* const statistic, U64& value) {
    return false;
}

}  // namespace CanSyscalls
}  // namespace SocketCan
