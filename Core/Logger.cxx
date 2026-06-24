#include "Public/Logger.hxx"
#include <print>
#include <cstdlib>
#include <system_error>

std::vector<std::shared_ptr<ILogSink>> Logger::sinks;
std::mutex Logger::sinksMutex;
std::queue<LogMessage> Logger::queue;
std::mutex Logger::mutex;
std::condition_variable Logger::cv;
std::thread Logger::workerThread;
std::atomic<bool> Logger::running = false;
LogLevel Logger::currentLevel = LogLevel::Trace;

void ConsoleSink::Write(const LogMessage& msg) {
    auto t = std::chrono::system_clock::to_time_t(msg.time);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << "[" << std::put_time(&tm, "%H:%M:%S") << "]"
        << "[" << ToString(msg.level) << "]"
        << "[" << msg.category << "] "
        << msg.message;
    try {
        std::print("{}\n", oss.str());
        fflush(stdout);
    } catch (const std::system_error&) {
        // Keep console output failures from terminating the logger worker.
    }
}

FileSink::FileSink(const std::string& filename) {
#ifdef _WIN32
    fopen_s(&file, filename.c_str(), "w");
#else
    file = fopen(filename.c_str(), "w");
#endif
}

FileSink::~FileSink() {
    if (file) {
        fflush(file);
        fclose(file);
        file = nullptr;
    }
}

void FileSink::Write(const LogMessage& msg) {
    if (!file) return;
    std::lock_guard<std::mutex> lock(fileMutex);
    auto t = std::chrono::system_clock::to_time_t(msg.time);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << "[" << std::put_time(&tm, "%H:%M:%S") << "]"
        << "[" << ToString(msg.level) << "]"
        << "[" << msg.category << "] "
        << msg.message << "\n";
    fputs(oss.str().c_str(), file);
    fflush(file);
}

void Logger::Init() {
    running = true;
    workerThread = std::thread(Worker);
}

void Logger::Uninit() {
    running = false;
    cv.notify_all();
    if (workerThread.joinable())
        workerThread.join();
}

void Logger::AddSink(std::shared_ptr<ILogSink> sink) {
    std::lock_guard<std::mutex> lock(sinksMutex);
    sinks.push_back(sink);
}

void Logger::SetLevel(LogLevel level) {
    currentLevel = level;
}

void Logger::Log(LogLevel level, const std::string& category, const std::string& msg) {
    if (level < currentLevel) return;
    LogMessage logMsg;
    logMsg.level = level;
    logMsg.category = category;
    logMsg.message = msg;
    logMsg.threadId = std::this_thread::get_id();
    logMsg.time = std::chrono::system_clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(logMsg);
    }
    cv.notify_one();
}

void Logger::Worker() {
    while (running || !queue.empty()) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [] { return !queue.empty() || !running; });
        while (!queue.empty()) {
            LogMessage msg = queue.front();
            queue.pop();
            lock.unlock();
            {
                std::lock_guard<std::mutex> sinksLock(sinksMutex);
                for (auto& sink : sinks) {
                    sink->Write(msg);
                }
            }
            lock.lock();
        }
    }
}

void CoreLog(HostLogLevel level, const char* category, const char* message) {
    Logger::Log(static_cast<LogLevel>(static_cast<int>(level)), category, message ? message : "");
}

[[noreturn]] void LogFatalFlushAndAbort(const char* category, const char* message) {
    Logger::Log(LogLevel::Fatal, category, message ? message : "");
    Logger::Uninit();
    std::abort();
}
