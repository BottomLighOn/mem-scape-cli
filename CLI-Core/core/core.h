#ifndef CORE_H
#define CORE_H
#include <Windows.h>

namespace core {
    enum class status : char { idle, attached };
    class core
    {
        DWORD attached_pid;
        HANDLE attached_handle;
        status current_status;
        core() {
            attached_pid = 0;
            attached_handle = 0;
            current_status = status::idle;
        }
    public:
        static core* instance() {
            static core singleton = core();
            return &singleton;
        }

        bool attach(DWORD pid);
        bool detach();
        void force_detach();
        void set_hardware_breakpoint(HANDLE handle, void* address);
        void check_handle_status();
        status get_status();
        DWORD get_pid();
        HANDLE get_handle();
    };
}
#endif // !CORE_H
