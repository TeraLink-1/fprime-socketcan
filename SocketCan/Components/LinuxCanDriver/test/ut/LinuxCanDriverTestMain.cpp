// ======================================================================
// \title  LinuxCanDriverTestMain.cpp
// \author zach
// \brief  cpp file for LinuxCanDriver component test main function
// ======================================================================

#include "LinuxCanDriverTester.hpp"

TEST(Open, Nominal) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testOpen();
}

TEST(Open, ReceiveBuffer) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testOpenReceiveBuffer();
}

TEST(Open, Failures) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testOpenFailures();
}

TEST(Send, BeforeOpen) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testSendBeforeOpen();
}

TEST(Send, Conversion) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testSendConversion();
}

TEST(Send, Validation) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testSendValidation();
}

TEST(Send, Errors) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testSendErrors();
}

TEST(Send, ErrorThrottle) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testSendErrorThrottle();
}

TEST(Receive, Conversion) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testReceiveConversion();
}

TEST(Receive, BadLength) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testReceiveBadLength();
}

TEST(Receive, ReadErrors) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testReadErrors();
}

TEST(ErrorFrames, Classes) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testErrorFrames();
}

TEST(ErrorFrames, BusOffAndRestart) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testBusOffAndRestart();
}

TEST(ErrorFrames, ThrottleRearmOnErrorActive) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testThrottleRearmOnErrorActive();
}

TEST(Receive, SocketDrops) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testSocketDrops();
}

TEST(Run, Telemetry) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testRunTelemetry();
}

TEST(ReadTask, QuitWithinTimeout) {
    SocketCan::LinuxCanDriverTester tester;
    tester.testReadTaskQuit();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
