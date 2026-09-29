// ======================================================================
// \title  CanSyscalls.hpp
// \author zach
// \brief  Seam between LinuxCanDriver and the operating system
//
// Thin wrappers around the socket and sysfs calls the driver makes,
// implemented in CanSyscallsLinux.cpp. In the stub build
// (CanSyscallsStub.cpp) every call fails with ENOSYS.
//
// Errors are returned, not left in errno: on success each call returns what
// the system call it wraps returns, and on failure the negated errno value
// (e.g. -ENOBUFS). CanSyscallsLinux.cpp is the only place errno is read.
//
// Unit tests replace these at link time: a test that compiles its own
// definitions of every function below links those instead of the ones in
// the component library. For that to work, CanSyscallsLinux.cpp must
// contain only these definitions.
//
// This header must stay free of Linux headers so that it builds on every
// platform. Linux types appear only as forward-declared pointers.
// ======================================================================

#ifndef SocketCan_CanSyscalls_HPP
#define SocketCan_CanSyscalls_HPP

#include <Fw/FPrimeBasicTypes.hpp>

struct sockaddr;
struct msghdr;
struct pollfd;

namespace SocketCan {
namespace CanSyscalls {

int socket(int domain, int type, int protocol);

int bind(int fd, const sockaddr* addr, U32 addrLen);

int setsockopt(int fd, int level, int name, const void* value, U32 valueLen);

int ioctl(int fd, unsigned long request, void* arg);

int poll(pollfd* fds, U32 count, int timeoutMs);

FwSignedSizeType recvmsg(int fd, msghdr* msg, int flags);

FwSignedSizeType send(int fd, const void* buffer, FwSizeType length, int flags);

int close(int fd);

//! Read one counter from /sys/class/net/<interfaceName>/statistics/<statistic>
//! \return true if the counter was read and parsed
bool readInterfaceStatistic(const char* interfaceName, const char* statistic, U64& value);

}  // namespace CanSyscalls
}  // namespace SocketCan

#endif
