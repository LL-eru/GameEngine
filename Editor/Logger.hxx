//	ファイル名	：Logging.hxx
//	  概  要	：
//	作	成	者	：daigo
//	 作成日時	：2026/03/30 23:43:54
//_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/

// =-=-= インクルードガード部 =-=-=
#pragma once
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

// =========================
// Log Level
// =========================
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

// =========================
// Log Message
// =========================
struct LogMessage {
    LogLevel level;
    std::string category;
    std::string message;
    std::thread::id threadId;
    std::chrono::system_clock::time_point time;
};

// =========================
// Sink Interface
// =========================
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

// =========================
// Console Sink
// =========================
class ConsoleSink : public ILogSink {
public:
    void Write(const LogMessage& msg) override;
};

// =========================
// File Sink
// =========================
class FileSink : public ILogSink {
public:
    explicit FileSink(const std::string& filename);
    void Write(const LogMessage& msg) override;

private:
    std::mutex fileMutex;
    FILE* file;
};

// =========================
// Logger Core
// =========================
class Logger {
public:
    static void Init();
    static void Uninit();

    static void AddSink(std::shared_ptr<ILogSink> sink);

    static void Log(LogLevel level, const std::string& category, const std::string& msg);

    static void SetLevel(LogLevel level);

private:
    static void Worker();

private:
    static std::vector<std::shared_ptr<ILogSink>> sinks;
    static std::queue<LogMessage> queue;

    static std::mutex mutex;
    static std::condition_variable cv;

    static std::thread workerThread;
    static std::atomic<bool> running;

    static LogLevel currentLevel;
};

// =========================
// Macros
// =========================
#define LOG_TRACE(cat, msg) Logger::Log(LogLevel::Trace, cat, msg)
#define LOG_DEBUG(cat, msg) Logger::Log(LogLevel::Debug, cat, msg)
#define LOG_INFO(cat, msg)  Logger::Log(LogLevel::Info,  cat, msg)
#define LOG_WARN(cat, msg)  Logger::Log(LogLevel::Warn,  cat, msg)
#define LOG_ERROR(cat, msg) Logger::Log(LogLevel::Error, cat, msg)
#define LOG_FATAL(cat, msg) Logger::Log(LogLevel::Fatal, cat, msg)