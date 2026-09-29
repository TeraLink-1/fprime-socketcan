module SocketCan {
  @ Maximum data length of a classic CAN frame
  constant CAN_FRAME_MAX_LEN = 8

  @ Largest 11-bit (standard) identifier
  constant CAN_FRAME_MAX_STD_ID = 0x7FF

  @ Largest 29-bit (extended) identifier
  constant CAN_FRAME_MAX_EXT_ID = 0x1FFFFFFF

  @ CanFrame.flags bit: 29-bit (extended) identifier
  constant CAN_FRAME_FLAG_IDE = 0x01

  @ CanFrame.flags bit: remote transmission request
  constant CAN_FRAME_FLAG_RTR = 0x02

  struct CanFrame {
    @ CAN identifier (either 11-bit or 29-bit, see flags)
    $id: U32
    @ bit0 IDE (29-bit), bit1 RTR; all other bits must be zero
    flags: U8
    @ Data length (0-8); for RTR frames, the requested DLC
    len: U8
    @ Frame data; bytes past len are zero
    data: [CAN_FRAME_MAX_LEN] U8
  }

  @ Result of sending a frame
  enum CanStatus : U8 {
    @ Queued for transmission; not yet confirmed on the bus
    OK,
    @ The driver has no open socket
    NOT_OPEN,
    @ The frame failed validation and was not sent
    INVALID_FRAME,
    @ The interface TX queue is full; the caller may retry
    TX_BUSY,
    @ The send failed for another reason, reported by the driver
    ERROR
  }
}
