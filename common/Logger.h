#ifndef LLFC_LOGGER_H
#define LLFC_LOGGER_H

// ============================================================
// 统一日志（服务端 + Qt 客户端共用）
//
// 用法：
//   xulei::Logger::Init("chatserver1", "logs", "info");  // 程序启动时调用一次
//   LLOG_INFO << "server started on port " << port;
//   LLOG_WARN << "redis connect failed: " << err;
//   LLOG_ERROR << "sql exception: " << e.what();
//   LLOG_DEBUG << "recv msg id " << id;
//
// 特点：
//   - 同时输出到控制台和文件（logs/<名字>.log）
//   - 滚动日志：单文件 5MB，保留 3 个备份
//   - 线程安全；每条都 flush，程序崩溃也不丢最后几条
//   - 宏后面直接接 <<，和原来 cout/qDebug 的写法几乎一样
//   - 日志格式：[时间] [级别] [线程id] [文件:行号] 内容
// ============================================================

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef QT_CORE_LIB
// Qt 客户端里 qDebug 常直接打印这些类型，这里补上流输出，避免编译报错
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#endif

namespace xulei {

class Logger {
public:
    // name: 日志文件名（服务名/客户端名）
    // dir:  日志目录，相对路径按程序当前工作目录算
    // level: trace/debug/info/warn/error/critical/off
    static void Init(const std::string& name,
                     const std::string& dir = "logs",
                     const std::string& level = "info") {
        // 配置里没写 [Log] 时传进来的是空串，这里回退到默认值
        const std::string use_dir = dir.empty() ? "logs" : dir;
        const std::string use_level = level.empty() ? "info" : level;
        try {
            std::filesystem::create_directories(use_dir);

            std::vector<spdlog::sink_ptr> sinks;

            auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
            sinks.push_back(console);

            auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                use_dir + "/" + SafeFileName(name) + ".log",
                5 * 1024 * 1024, 3);
            file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
            sinks.push_back(file);

            auto logger = std::make_shared<spdlog::logger>(
                name, sinks.begin(), sinks.end());
            logger->set_level(ParseLevel(use_level));
            // 告警/错误日志立即落盘，避免崩溃丢现场；普通 INFO/DEBUG 每秒批量刷盘，避免高并发同步 fflush 瓶颈
            logger->flush_on(spdlog::level::warn);
            spdlog::flush_every(std::chrono::seconds(1));
            spdlog::set_default_logger(logger);

            // 日志库内部错误（比如磁盘满）不要抛异常打断业务
            spdlog::set_error_handler([](const std::string& msg) {
                std::fprintf(stderr, "[logger] %s\n", msg.c_str());
            });
        }
        catch (const std::exception& e) {
            // 初始化失败也不能影响程序启动，退回控制台默认日志
            std::fprintf(stderr, "[logger] init failed: %s\n", e.what());
        }
    }

    static void SetLevel(const std::string& level) {
        if (auto logger = spdlog::default_logger()) {
            logger->set_level(ParseLevel(level));
        }
    }

    static spdlog::level::level_enum ParseLevel(const std::string& text) {
        std::string s = text;
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (s == "trace") return spdlog::level::trace;
        if (s == "debug") return spdlog::level::debug;
        if (s == "warn" || s == "warning") return spdlog::level::warn;
        if (s == "error") return spdlog::level::err;
        if (s == "critical") return spdlog::level::critical;
        if (s == "off") return spdlog::level::off;
        return spdlog::level::info;
    }

private:
    // 防止名字里带路径分隔符，把日志写到意料之外的位置
    static std::string SafeFileName(const std::string& name) {
        std::string out = name;
        for (char& c : out) {
            if (c == '/' || c == '\\' || c == ':') {
                c = '_';
            }
        }
        if (out.empty()) {
            out = "app";
        }
        return out;
    }
};

// 一条日志的临时对象：析构时一次性写出去，所以可以像 cout 一样随意 <<
class LogLine {
public:
    LogLine(spdlog::level::level_enum level, const char* file, int line)
        : _level(level), _loc{ file, line, nullptr } {
        auto* logger = spdlog::default_logger_raw();
        _enabled = (logger != nullptr && logger->should_log(_level));
    }

    ~LogLine() {
        if (!_enabled) {
            return;
        }
        auto* logger = spdlog::default_logger_raw();
        if (!logger || !logger->should_log(_level)) {
            return;
        }
        std::string msg = _stream.str();
        // 兼容原来语句末尾的 std::endl：去掉多余换行，日志库会自己加
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
            msg.pop_back();
        }
        try {
            logger->log(_loc, _level, msg);
        }
        catch (...) {
            // 日志失败不影响业务
        }
    }

    template <typename T>
    LogLine& operator<<(const T& value) {
        if (!_enabled) {
            return *this;
        }
        _stream << value;
        return *this;
    }

    // 支持 std::endl / Qt::endl 这类函数指针
    LogLine& operator<<(std::ostream& (*fn)(std::ostream&)) {
        if (!_enabled) {
            return *this;
        }
        if (fn) {
            fn(_stream);
        }
        return *this;
    }

#ifdef QT_CORE_LIB
    LogLine& operator<<(const QString& value) {
        if (!_enabled) return *this;
        _stream << value.toStdString();
        return *this;
    }
    LogLine& operator<<(const QByteArray& value) {
        if (!_enabled) return *this;
        _stream << value.constData();
        return *this;
    }
    LogLine& operator<<(const QSize& value) {
        if (!_enabled) return *this;
        _stream << value.width() << "x" << value.height();
        return *this;
    }
    LogLine& operator<<(const QPoint& value) {
        if (!_enabled) return *this;
        _stream << "(" << value.x() << "," << value.y() << ")";
        return *this;
    }
    LogLine& operator<<(const QRect& value) {
        if (!_enabled) return *this;
        _stream << "(" << value.x() << "," << value.y() << " "
                << value.width() << "x" << value.height() << ")";
        return *this;
    }
    LogLine& operator<<(const QJsonObject& value) {
        if (!_enabled) return *this;
        _stream << QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString();
        return *this;
    }
    LogLine& operator<<(const QJsonArray& value) {
        if (!_enabled) return *this;
        _stream << QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString();
        return *this;
    }
    LogLine& operator<<(const QJsonValue& value) {
        if (!_enabled) return *this;
        if (value.isObject()) {
            return *this << value.toObject();
        }
        if (value.isArray()) {
            return *this << value.toArray();
        }
        if (value.isString()) {
            _stream << value.toString().toStdString();
        } else if (value.isBool()) {
            _stream << (value.toBool() ? "true" : "false");
        } else if (value.isDouble()) {
            _stream << value.toDouble();
        } else {
            _stream << "null";
        }
        return *this;
    }
    LogLine& operator<<(const QJsonDocument& value) {
        if (!_enabled) return *this;
        _stream << value.toJson(QJsonDocument::Compact).toStdString();
        return *this;
    }
#endif

private:
    spdlog::level::level_enum _level;
    spdlog::source_loc _loc;
    bool _enabled = false;
    std::ostringstream _stream;
};

} // namespace xulei

#define LLOG_DEBUG ::xulei::LogLine(spdlog::level::debug, __FILE__, __LINE__)
#define LLOG_INFO  ::xulei::LogLine(spdlog::level::info,  __FILE__, __LINE__)
#define LLOG_WARN  ::xulei::LogLine(spdlog::level::warn,  __FILE__, __LINE__)
#define LLOG_ERROR ::xulei::LogLine(spdlog::level::err, __FILE__, __LINE__)

#endif // LLFC_LOGGER_H
