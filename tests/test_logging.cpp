#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <string>

#include <corium/corium.hpp>

using namespace corium;
using namespace corium::logging;

TEST(LoggingTest, LogLevelHelpers)
{
    EXPECT_STREQ(logLevelToString(LogLevel::Trace), "TRACE");
    EXPECT_STREQ(logLevelToString(LogLevel::Debug), "DEBUG");
    EXPECT_STREQ(logLevelToString(LogLevel::Info), "INFO");
    EXPECT_STREQ(logLevelToString(LogLevel::Warn), "WARN");
    EXPECT_STREQ(logLevelToString(LogLevel::Error), "ERROR");
    EXPECT_STREQ(logLevelToString(LogLevel::Critical), "CRITICAL");
    EXPECT_STREQ(logLevelToString(LogLevel::Off), "OFF");

    EXPECT_NE(logLevelToColor(LogLevel::Info), nullptr);
}

TEST(LoggingTest, LogEventZeroHeapStorage)
{
    LogEvent event(LogLevel::Info, "Test Message 123", "TestCat");
    EXPECT_EQ(event.level, LogLevel::Info);
    EXPECT_STREQ(event.category, "TestCat");
    EXPECT_EQ(event.view(), "Test Message 123");
    EXPECT_EQ(event.length, 16u);

    // Test message length capping
    LogEventT<10> smallEvent(LogLevel::Warn, "Very long text exceeding limit");
    EXPECT_EQ(smallEvent.length, 9u);
    EXPECT_EQ(smallEvent.view(), "Very long");
}

TEST(LoggingTest, NullLogSinkNoOp)
{
    sinks::NullLogSink sink;
    LogEvent event(LogLevel::Info, "Testing Null Sink");
    sink.write(event); // Should compile and execute safely as no-op
}

TEST(LoggingTest, FileLogSinkOutput)
{
    const char* testFilePath = "test_log_output.tmp";
    {
        sinks::FileLogSink fileSink(testFilePath);
        EXPECT_TRUE(fileSink.isOpen());

        LogEvent event(LogLevel::Warn, "Disk space low", "Storage");
        fileSink.write(event);
    } // File automatically flushed and closed

    std::ifstream file(testFilePath);
    EXPECT_TRUE(file.is_open());
    std::string line;
    std::getline(file, line);
    EXPECT_NE(line.find("[WARN] [Storage] Disk space low"), std::string::npos);
    file.close();
    std::remove(testFilePath);
}

struct LoggingMockSink {
    mutable int writeCount = 0;
    mutable LogLevel lastLevel = LogLevel::Trace;

    template <std::size_t N>
    void write(const LogEventT<N>& event) const
    {
        writeCount++;
        lastLevel = event.level;
    }
};

TEST(LoggingTest, LoggerLevelFiltering)
{
    LoggingMockSink mockSink;
    LoggerT<LoggingMockSink> logger("App", LogLevel::Warn, mockSink);

    logger.info("This should be filtered out");
    EXPECT_EQ(logger.sink().writeCount, 0);

    logger.warn("Warning message: %d", 100);
    EXPECT_EQ(logger.sink().writeCount, 1);
    EXPECT_EQ(logger.sink().lastLevel, LogLevel::Warn);

    logger.error("Critical failure!");
    EXPECT_EQ(logger.sink().writeCount, 2);
    EXPECT_EQ(logger.sink().lastLevel, LogLevel::Error);
}

TEST(LoggingTest, LoggerIntegrationWithRuntime)
{
    using AppEvents = std::variant<QuitEvent, LogEvent>;
    using TestRuntime = RuntimeBuilder::WithEvents<AppEvents>::Build;

    class LogApp : public Application<LogApp, AppEvents> {
    public:
        bool logReceived = false;
        std::string receivedMsg;

        void onRegisterHandlers()
        {
            this->on([this](const LogEvent& e) {
                logReceived = true;
                receivedMsg = std::string(e.view());
            });
        }
    };

    TestRuntime runtime;
    LogApp app;
    runtime.initialize(app);

    ConsoleLogger logger("RuntimeApp", LogLevel::Info);
    logger.logToSink(runtime.eventSink(), LogLevel::Info, "Runtime Log Message %s", "OK");

    runtime.pump();

    EXPECT_TRUE(app.logReceived);
    EXPECT_EQ(app.receivedMsg, "Runtime Log Message OK");

    runtime.shutdown();
}
