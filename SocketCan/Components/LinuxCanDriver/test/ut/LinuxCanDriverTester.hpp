// ======================================================================
// \title  LinuxCanDriverTester.hpp
// \author zach
// \brief  hpp file for LinuxCanDriver component test harness implementation class
// ======================================================================

#ifndef SocketCan_LinuxCanDriverTester_HPP
#define SocketCan_LinuxCanDriverTester_HPP

#include "SocketCan/Components/LinuxCanDriver/LinuxCanDriver.hpp"
#include "SocketCan/Components/LinuxCanDriver/LinuxCanDriverGTestBase.hpp"

namespace SocketCan {

class LinuxCanDriverTester final : public LinuxCanDriverGTestBase {
  public:
    // ----------------------------------------------------------------------
    // Constants
    // ----------------------------------------------------------------------

    static const FwSizeType MAX_HISTORY_SIZE = 100;

    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

  public:
    // ----------------------------------------------------------------------
    // Construction and destruction
    // ----------------------------------------------------------------------

    LinuxCanDriverTester();

    ~LinuxCanDriverTester();

  public:
    // ----------------------------------------------------------------------
    // Tests
    // ----------------------------------------------------------------------

    //! open() binds the socket and sets the socket options
    void testOpen();

    //! open() sets SO_RCVBUF when a size is given
    void testOpenReceiveBuffer();

    //! open() reports each failure and closes the socket
    void testOpenFailures();

    //! canSend before open() returns NOT_OPEN
    void testSendBeforeOpen();

    //! Sent frames convert correctly: 11- and 29-bit IDs, RTR, lengths 0-8
    void testSendConversion();

    //! Invalid frames are rejected without being sent
    void testSendValidation();

    //! A full TX queue returns TX_BUSY; other errors return ERROR
    void testSendErrors();

    //! SendError is throttled and re-armed by a successful send
    void testSendErrorThrottle();

    //! Received frames convert correctly: 11- and 29-bit IDs, RTR, lengths 0-8
    void testReceiveConversion();

    //! Short and oversized reads are counted and not delivered
    void testReceiveBadLength();

    //! Failed polls and reads are reported; timeouts and EINTR are not
    void testReadErrors();

    //! Each error-frame class updates the right state, event, and counter
    void testErrorFrames();

    //! Bus-off followed by restart
    void testBusOffAndRestart();

    //! Returning to error-active re-arms the throttled bus events
    void testThrottleRearmOnErrorActive();

    //! A rising SO_RXQ_OVFL count emits SocketRxDrops with the new drops
    void testSocketDrops();

    //! run publishes the counters and the interface statistics
    void testRunTelemetry();

    //! The read task exits within the read timeout after quitReadThread()
    void testReadTaskQuit();

  private:
    // ----------------------------------------------------------------------
    // Helper functions
    // ----------------------------------------------------------------------

    void connectPorts();

    void initComponents();

    //! Open the component on "can0" and clear the history
    void openInterface();

    //! Run one pass of the read loop and check its result
    void readOnce(LinuxCanDriver::ReadResult expected);

    //! Build a CanFrame whose data bytes are base, base + 1, ... up to len
    static CanFrame makeFrame(U32 id, U8 flags, U8 len, U8 base);

  private:
    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    LinuxCanDriver component;
};

}  // namespace SocketCan

#endif
