module SocketCan {
  port CanSend(frame: CanFrame) -> CanStatus

  port CanReceive(frame: CanFrame, rxTime: Fw.Time)
}