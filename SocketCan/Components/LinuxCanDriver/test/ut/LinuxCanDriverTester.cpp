// ======================================================================
// \title  LinuxCanDriverTester.cpp
// \author zach
// \brief  cpp file for LinuxCanDriver component test harness implementation class
// ======================================================================

#include "LinuxCanDriverTester.hpp"

#include <linux/can.h>
#include <linux/can/error.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

#include "TestCanSyscalls.hpp"
#include "SocketCan/Ports/FppConstantsAc.hpp"

namespace SocketCan {

namespace {

TestCanSyscalls::State& fake() {
    return TestCanSyscalls::state();
}

TestCanSyscalls::RxMessage frameWithDropCount(const U32 dropCount) {
    TestCanSyscalls::RxMessage message{};
    message.frame.can_id = 0x123;
    message.frame.can_dlc = 1;
    message.result = CAN_MTU;
    message.hasDropCount = true;
    message.dropCount = dropCount;
    return message;
}

}  // namespace

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

LinuxCanDriverTester ::LinuxCanDriverTester()
    : LinuxCanDriverGTestBase("LinuxCanDriverTester", LinuxCanDriverTester::MAX_HISTORY_SIZE),
      component("LinuxCanDriver") {
    TestCanSyscalls::reset();
    this->initComponents();
    this->connectPorts();
}

LinuxCanDriverTester ::~LinuxCanDriverTester() {
    this->component.deinit();
}

// ----------------------------------------------------------------------
// Tests: open
// ----------------------------------------------------------------------

void LinuxCanDriverTester ::testOpen() {
    ASSERT_TRUE(this->component.open("can0"));

    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_InterfaceOpened_SIZE(1);
    ASSERT_EVENTS_InterfaceOpened(0, "can0");

    ASSERT_EQ(fake().ioctlInterfaceName, "can0");
    ASSERT_EQ(fake().boundInterfaceIndex, 7);
    ASSERT_TRUE(fake().errorFilterSet);
    ASSERT_EQ(fake().errorFilter, static_cast<can_err_mask_t>(CAN_ERR_CRTL | CAN_ERR_PROT | CAN_ERR_ACK |
                                                              CAN_ERR_BUSOFF | CAN_ERR_BUSERROR |
                                                              CAN_ERR_RESTARTED));
    ASSERT_TRUE(fake().dropCounterEnabled);
    ASSERT_EQ(fake().receiveBufferBytes, 0);
    ASSERT_TRUE(fake().closedFds.empty());
}

void LinuxCanDriverTester ::testOpenReceiveBuffer() {
    ASSERT_TRUE(this->component.open("can0", 262144));
    ASSERT_EQ(fake().receiveBufferBytes, 262144);
}

void LinuxCanDriverTester ::testOpenFailures() {
    // Interface missing
    fake().interfaceIndexError = ENODEV;
    ASSERT_FALSE(this->component.open("can0"));
    ASSERT_EVENTS_InterfaceOpenFailed_SIZE(1);
    ASSERT_EVENTS_InterfaceOpenFailed(0, "can0", ENODEV);
    ASSERT_EQ(fake().closedFds.size(), 1U);
    fake().interfaceIndexError = 0;

    // Interface down
    fake().interfaceFlags = 0;
    ASSERT_FALSE(this->component.open("can0"));
    ASSERT_EVENTS_InterfaceDown_SIZE(1);
    ASSERT_EVENTS_InterfaceDown(0, "can0");
    ASSERT_EQ(fake().closedFds.size(), 2U);
    fake().interfaceFlags = IFF_UP;

    fake().setsockoptFailName = SO_RXQ_OVFL;
    fake().setsockoptError = ENOPROTOOPT;
    ASSERT_FALSE(this->component.open("can0"));
    ASSERT_EVENTS_InterfaceOpenFailed(1, "can0", ENOPROTOOPT);
    ASSERT_EQ(fake().closedFds.size(), 3U);
    fake().setsockoptFailName = -1;

    fake().bindError = EADDRNOTAVAIL;
    ASSERT_FALSE(this->component.open("can0"));
    ASSERT_EVENTS_InterfaceOpenFailed(2, "can0", EADDRNOTAVAIL);
    ASSERT_EQ(fake().closedFds.size(), 4U);
    fake().bindError = 0;

    // No socket to close when socket() itself fails
    fake().socketResult = -EAFNOSUPPORT;
    ASSERT_FALSE(this->component.open("can0"));
    ASSERT_EVENTS_InterfaceOpenFailed(3, "can0", EAFNOSUPPORT);
    ASSERT_EQ(fake().closedFds.size(), 4U);
    fake().socketResult = 3;

    ASSERT_FALSE(this->component.open("a_name_far_too_long_for_linux"));
    ASSERT_EVENTS_InterfaceOpenFailed_SIZE(5);
    ASSERT_EQ(this->eventHistory_InterfaceOpenFailed->at(4).error, ENAMETOOLONG);

    ASSERT_EQ(this->invoke_to_canSend(0, makeFrame(0x1, 0, 0, 0)).e, CanStatus::NOT_OPEN);

    ASSERT_TRUE(this->component.open("can0"));
    ASSERT_EVENTS_InterfaceOpened_SIZE(1);
}

// ----------------------------------------------------------------------
// Tests: send
// ----------------------------------------------------------------------

void LinuxCanDriverTester ::testSendBeforeOpen() {
    ASSERT_EQ(this->invoke_to_canSend(0, makeFrame(0x1, 0, 1, 0)).e, CanStatus::NOT_OPEN);
    ASSERT_TRUE(fake().sentFrames.empty());
    ASSERT_EVENTS_SIZE(0);
}

void LinuxCanDriverTester ::testSendConversion() {
    this->openInterface();
    U32 sentCount = 0;
    for (U8 extended = 0; extended < 2; extended++) {
        for (U8 remote = 0; remote < 2; remote++) {
            for (U8 len = 0; len <= CAN_FRAME_MAX_LEN; len++) {
                const U32 id = (extended != 0) ? static_cast<U32>(CAN_FRAME_MAX_EXT_ID) - len
                                               : static_cast<U32>(CAN_FRAME_MAX_STD_ID) - len;
                const U8 flags = static_cast<U8>(((extended != 0) ? CAN_FRAME_FLAG_IDE : 0) |
                                                 ((remote != 0) ? CAN_FRAME_FLAG_RTR : 0));
                ASSERT_EQ(this->invoke_to_canSend(0, makeFrame(id, flags, len, 0x10)).e, CanStatus::OK);
                sentCount++;

                ASSERT_EQ(fake().sentFrames.size(), sentCount);
                const can_frame& sent = fake().sentFrames.back();
                const canid_t expectedId =
                    id | ((extended != 0) ? CAN_EFF_FLAG : 0U) | ((remote != 0) ? CAN_RTR_FLAG : 0U);
                ASSERT_EQ(sent.can_id, expectedId);
                ASSERT_EQ(sent.can_dlc, len);
                for (U8 i = 0; i < CAN_MAX_DLEN; i++) {
                    // RTR frames carry no data, and bytes past len are zero
                    const U8 expected = ((remote == 0) && (i < len)) ? static_cast<U8>(0x10 + i) : 0;
                    ASSERT_EQ(sent.data[i], expected) << "byte " << static_cast<int>(i);
                }
            }
        }
    }
    ASSERT_EQ(fake().lastSendFlags, MSG_DONTWAIT);

    this->invoke_to_run(0, 0);
    ASSERT_TLM_FramesTx(0, sentCount);
    ASSERT_EVENTS_SIZE(0);
}

void LinuxCanDriverTester ::testSendValidation() {
    this->openInterface();

    // Too long
    ASSERT_EQ(this->invoke_to_canSend(0, makeFrame(0x1, 0, 9, 0)).e, CanStatus::INVALID_FRAME);
    ASSERT_EQ(this->invoke_to_canSend(0, makeFrame(CAN_FRAME_MAX_STD_ID + 1, 0, 1, 0)).e,
              CanStatus::INVALID_FRAME);
    ASSERT_EQ(
        this->invoke_to_canSend(0, makeFrame(CAN_FRAME_MAX_EXT_ID + 1U, CAN_FRAME_FLAG_IDE, 1, 0)).e,
        CanStatus::INVALID_FRAME);
    // Undefined flag bit
    ASSERT_EQ(this->invoke_to_canSend(0, makeFrame(0x1, 0x04, 1, 0)).e, CanStatus::INVALID_FRAME);

    ASSERT_EQ(this->invoke_to_canSend(0, makeFrame(CAN_FRAME_MAX_STD_ID, 0, 8, 0)).e, CanStatus::OK);
    ASSERT_EQ(this->invoke_to_canSend(0, makeFrame(CAN_FRAME_MAX_EXT_ID, CAN_FRAME_FLAG_IDE, 8, 0)).e,
              CanStatus::OK);

    ASSERT_EQ(fake().sentFrames.size(), 2U);
    ASSERT_EVENTS_SIZE(0);
}

void LinuxCanDriverTester ::testSendErrors() {
    this->openInterface();
    const CanFrame frame = makeFrame(0x42, 0, 2, 0);

    fake().sendResults.push_back(-ENOBUFS);
    ASSERT_EQ(this->invoke_to_canSend(0, frame).e, CanStatus::TX_BUSY);
    fake().sendResults.push_back(-EAGAIN);
    ASSERT_EQ(this->invoke_to_canSend(0, frame).e, CanStatus::TX_BUSY);
    ASSERT_EVENTS_SIZE(0);

    fake().sendResults.push_back(-ENETDOWN);
    ASSERT_EQ(this->invoke_to_canSend(0, frame).e, CanStatus::ERROR);
    ASSERT_EVENTS_SendError_SIZE(1);
    ASSERT_EVENTS_SendError(0, ENETDOWN);

    // A short write is reported as EIO
    fake().sendResults.push_back(5);
    ASSERT_EQ(this->invoke_to_canSend(0, frame).e, CanStatus::ERROR);
    ASSERT_EVENTS_SendError(1, EIO);

    this->invoke_to_run(0, 0);
    ASSERT_TLM_TxBusy(0, 2U);
    ASSERT_TLM_TxErrors(0, 2U);
    ASSERT_TLM_FramesTx(0, 0U);
}

void LinuxCanDriverTester ::testSendErrorThrottle() {
    this->openInterface();
    const CanFrame frame = makeFrame(0x42, 0, 2, 0);

    for (U32 i = 0; i < 7; i++) {
        fake().sendResults.push_back(-EIO);
        ASSERT_EQ(this->invoke_to_canSend(0, frame).e, CanStatus::ERROR);
    }
    ASSERT_EVENTS_SendError_SIZE(5);

    // A successful send re-arms the event
    ASSERT_EQ(this->invoke_to_canSend(0, frame).e, CanStatus::OK);
    fake().sendResults.push_back(-EIO);
    ASSERT_EQ(this->invoke_to_canSend(0, frame).e, CanStatus::ERROR);
    ASSERT_EVENTS_SendError_SIZE(6);
}

// ----------------------------------------------------------------------
// Tests: receive
// ----------------------------------------------------------------------

void LinuxCanDriverTester ::testReceiveConversion() {
    this->openInterface();
    FwSizeType received = 0;
    for (U8 extended = 0; extended < 2; extended++) {
        for (U8 remote = 0; remote < 2; remote++) {
            for (U8 len = 0; len <= CAN_FRAME_MAX_LEN; len++) {
                const U32 id = (extended != 0) ? static_cast<U32>(CAN_FRAME_MAX_EXT_ID) - len
                                               : static_cast<U32>(CAN_FRAME_MAX_STD_ID) - len;
                const canid_t canId =
                    id | ((extended != 0) ? CAN_EFF_FLAG : 0U) | ((remote != 0) ? CAN_RTR_FLAG : 0U);
                U8 data[CAN_MAX_DLEN] = {};
                for (U8 i = 0; i < len; i++) {
                    data[i] = static_cast<U8>(0x20 + i);
                }
                TestCanSyscalls::queueFrame(canId, len, data);
                this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
                received++;

                ASSERT_from_canReceive_SIZE(received);
                const CanFrame& frame = this->fromPortHistory_canReceive->at(received - 1).frame;
                const U8 expectedFlags = static_cast<U8>(((extended != 0) ? CAN_FRAME_FLAG_IDE : 0) |
                                                         ((remote != 0) ? CAN_FRAME_FLAG_RTR : 0));
                ASSERT_EQ(frame.get_id(), id);
                ASSERT_EQ(frame.get_flags(), expectedFlags);
                ASSERT_EQ(frame.get_len(), len);
                for (U8 i = 0; i < CAN_FRAME_MAX_LEN; i++) {
                    const U8 expected = ((remote == 0) && (i < len)) ? static_cast<U8>(0x20 + i) : 0;
                    ASSERT_EQ(frame.get_data()[i], expected) << "byte " << static_cast<int>(i);
                }
            }
        }
    }

    this->invoke_to_run(0, 0);
    ASSERT_TLM_FramesRx(0, static_cast<U64>(received));
    ASSERT_EVENTS_SIZE(0);
}

void LinuxCanDriverTester ::testReceiveBadLength() {
    this->openInterface();

    // Short read
    TestCanSyscalls::RxMessage message{};
    message.frame.can_id = 0x123;
    message.frame.can_dlc = 8;
    message.result = 8;
    TestCanSyscalls::queueMessage(message);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_from_canReceive_SIZE(0);
    ASSERT_EVENTS_ReadError_SIZE(1);
    ASSERT_EVENTS_ReadError(0, EMSGSIZE);

    // Oversized: the kernel truncates to the buffer and sets MSG_TRUNC
    message.result = CAN_MTU;
    message.truncated = true;
    TestCanSyscalls::queueMessage(message);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_from_canReceive_SIZE(0);
    ASSERT_EVENTS_ReadError(1, EMSGSIZE);

    // A good frame still gets through
    TestCanSyscalls::queueFrame(0x123, 0);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_from_canReceive_SIZE(1);

    this->invoke_to_run(0, 0);
    ASSERT_TLM_ReadErrors(0, 2U);
    ASSERT_TLM_FramesRx(0, 1U);
}

void LinuxCanDriverTester ::testReadErrors() {
    this->openInterface();

    // Timeout
    fake().pollResults.push_back(0);
    this->readOnce(LinuxCanDriver::ReadResult::IDLE);

    fake().pollResults.push_back(-EINTR);
    this->readOnce(LinuxCanDriver::ReadResult::IDLE);
    ASSERT_EVENTS_SIZE(0);

    fake().pollResults.push_back(-EBADF);
    this->readOnce(LinuxCanDriver::ReadResult::FAILED);
    ASSERT_EVENTS_ReadError(0, EBADF);

    // Socket error without data
    fake().pollResults.push_back(1);
    fake().pollRevents = POLLERR;
    this->readOnce(LinuxCanDriver::ReadResult::FAILED);
    ASSERT_EVENTS_ReadError(1, EIO);
    fake().pollRevents = POLLIN;

    // Nothing to read after all
    TestCanSyscalls::RxMessage message{};
    message.result = -EAGAIN;
    TestCanSyscalls::queueMessage(message);
    this->readOnce(LinuxCanDriver::ReadResult::IDLE);

    message.result = -ENETDOWN;
    TestCanSyscalls::queueMessage(message);
    this->readOnce(LinuxCanDriver::ReadResult::FAILED);
    ASSERT_EVENTS_ReadError(2, ENETDOWN);

    ASSERT_EVENTS_SIZE(3);
    ASSERT_from_canReceive_SIZE(0);
    this->invoke_to_run(0, 0);
    ASSERT_TLM_ReadErrors(0, 3U);
}

// ----------------------------------------------------------------------
// Tests: error frames
// ----------------------------------------------------------------------

void LinuxCanDriverTester ::testErrorFrames() {
    this->openInterface();
    U8 data[CAN_MAX_DLEN] = {};

    // Warning, then passive
    data[1] = CAN_ERR_CRTL_TX_WARNING;
    TestCanSyscalls::queueErrorFrame(CAN_ERR_CRTL, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_EVENTS_BusStateChanged(0, CanBusState::ERROR_ACTIVE, CanBusState::ERROR_WARNING);

    data[1] = CAN_ERR_CRTL_RX_PASSIVE;
    TestCanSyscalls::queueErrorFrame(CAN_ERR_CRTL, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_EVENTS_BusStateChanged(1, CanBusState::ERROR_WARNING, CanBusState::ERROR_PASSIVE);

    // Controller overflows leave the state alone
    data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
    TestCanSyscalls::queueErrorFrame(CAN_ERR_CRTL, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    data[1] = CAN_ERR_CRTL_TX_OVERFLOW;
    TestCanSyscalls::queueErrorFrame(CAN_ERR_CRTL, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_EVENTS_ControllerOverflow_SIZE(2);
    ASSERT_EVENTS_ControllerOverflow(0, CanOverflowDirection::RX);
    ASSERT_EVENTS_ControllerOverflow(1, CanOverflowDirection::TX);
    ASSERT_EVENTS_BusStateChanged_SIZE(2);

    // Protocol error: counted, no event
    data[1] = 0;
    TestCanSyscalls::queueErrorFrame(CAN_ERR_PROT | CAN_ERR_BUSERROR, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);

    TestCanSyscalls::queueErrorFrame(CAN_ERR_ACK | CAN_ERR_BUSERROR, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_EVENTS_NoAck_SIZE(1);

    data[1] = CAN_ERR_CRTL_ACTIVE;
    TestCanSyscalls::queueErrorFrame(CAN_ERR_CRTL, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_EVENTS_BusStateChanged(2, CanBusState::ERROR_PASSIVE, CanBusState::ERROR_ACTIVE);

    ASSERT_EVENTS_SIZE(6);
    ASSERT_from_canReceive_SIZE(0);

    // No TEC or REC yet
    this->invoke_to_run(0, 0);
    ASSERT_TLM_ErrorFrames(0, 7U);
    ASSERT_TLM_ProtocolErrors(0, 1U);
    ASSERT_TLM_BusState(0, CanBusState::ERROR_ACTIVE);
    ASSERT_TLM_Tec_SIZE(0);
    ASSERT_TLM_Rec_SIZE(0);

    data[1] = 0;
    data[6] = 96;
    data[7] = 12;
    TestCanSyscalls::queueErrorFrame(CAN_ERR_CNT, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    this->invoke_to_run(0, 0);
    ASSERT_TLM_Tec(0, 96);
    ASSERT_TLM_Rec(0, 12);
}

void LinuxCanDriverTester ::testBusOffAndRestart() {
    this->openInterface();
    U8 data[CAN_MAX_DLEN] = {};

    TestCanSyscalls::queueErrorFrame(CAN_ERR_BUSOFF, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_EVENTS_BusOff_SIZE(1);
    ASSERT_EVENTS_BusStateChanged(0, CanBusState::ERROR_ACTIVE, CanBusState::BUS_OFF);
    this->invoke_to_run(0, 0);
    ASSERT_TLM_BusState(0, CanBusState::BUS_OFF);

    TestCanSyscalls::queueErrorFrame(CAN_ERR_RESTARTED, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_EVENTS_BusRestarted_SIZE(1);
    ASSERT_EVENTS_BusStateChanged(1, CanBusState::BUS_OFF, CanBusState::ERROR_ACTIVE);
    this->invoke_to_run(0, 0);
    ASSERT_TLM_BusState(1, CanBusState::ERROR_ACTIVE);
}

void LinuxCanDriverTester ::testThrottleRearmOnErrorActive() {
    this->openInterface();
    U8 data[CAN_MAX_DLEN] = {};

    for (U32 i = 0; i < 7; i++) {
        TestCanSyscalls::queueErrorFrame(CAN_ERR_ACK, data);
        this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    }
    ASSERT_EVENTS_NoAck_SIZE(5);

    // A fault episode ends when the bus returns to error-active
    data[1] = CAN_ERR_CRTL_TX_PASSIVE;
    TestCanSyscalls::queueErrorFrame(CAN_ERR_CRTL, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    data[1] = CAN_ERR_CRTL_ACTIVE;
    TestCanSyscalls::queueErrorFrame(CAN_ERR_CRTL, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);

    data[1] = 0;
    TestCanSyscalls::queueErrorFrame(CAN_ERR_ACK, data);
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_EVENTS_NoAck_SIZE(6);
}

void LinuxCanDriverTester ::testSocketDrops() {
    this->openInterface();

    TestCanSyscalls::queueMessage(frameWithDropCount(3));
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_EVENTS_SocketRxDrops_SIZE(1);
    ASSERT_EVENTS_SocketRxDrops(0, 3U);

    // Same running total: nothing new was dropped
    TestCanSyscalls::queueMessage(frameWithDropCount(3));
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_EVENTS_SocketRxDrops_SIZE(1);

    // The event reports only the new drops
    TestCanSyscalls::queueMessage(frameWithDropCount(10));
    this->readOnce(LinuxCanDriver::ReadResult::HANDLED);
    ASSERT_EVENTS_SocketRxDrops(1, 7U);

    // Frames carrying a drop count are still delivered
    ASSERT_from_canReceive_SIZE(3);

    this->invoke_to_run(0, 0);
    ASSERT_TLM_SocketRxDrops(0, 10U);
}

// ----------------------------------------------------------------------
// Tests: run and the read task
// ----------------------------------------------------------------------

void LinuxCanDriverTester ::testRunTelemetry() {
    fake().statistics["rx_errors"] = 1;
    fake().statistics["rx_over_errors"] = 2;
    fake().statistics["rx_dropped"] = 3;
    fake().statistics["tx_errors"] = 4;
    // tx_dropped is missing, so it is not published

    // Before open, only the counters are published
    this->invoke_to_run(0, 0);
    ASSERT_TLM_FramesRx(0, 0U);
    ASSERT_TLM_BusState(0, CanBusState::ERROR_ACTIVE);
    ASSERT_TLM_IfRxErrors_SIZE(0);

    this->openInterface();
    this->invoke_to_run(0, 0);
    ASSERT_TLM_IfRxErrors(0, 1U);
    ASSERT_TLM_IfRxOverErrors(0, 2U);
    ASSERT_TLM_IfRxDropped(0, 3U);
    ASSERT_TLM_IfTxErrors(0, 4U);
    ASSERT_TLM_IfTxDropped_SIZE(0);
}

void LinuxCanDriverTester ::testReadTaskQuit() {
    this->openInterface();
    this->component.start();
    // Let the task reach its wait
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const auto quitTime = std::chrono::steady_clock::now();
    this->component.quitReadThread();
    ASSERT_EQ(this->component.join(), Os::Task::OP_OK);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - quitTime);

    // The fake's poll() sleeps for the full read timeout, as the real one does
    ASSERT_LT(elapsed.count(), 2 * LinuxCanDriver::READ_TIMEOUT_MS);
}

// ----------------------------------------------------------------------
// Helper functions
// ----------------------------------------------------------------------

void LinuxCanDriverTester ::openInterface() {
    ASSERT_TRUE(this->component.open("can0"));
    this->clearHistory();
}

void LinuxCanDriverTester ::readOnce(const LinuxCanDriver::ReadResult expected) {
    ASSERT_EQ(this->component.readOnce(), expected);
}

CanFrame LinuxCanDriverTester ::makeFrame(const U32 id, const U8 flags, const U8 len, const U8 base) {
    U8 data[CAN_FRAME_MAX_LEN] = {};
    for (U8 i = 0; (i < len) && (i < CAN_FRAME_MAX_LEN); i++) {
        data[i] = static_cast<U8>(base + i);
    }
    return CanFrame(id, flags, len, data);
}

}  // namespace SocketCan
