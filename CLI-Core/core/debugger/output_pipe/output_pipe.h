#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <mutex>
#include <iostream>
#include <memory>
#include <functional>

#ifdef ERROR
#undef ERROR
#endif // ERROR

namespace debug {

    enum class MessageType
    {
        INFO,
        ERROR,
        WARNING,
        SUCCESS
    };

    class DebuggerImpl;

    class Debugger
    {
    private:
        std::unique_ptr<DebuggerImpl> impl_;

    public:
        Debugger();
        ~Debugger();

        Debugger(const Debugger&) = delete;
        Debugger& operator=(const Debugger&) = delete;

        bool connect();
        void disconnect();

        bool print(const std::string& message);
        bool print(MessageType type, const std::string& message);

        bool error(const std::string& message);
        bool warning(const std::string& message);
        bool success(const std::string& message);

        static Debugger& instance();
    };
    template<typename... Args>
    std::string format_string(const std::string& format, Args&&... args) {
        int size = snprintf(nullptr, 0, format.c_str(), std::forward<Args>(args)...);
        if (size <= 0) {
            return "Error in formatting";
        }

        std::string buffer(size + 1, '\0');

        snprintf(&buffer[0], size + 1, format.c_str(), std::forward<Args>(args)...);

        buffer.pop_back();

        return buffer;
    }

    template<typename... Args>
    inline void print_fmt(const std::string& format, Args&&... args) {
        Debugger::instance().print(format_string(format, std::forward<Args>(args)...));
    }

    template<typename... Args>
    inline void print_fmt(MessageType type, const std::string& format, Args&&... args) {
        Debugger::instance().print(type, format_string(format, std::forward<Args>(args)...));
    }

    template<typename... Args>
    inline void error_fmt(const std::string& format, Args&&... args) {
        Debugger::instance().error(format_string(format, std::forward<Args>(args)...));
    }

    template<typename... Args>
    inline void warning_fmt(const std::string& format, Args&&... args) {
        Debugger::instance().warning(format_string(format, std::forward<Args>(args)...));
    }

    template<typename... Args>
    inline void success_fmt(const std::string& format, Args&&... args) {
        Debugger::instance().success(format_string(format, std::forward<Args>(args)...));
    }

    inline void print(const std::string& message) {
        Debugger::instance().print(message);
    }

    inline void print(MessageType type, const std::string& message) {
        Debugger::instance().print(type, message);
    }

    inline void error(const std::string& message) {
        Debugger::instance().error(message);
    }

    inline void warning(const std::string& message) {
        Debugger::instance().warning(message);
    }

    inline void success(const std::string& message) {
        Debugger::instance().success(message);
    }

    inline bool start() {
        return Debugger::instance().connect();
    }

    inline void stop() {
        try {
            Debugger::instance().disconnect();
        }
        catch (...) {
        }
    }

    class ScopedDebugger
    {
    public:
        ScopedDebugger(const std::string& scope_name) : scope_name_(scope_name) {
            Debugger::instance().print("ENTER: " + scope_name_);
        }

        ~ScopedDebugger() {
            Debugger::instance().print("EXIT: " + scope_name_);
        }

    private:
        std::string scope_name_;
    };

}
#ifndef ERROR
#define ERROR 0
#endif // !ERROR
