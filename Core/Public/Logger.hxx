#pragma once

#include "CoreExport.hxx"
#include "../../Interface/HostServices.hxx"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cstdio>

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

struct LogMessage {
    LogLevel level;
    std::string category;
    std::string message;
    std::thread::id threadId;
    std::chrono::system_clock::time_point time;
};

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Write(const LogMessage& msg) = 0;

protected:
    inline static std::string ToString(LogLevel level) {
        switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        }
        return "UNKNOWN";
    }
};

class GE_API ConsoleSink : public ILogSink {
public:
    void Write(const LogMessage& msg) override;
};

class GE_API FileSink : public ILogSink {
public:
    explicit FileSink(const std::string& filename);
    ~FileSink();
    void Write(const LogMessage& msg) override;

private:
    std::mutex fileMutex;
    FILE* file = nullptr;
};

class GE_API Logger {
public:
    static void Init();
    static void Uninit();
    static void AddSink(std::shared_ptr<ILogSink> sink);
    static void Log(LogLevel level, const std::string& category, const std::string& msg);
    static void SetLevel(LogLevel level);

private:
    static void Worker();

    static std::vector<std::shared_ptr<ILogSink>> sinks;
    static std::mutex sinksMutex;
    static std::queue<LogMessage> queue;
    static std::mutex mutex;
    static std::condition_variable cv;
    static std::thread workerThread;
    static std::atomic<bool> running;
    static LogLevel currentLevel;
};

GE_API void CoreLog(HostLogLevel level, const char* category, const char* message);

[[noreturn]] GE_API void LogFatalFlushAndAbort(const char* category, const char* message);
