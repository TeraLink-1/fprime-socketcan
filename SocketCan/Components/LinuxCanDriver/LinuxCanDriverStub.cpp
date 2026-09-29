// ======================================================================
// \title  LinuxCanDriverStub.cpp
// \author zach
// \brief  Stub implementation of LinuxCanDriver for non-Linux hosts
//
// Built when FPRIME_USE_STUBBED_DRIVERS is on (the default on Darwin).
// Lets the rest of the project build without SocketCAN. The driver never
// opens: sends return NOT_OPEN and nothing is ever received.
// ======================================================================

#include "SocketCan/Components/LinuxCanDriver/LinuxCanDriver.hpp"

#include <cerrno>

namespace SocketCan {

// ----------------------------------------------------------------------
// Setup and teardown
// ----------------------------------------------------------------------

bool LinuxCanDriver ::open(const Fw::String& interfaceName, const U32 receiveBufferBytes) {
    // Truncation doesn't matter: the name is only shown in the event
    this->m_interfaceName = interfaceName;
    this->log_WARNING_HI_InterfaceOpenFailed(this->m_interfaceName, ENOSYS);
    return false;
}

void LinuxCanDriver ::start(const FwTaskPriorityType priority,
                            const Os::Task::ParamType stackSize,
                            const Os::Task::ParamType cpuAffinity) {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

SocketCan::CanStatus LinuxCanDriver ::canSend_handler(const FwIndexType portNum, const SocketCan::CanFrame& frame) {
    return SocketCan::CanStatus::NOT_OPEN;
}

}  // namespace SocketCan
