####
# SocketCan/Ports
#
# CAN types and ports shared by CAN drivers and their consumers.
# Platform-independent: do not restrict this module to Linux.
####

register_fprime_module(
        AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/CanTypes.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/CanPorts.fpp"
)
