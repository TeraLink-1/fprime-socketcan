// ======================================================================
// \title  CanSyscallsLinux.cpp
// \author zach
// \brief  Linux implementation of the CanSyscalls seam
//
// Each function forwards to the system call of the same name and turns a
// failure into -errno. This is the only file that reads errno.
//
// Keep this file to these definitions only: unit tests replace all of them
// at link time, which works only while nothing else in this object file is
// needed.
// ======================================================================

#include "SocketCan/Components/LinuxCanDriver/CanSyscalls.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace SocketCan {
namespace CanSyscalls {

namespace {

//! Room for "/sys/class/net/" (15) + a 15-character interface name +
//! "/statistics/" (12) + the longest statistic read, "rx_over_errors" (14),
//! + the terminator = 57 bytes, with margin for longer statistic names
constexpr FwSizeType STATISTIC_PATH_SIZE = 96;

//! Room for a U64 in decimal (20 digits) + a newline + the terminator =
//! 22 bytes, with margin
constexpr FwSizeType STATISTIC_TEXT_SIZE = 32;

template <typename T>
T resultOrError(const T result) {
    return (result < 0) ? static_cast<T>(-errno) : result;
}

}  // namespace

int socket(const int domain, const int type, const int protocol) {
    return resultOrError(::socket(domain, type, protocol));
}

int bind(const int fd, const sockaddr* const addr, const U32 addrLen) {
    return resultOrError(::bind(fd, addr, static_cast<socklen_t>(addrLen)));
}

int setsockopt(const int fd, const int level, const int name, const void* const value, const U32 valueLen) {
    return resultOrError(::setsockopt(fd, level, name, value, static_cast<socklen_t>(valueLen)));
}

int ioctl(const int fd, const unsigned long request, void* const arg) {
    return resultOrError(::ioctl(fd, request, arg));
}

int poll(pollfd* const fds, const U32 count, const int timeoutMs) {
    return resultOrError(::poll(fds, static_cast<nfds_t>(count), timeoutMs));
}

FwSignedSizeType recvmsg(const int fd, msghdr* const msg, const int flags) {
    return resultOrError(static_cast<FwSignedSizeType>(::recvmsg(fd, msg, flags)));
}

FwSignedSizeType send(const int fd, const void* const buffer, const FwSizeType length, const int flags) {
    return resultOrError(static_cast<FwSignedSizeType>(::send(fd, buffer, static_cast<size_t>(length), flags)));
}

int close(const int fd) {
    return resultOrError(::close(fd));
}

bool readInterfaceStatistic(const char* const interfaceName, const char* const statistic, U64& value) {
    char path[STATISTIC_PATH_SIZE] = {};
    const int pathLength =
        std::snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s", interfaceName, statistic);
    if ((pathLength < 0) || (static_cast<FwSizeType>(pathLength) >= sizeof(path))) {
        return false;
    }

    // Plain open/read rather than stdio, which allocates a buffer per file
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    char text[STATISTIC_TEXT_SIZE] = {};
    const ssize_t length = ::read(fd, text, sizeof(text) - 1);
    // The read is already done, so a failed close loses nothing
    (void)::close(fd);
    if (length <= 0) {
        return false;
    }
    text[length] = '\0';

    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if ((errno != 0) || (end == text)) {
        return false;
    }
    value = static_cast<U64>(parsed);
    return true;
}

}  // namespace CanSyscalls
}  // namespace SocketCan
