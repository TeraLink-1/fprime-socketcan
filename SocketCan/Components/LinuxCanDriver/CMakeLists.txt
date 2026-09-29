####
# F Prime CMakeLists.txt:
#
# SOURCES: list of source files (to be compiled)
# AUTOCODER_INPUTS: list of files to be passed to the autocoders
# DEPENDS: list of libraries that this module depends on
#
# More information in the F´ CMake API documentation:
# https://fprime.jpl.nasa.gov/latest/docs/reference/api/cmake/API/
#
####

# Module names are derived from the path from the nearest project/library/framework
# root when not specifically overridden by the developer, i.e. the module defined by
# `MyProj/Some/Path/CMakeLists.txt` will be named `MyProj_Some_Path`.

# SocketCAN is Linux only, so use stubbed drivers if not on Linux

if (NOT FPRIME_USE_STUBBED_DRIVERS)
    restrict_platforms(Linux)
endif()

if (FPRIME_USE_STUBBED_DRIVERS)
    set(LINUX_CAN_DRIVER_PLATFORM_SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/LinuxCanDriverStub.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/CanSyscallsStub.cpp"
    )
else()
    # CanSyscallsLinux.cpp must stay a separate source: unit tests replace
    # its functions at link time
    set(LINUX_CAN_DRIVER_PLATFORM_SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/LinuxCanDriverLinux.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/CanSyscallsLinux.cpp"
    )
endif()

register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/LinuxCanDriver.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/LinuxCanDriver.cpp"
        ${LINUX_CAN_DRIVER_PLATFORM_SOURCES}
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/LinuxCanDriver.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/CanSyscalls.hpp"
)

### Unit Tests ###
# The tests exercise the Linux implementation, so they build only when it
# does. TestCanSyscalls.cpp replaces CanSyscallsLinux.cpp at link time.
if (NOT FPRIME_USE_STUBBED_DRIVERS)
    register_fprime_ut(
        AUTOCODER_INPUTS
            "${CMAKE_CURRENT_LIST_DIR}/LinuxCanDriver.fpp"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/test/ut/LinuxCanDriverTestMain.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/test/ut/LinuxCanDriverTester.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/test/ut/TestCanSyscalls.cpp"
        UT_AUTO_HELPERS
    )
endif()
