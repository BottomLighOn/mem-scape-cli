#ifndef DEBUGGER_H
#define DEBUGGER_H
#include <iostream>
#include <Windows.h>
#include <thread>
#include <atomic>

class core_debugger
{
    DWORD target_pid;
    HANDLE target_handle;
    DEBUG_EVENT current_debug_event;
    std::thread debugger_thread;
    std::atomic<bool> is_thread_running;
public:
    core_debugger() : target_pid(0), current_debug_event({}) {};
    static core_debugger* instance() {
        static core_debugger singleton;
        return &singleton;
    }
    bool enable_debug_privelege();
    bool attach(DWORD pid, HANDLE handle);
    bool detach();
    DEBUG_EVENT get_current_debug_event();
    void handler();
    void run_handler();
    void stop_handler();

    bool is_target_process_running();
};
#endif // !DEBUGGER_H
