// ======================================================================
// \title  LinuxCanDriver.cpp
// \author zach
// \brief  cpp file for LinuxCanDriver component implementation class
//
// Platform-independent code, built for both the Linux and stub variants.
// Linux-specific code goes in LinuxCanDriverLinux.cpp, and its stubbed
// counterparts in LinuxCanDriverStub.cpp.
// ======================================================================

#include "SocketCan/Components/LinuxCanDriver/LinuxCanDriver.hpp"

#include "SocketCan/Components/LinuxCanDriver/CanSyscalls.hpp"
#include "SocketCan/Ports/FppConstantsAc.hpp"

namespace SocketCan {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

LinuxCanDriver ::LinuxCanDriver(const char* const compName)
    : LinuxCanDriverComponentBase(compName),
      m_fd(-1),
      m_interfaceName(),
      m_quitReadThread(false),
      m_lastDropCount(0),
      m_readErrorLatched(false),
      m_sendErrorLatched(false),
      m_framesRx(0),
      m_framesTx(0),
      m_txBusy(0),
      m_txErrors(0),
      m_readErrors(0),
      m_errorFrames(0),
      m_protocolErrors(0),
      m_socketRxDrops(0),
      m_busState(CanBusState::ERROR_ACTIVE),
      m_tec(0),
      m_rec(0),
      m_errorCountersValid(false) {}

LinuxCanDriver ::~LinuxCanDriver() {
    if (this->m_fd >= 0) {
        // Nothing can be done about a failed close while being destroyed
        (void)CanSyscalls::close(this->m_fd);
        this->m_fd = -1;
    }
}

// ----------------------------------------------------------------------
// Setup and teardown
// ----------------------------------------------------------------------

void LinuxCanDriver ::quitReadThread() {
    this->m_quitReadThread = true;
}

Os::Task::Status LinuxCanDriver ::join() {
    return this->m_readTask.join();
}

// ----------------------------------------------------------------------
// Frame helpers
// ----------------------------------------------------------------------

bool LinuxCanDriver ::isValidFrame(const CanFrame& frame) {
    const U8 flags = frame.get_flags();
    if ((flags & ~static_cast<U8>(CAN_FRAME_FLAG_IDE | CAN_FRAME_FLAG_RTR)) != 0) {
        return false;
    }
    if (frame.get_len() > CAN_FRAME_MAX_LEN) {
        return false;
    }
    const U32 maxId = ((flags & CAN_FRAME_FLAG_IDE) != 0) ? static_cast<U32>(CAN_FRAME_MAX_EXT_ID)
                                                           : static_cast<U32>(CAN_FRAME_MAX_STD_ID);
    return frame.get_id() <= maxId;
}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void LinuxCanDriver ::run_handler(const FwIndexType portNum, const U32 context) {
    this->tlmWrite_FramesRx(this->m_framesRx.load());
    this->tlmWrite_FramesTx(this->m_framesTx.load());
    this->tlmWrite_TxBusy(this->m_txBusy.load());
    this->tlmWrite_TxErrors(this->m_txErrors.load());
    this->tlmWrite_ReadErrors(this->m_readErrors.load());
    this->tlmWrite_ErrorFrames(this->m_errorFrames.load());
    this->tlmWrite_ProtocolErrors(this->m_protocolErrors.load());
    this->tlmWrite_BusState(this->m_busState.load());
    this->tlmWrite_SocketRxDrops(this->m_socketRxDrops.load());
    // TEC and REC are only meaningful once an error frame has carried them
    if (this->m_errorCountersValid.load()) {
        this->tlmWrite_Tec(this->m_tec.load());
        this->tlmWrite_Rec(this->m_rec.load());
    }
    if (this->m_fd >= 0) {
        this->publishInterfaceStatistics();
    }
}

// ----------------------------------------------------------------------
// Shared bookkeeping
// ----------------------------------------------------------------------

void LinuxCanDriver ::updateSocketDrops(const U32 dropCount) {
    // SO_RXQ_OVFL reports a running total for the socket; unsigned
    // subtraction handles wraparound
    const U32 newDrops = dropCount - this->m_lastDropCount;
    this->m_lastDropCount = dropCount;
    if (newDrops != 0) {
        this->m_socketRxDrops = dropCount;
        this->log_WARNING_HI_SocketRxDrops(newDrops);
    }
}

void LinuxCanDriver ::reportReadError(const I32 error) {
    ++this->m_readErrors;
    this->m_readErrorLatched = true;
    this->log_WARNING_HI_ReadError(error);
}

void LinuxCanDriver ::clearReadError() {
    if (this->m_readErrorLatched) {
        this->m_readErrorLatched = false;
        this->log_WARNING_HI_ReadError_ThrottleClear();
    }
}

void LinuxCanDriver ::setBusState(const CanBusState::T state) {
    const CanBusState::T previous = this->m_busState.exchange(state);
    if (previous == state) {
        return;
    }
    this->log_WARNING_LO_BusStateChanged(previous, state);
    if (state == CanBusState::ERROR_ACTIVE) {
        // The fault episode is over: re-arm the events it may have throttled
        this->log_WARNING_LO_BusStateChanged_ThrottleClear();
        this->log_WARNING_HI_BusOff_ThrottleClear();
        this->log_ACTIVITY_HI_BusRestarted_ThrottleClear();
        this->log_WARNING_LO_NoAck_ThrottleClear();
    }
}

void LinuxCanDriver ::publishInterfaceStatistics() const {
    const char* const interfaceName = this->m_interfaceName.toChar();
    U64 value = 0;
    if (CanSyscalls::readInterfaceStatistic(interfaceName, "rx_errors", value)) {
        this->tlmWrite_IfRxErrors(value);
    }
    if (CanSyscalls::readInterfaceStatistic(interfaceName, "rx_over_errors", value)) {
        this->tlmWrite_IfRxOverErrors(value);
    }
    if (CanSyscalls::readInterfaceStatistic(interfaceName, "rx_dropped", value)) {
        this->tlmWrite_IfRxDropped(value);
    }
    if (CanSyscalls::readInterfaceStatistic(interfaceName, "tx_errors", value)) {
        this->tlmWrite_IfTxErrors(value);
    }
    if (CanSyscalls::readInterfaceStatistic(interfaceName, "tx_dropped", value)) {
        this->tlmWrite_IfTxDropped(value);
    }
}

}  // namespace SocketCan
