#pragma once

#include "Logger.hxx"
#include "HostContext.hxx"
#include <string>

#ifdef GE_PLUGIN
#define LOG_TRACE(cat, msg) do { \
    HostServices* _hs = GetHostServices(); \
    if (_hs && _hs->Log) { \
        std::string _logMsg = (msg); \
        _hs->Log(HostLogLevel::Trace, cat, _logMsg.c_str()); \
    } \
} while(0)
#define LOG_DEBUG(cat, msg) do { \
    HostServices* _hs = GetHostServices(); \
    if (_hs && _hs->Log) { \
        std::string _logMsg = (msg); \
        _hs->Log(HostLogLevel::Debug, cat, _logMsg.c_str()); \
    } \
} while(0)
#define LOG_INFO(cat, msg) do { \
    HostServices* _hs = GetHostServices(); \
    if (_hs && _hs->Log) { \
        std::string _logMsg = (msg); \
        _hs->Log(HostLogLevel::Info, cat, _logMsg.c_str()); \
    } \
} while(0)
#define LOG_WARN(cat, msg) do { \
    HostServices* _hs = GetHostServices(); \
    if (_hs && _hs->Log) { \
        std::string _logMsg = (msg); \
        _hs->Log(HostLogLevel::Warn, cat, _logMsg.c_str()); \
    } \
} while(0)
#define LOG_ERROR(cat, msg) do { \
    HostServices* _hs = GetHostServices(); \
    if (_hs && _hs->Log) { \
        std::string _logMsg = (msg); \
        _hs->Log(HostLogLevel::Error, cat, _logMsg.c_str()); \
    } \
} while(0)
#define LOG_FATAL(cat, msg) do { \
    HostServices* _hs = GetHostServices(); \
    if (_hs && _hs->Log) { \
        std::string _logMsg = (msg); \
        _hs->Log(HostLogLevel::Fatal, cat, _logMsg.c_str()); \
    } \
} while(0)
#else
#define LOG_TRACE(cat, msg) Logger::Log(LogLevel::Trace, cat, msg)
#define LOG_DEBUG(cat, msg) Logger::Log(LogLevel::Debug, cat, msg)
#define LOG_INFO(cat, msg)  Logger::Log(LogLevel::Info,  cat, msg)
#define LOG_WARN(cat, msg)  Logger::Log(LogLevel::Warn,  cat, msg)
#define LOG_ERROR(cat, msg) Logger::Log(LogLevel::Error, cat, msg)
#define LOG_FATAL(cat, msg) Logger::Log(LogLevel::Fatal, cat, msg)
#endif
