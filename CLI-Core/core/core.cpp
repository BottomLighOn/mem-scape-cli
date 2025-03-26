#include "core.h"
#include <iostream>

bool core::core::attach(DWORD pid) {
    if ((attached_handle != 0 && attached_pid != 0) || (current_status == status::attached)) {
        std::cout << "Core was attached to other target. Detaching.\n";
        if (!detach()) {
            std::cout << "Unable to Detach. Attaching will not be proceed.\n";
            return false;
        }
    }

    HANDLE process_handle = OpenProcess(PROCESS_ALL_ACCESS, false, pid);
    if (process_handle == INVALID_HANDLE_VALUE || process_handle == 0) {
        int last_error = GetLastError();
        if (last_error == ERROR_NOT_FOUND) {
            std::cout << "Invalid PID, or process is protected from attaching\nError: " << GetLastError() << std::endl;
            return false;
        }
        std::cout << "Core is not running as admin, or process is protected from attaching\nError: " << GetLastError() << std::endl;
        return false;
    }

    attached_pid = pid;
    attached_handle = process_handle;
    current_status = status::attached;
    return true;
}

bool core::core::detach() {
    auto is_handle_closed = CloseHandle(attached_handle);
    if (!is_handle_closed) {
        std::cout << "Failed to close handle. Maybe handle closed?\nError: " << GetLastError() << std::endl;
        return false;
    }
    attached_handle = 0;
    attached_pid = 0;
    current_status = status::idle;
    return true;
}

void core::core::force_detach() {
    CloseHandle(attached_handle);
    attached_handle = 0;
    attached_pid = 0;
    current_status = status::idle;
    std::cout << "Success.\n";
}

void core::core::set_hardware_breakpoint(HANDLE handle, void* address) {
    CONTEXT ctx = {0};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(handle, &ctx)) {
        ctx.Dr0 = (DWORD_PTR)address;
        ctx.Dr7 |= (1 << 0);
        ctx.Dr7 |= (1 << 16); // RW
        ctx.Dr7 |= (0b00 << 18);
        SetThreadContext(handle, &ctx);
    }
}

void core::core::check_handle_status() {
    if (attached_handle == NULL) {
        return;
    }

    DWORD exit_code = 0;
    if (GetExitCodeProcess(attached_handle, &exit_code)) {
        if (exit_code != STILL_ACTIVE) {
            attached_handle = 0;
            attached_pid = 0;
            current_status = status::idle;
            std::cout << "Target closed. Detaching.\n";
        }
    }
    else {
        DWORD error = GetLastError();
        std::cout << "Failed to check process. Error: " << error << std::endl;
    }
}

core::status core::core::get_status() {
    check_handle_status();
    return current_status;
}

DWORD core::core::get_pid() {
    check_handle_status();
    return attached_pid;
}

HANDLE core::core::get_handle() {
    check_handle_status();
    return attached_handle;
}