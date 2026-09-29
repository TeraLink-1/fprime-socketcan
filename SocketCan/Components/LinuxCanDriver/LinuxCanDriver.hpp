// ======================================================================
// \title  LinuxCanDriver.hpp
// \author zach
// \brief  hpp file for LinuxCanDriver component implementation class
//
// This header is shared by the Linux and stub builds, so it must not
// include Linux headers. Linux types appear only as forward declarations.
// ======================================================================

#ifndef SocketCan_LinuxCanDriver_HPP
#define SocketCan_LinuxCanDriver_HPP

#include <Fw/Types/String.hpp>
#include <Fw/Types/StringTemplate.hpp>
#include <Os/Task.hpp>
#include <atomic>

#include "SocketCan/Components/LinuxCanDriver/FppConstantsAc.hpp"
#include "SocketCan/Components/LinuxCanDriver/LinuxCanDriverComponentAc.hpp"

struct can_frame;

namespace SocketCan {

class LinuxCanDriver final : public LinuxCanDriverComponentBase {
    // Unit tests drive the read loop one pass at a time through readOnce()
    friend class LinuxCanDriverTester;

  public:
    //! How long the read task waits for a frame before checking the quit flag
    static constexpr I32 READ_TIMEOUT_MS = 100;

    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    explicit LinuxCanDriver(const char* const compName);

    //! Closes the socket if it is open. Stop and join the read task first.
    ~LinuxCanDriver() override;

    LinuxCanDriver(const LinuxCanDriver&) = delete;
    LinuxCanDriver& operator=(const LinuxCanDriver&) = delete;
    LinuxCanDriver(LinuxCanDriver&&) = delete;
    LinuxCanDriver& operator=(LinuxCanDriver&&) = delete;

    // ----------------------------------------------------------------------
    // Setup and teardown
    // ----------------------------------------------------------------------

    //! Open a raw CAN socket bound to the named interface. The interface's
    //! bitrate and restart settings belong to the system configuration and
    //! must already be applied, and the interface must be up.
    //!
    //! Call before start(). Emits InterfaceOpenFailed or InterfaceDown on
    //! failure, after which open() may be retried. Asserts if the socket is
    //! already open.
    //!
    //! \return true if the socket is open and ready
    bool open(const Fw::String& interfaceName,  //!< CAN interface name, e.g. "can0"
              U32 receiveBufferBytes = 0        //!< SO_RCVBUF size; 0 keeps the kernel default
    );

    void start(FwTaskPriorityType priority = Os::Task::TASK_PRIORITY_DEFAULT,
               Os::Task::ParamType stackSize = Os::Task::TASK_DEFAULT,
               Os::Task::ParamType cpuAffinity = Os::Task::TASK_DEFAULT);

    //! Ask the read task to exit. It exits within READ_TIMEOUT_MS.
    void quitReadThread();

    Os::Task::Status join();

    // ----------------------------------------------------------------------
    // Frame helpers (platform-independent)
    // ----------------------------------------------------------------------

    //! Check that a frame is valid to send: len at most 8, the identifier
    //! fits its format, and no undefined flag bits are set
    static bool isValidFrame(const CanFrame& frame);

  private:
    //! Result of one pass of the read loop
    enum class ReadResult {
        IDLE,     //!< Timed out or interrupted; nothing received
        HANDLED,  //!< A message was received and handled
        FAILED,   //!< A read failed; the caller should back off
    };

    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Sends without blocking; a full TX queue returns TX_BUSY
    SocketCan::CanStatus canSend_handler(FwIndexType portNum, const SocketCan::CanFrame& frame) override;

    //! Publishes telemetry, including the interface statistics from sysfs
    void run_handler(FwIndexType portNum, U32 context) override;

    // ----------------------------------------------------------------------
    // Linux only; defined in LinuxCanDriverLinux.cpp
    // ----------------------------------------------------------------------

    bool openFailed(int fd, I32 error) const;

    static void readTaskEntry(void* ptr);

    //! Wait up to READ_TIMEOUT_MS for one message and handle it
    ReadResult readOnce();

    void handleDataFrame(const can_frame& kernelFrame);

    //! Update the bus state, counters, and events from a received error frame
    void handleErrorFrame(const can_frame& kernelFrame);

    // ----------------------------------------------------------------------
    // Shared bookkeeping (platform-independent; defined in LinuxCanDriver.cpp)
    // ----------------------------------------------------------------------

    //! Record the socket's cumulative drop count from SO_RXQ_OVFL
    void updateSocketDrops(U32 dropCount);

    void reportReadError(I32 error);

    //! Record a successful read, re-arming the ReadError event
    void clearReadError();

    //! Move to a new bus state and emit BusStateChanged. Returning to
    //! ERROR_ACTIVE re-arms the throttled bus events.
    void setBusState(CanBusState::T state);

    //! Publish the interface statistics from sysfs that can be read
    void publishInterfaceStatistics() const;

    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    int m_fd;  //!< Socket, or -1 when closed. Written only by open(), before start().
    Fw::StringTemplate<CAN_INTERFACE_NAME_MAX_LENGTH> m_interfaceName;
    Os::Task m_readTask;
    std::atomic<bool> m_quitReadThread;

    // Read task state
    U32 m_lastDropCount;      //!< Last SO_RXQ_OVFL value seen
    bool m_readErrorLatched;  //!< A ReadError was emitted since the last good read

    // Send state (protected by the canSend guard)
    bool m_sendErrorLatched;  //!< A SendError was emitted since the last good send

    // Published by run: FramesTx, TxBusy, and TxErrors are written by canSend,
    // everything else by the read task
    std::atomic<U64> m_framesRx;
    std::atomic<U64> m_framesTx;
    std::atomic<U32> m_txBusy;
    std::atomic<U32> m_txErrors;
    std::atomic<U32> m_readErrors;
    std::atomic<U32> m_errorFrames;
    std::atomic<U32> m_protocolErrors;
    std::atomic<U32> m_socketRxDrops;
    std::atomic<CanBusState::T> m_busState;
    std::atomic<U8> m_tec;
    std::atomic<U8> m_rec;
    std::atomic<bool> m_errorCountersValid;  //!< An error frame has reported TEC and REC
};

}  // namespace SocketCan

#endif
