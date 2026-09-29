module SocketCan {

    @ Longest CAN interface name: Linux IFNAMSIZ (16) less the terminator
    constant CAN_INTERFACE_NAME_MAX_LENGTH = 15

    @ CAN controller error state, as reported by SocketCAN error frames
    enum CanBusState : U8 {
        ERROR_ACTIVE
        ERROR_WARNING
        ERROR_PASSIVE
        BUS_OFF
    }

    @ Direction of a CAN controller FIFO overflow
    enum CanOverflowDirection : U8 {
        RX
        TX
    }

    @ Driver for SocketCAN on Linux
    passive component LinuxCanDriver {

        # ----------------------------------------------------------------------
        # General ports
        # ----------------------------------------------------------------------

        @ Send one frame; returns immediately with the result
        guarded input port canSend: SocketCan.CanSend

        @ Received data frames; called on the read task
        output port canReceive: SocketCan.CanReceive

        @ The rate group input for publishing telemetry and interface statistics
        sync input port run: Svc.Sched

        # ----------------------------------------------------------------------
        # Special ports
        # ----------------------------------------------------------------------

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel

        # ----------------------------------------------------------------------
        # Events
        # ----------------------------------------------------------------------

        @ The CAN interface could not be opened or configured
        event InterfaceOpenFailed(
                                   interfaceName: string size CAN_INTERFACE_NAME_MAX_LENGTH @< The interface name
                                   error: I32 @< The errno value
                                 ) \
            severity warning high \
            format "Failed to open CAN interface {}: errno {}"

        @ The CAN interface exists but is not up
        event InterfaceDown(
                             interfaceName: string size CAN_INTERFACE_NAME_MAX_LENGTH @< The interface name
                           ) \
            severity warning high \
            format "CAN interface {} is down"

        @ The CAN interface was opened and the socket is ready
        event InterfaceOpened(
                               interfaceName: string size CAN_INTERFACE_NAME_MAX_LENGTH @< The interface name
                             ) \
            severity activity high \
            format "CAN interface {} opened"

        @ Reading from the CAN socket failed
        event ReadError(
                         error: I32 @< The errno value
                       ) \
            severity warning high \
            format "CAN socket read failed: errno {}" \
            throttle 5

        @ Writing to the CAN socket failed with an error other than a full TX queue
        event SendError(
                         error: I32 @< The errno value
                       ) \
            severity warning high \
            format "CAN socket write failed: errno {}" \
            throttle 5

        @ The CAN controller changed error state
        event BusStateChanged(
                               previous: SocketCan.CanBusState @< The previous state
                               current: SocketCan.CanBusState @< The new state
                             ) \
            severity warning low \
            format "CAN bus state changed from {} to {}" \
            throttle 10

        @ The CAN controller entered bus-off
        event BusOff \
            severity warning high \
            format "CAN controller is bus-off" \
            throttle 5

        @ The CAN controller restarted after bus-off
        event BusRestarted \
            severity activity high \
            format "CAN controller restarted" \
            throttle 5

        @ A CAN controller FIFO overflowed and frames were lost
        event ControllerOverflow(
                                  direction: SocketCan.CanOverflowDirection @< The FIFO that overflowed
                                ) \
            severity warning high \
            format "CAN controller {} overflow" \
            throttle 5

        @ The socket receive queue dropped frames because the read task fell behind
        event SocketRxDrops(
                             $count: U32 @< Frames dropped since the last report
                           ) \
            severity warning high \
            format "CAN socket dropped {} received frames" \
            throttle 5

        @ No other node acknowledged a transmitted frame
        event NoAck \
            severity warning low \
            format "CAN frame not acknowledged by any node" \
            throttle 5

        # ----------------------------------------------------------------------
        # Telemetry
        # ----------------------------------------------------------------------

        @ Data frames received
        telemetry FramesRx: U64

        @ Data frames accepted by the socket for transmission
        telemetry FramesTx: U64

        @ Sends rejected because the interface TX queue was full
        telemetry TxBusy: U32

        @ Sends that failed with an error other than a full TX queue
        telemetry TxErrors: U32

        @ Failed socket reads, including reads of the wrong length
        telemetry ReadErrors: U32

        @ Error frames received
        telemetry ErrorFrames: U32

        @ Protocol errors reported in error frames
        telemetry ProtocolErrors: U32

        @ Current CAN controller error state
        telemetry BusState: SocketCan.CanBusState

        @ Transmit error counter (only if the kernel reports CAN_ERR_CNT)
        telemetry Tec: U8

        @ Receive error counter (only if the kernel reports CAN_ERR_CNT)
        telemetry Rec: U8

        @ Total frames dropped by the socket receive queue (SO_RXQ_OVFL)
        telemetry SocketRxDrops: U32

        @ Interface rx_errors from /sys/class/net/<if>/statistics
        telemetry IfRxErrors: U64

        @ Interface rx_over_errors from /sys/class/net/<if>/statistics
        telemetry IfRxOverErrors: U64

        @ Interface rx_dropped from /sys/class/net/<if>/statistics
        telemetry IfRxDropped: U64

        @ Interface tx_errors from /sys/class/net/<if>/statistics
        telemetry IfTxErrors: U64

        @ Interface tx_dropped from /sys/class/net/<if>/statistics
        telemetry IfTxDropped: U64

    }
}
