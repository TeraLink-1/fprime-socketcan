// ======================================================================
// \title  TestCanSyscalls.hpp
// \author zach
// \brief  Scriptable fake of the CanSyscalls seam for unit tests
//
// TestCanSyscalls.cpp defines every function declared in CanSyscalls.hpp.
// Because the unit test compiles those definitions itself, the linker uses
// them instead of CanSyscallsLinux.cpp from the component library.
//
// Tests set up and inspect the fake through state(). Call reset() before
// each test.
// ======================================================================

#ifndef SocketCan_TestCanSyscalls_HPP
#define SocketCan_TestCanSyscalls_HPP

#include <linux/can.h>

#include <deque>
#include <map>
#include <string>
#include <vector>

#include <Fw/FPrimeBasicTypes.hpp>

namespace SocketCan {
namespace TestCanSyscalls {

//! One message returned by recvmsg()
struct RxMessage {
    can_frame frame;           //!< Frame copied into the receive buffer
    FwSignedSizeType result;   //!< recvmsg() result: bytes received, or -errno
    bool truncated;            //!< Set MSG_TRUNC in msg_flags
    bool hasDropCount;         //!< Attach an SO_RXQ_OVFL control message
    U32 dropCount;             //!< SO_RXQ_OVFL value
};

struct State {
    // socket() and open()
    int socketResult;            //!< Returned by socket(): an fd, or -errno
    int interfaceIndex;          //!< Returned by SIOCGIFINDEX
    int interfaceIndexError;     //!< If nonzero, SIOCGIFINDEX fails with this errno
    short interfaceFlags;        //!< Returned by SIOCGIFFLAGS
    int setsockoptFailName;      //!< Option name whose setsockopt() fails, or -1
    int setsockoptError;         //!< errno for that failure
    int bindError;               //!< If nonzero, bind() fails with this errno

    // What the driver asked for
    std::string ioctlInterfaceName;  //!< ifr_name passed to ioctl()
    can_err_mask_t errorFilter;      //!< CAN_RAW_ERR_FILTER value
    bool errorFilterSet;
    bool dropCounterEnabled;         //!< SO_RXQ_OVFL was enabled
    int receiveBufferBytes;          //!< SO_RCVBUF value, or 0 if not set
    int boundInterfaceIndex;         //!< can_ifindex passed to bind(), or -1
    std::vector<int> closedFds;      //!< Every fd passed to close()

    // poll(): scripted results are used first. With none left, poll()
    // reports POLLIN when a message is queued, and otherwise sleeps for the
    // timeout and returns 0, as the real call would.
    std::deque<int> pollResults;  //!< Results to return: 0, 1, or -errno
    short pollRevents;            //!< revents to report with a scripted result of 1

    // recvmsg()
    std::deque<RxMessage> rxMessages;

    // send(): scripted results are used first; with none left, send() succeeds
    std::deque<FwSignedSizeType> sendResults;  //!< Results to return, or -errno
    std::vector<can_frame> sentFrames;         //!< Every frame passed to send()
    int lastSendFlags;

    // readInterfaceStatistic(): statistics missing from the map fail
    std::map<std::string, U64> statistics;
};

State& state();

//! Restore the defaults: socket() returns fd 3, the interface exists and is
//! up, and every call succeeds
void reset();

//! Queue a received frame with the given can_id (including any flags)
void queueFrame(canid_t canId, U8 len, const U8* data = nullptr);

void queueErrorFrame(canid_t errorClass, const U8 (&data)[CAN_MAX_DLEN]);

//! Queue a received message exactly as given
void queueMessage(const RxMessage& message);

}  // namespace TestCanSyscalls
}  // namespace SocketCan

#endif
