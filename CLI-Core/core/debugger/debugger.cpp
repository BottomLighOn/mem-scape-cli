#define _CRT_SECURE_NO_WARNINGS
#include "debugger.h"
#include "output_pipe/output_pipe.h"

bool core_debugger::enable_debug_privelege() {
    HANDLE token;
    TOKEN_PRIVILEGES tkp;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        std::cerr << "OpenProcessToken failed, error: " << GetLastError() << std::endl;
        return false;
    }

    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tkp.Privileges[0].Luid)) {
        std::cerr << "LookupPrivilegeValue failed, error: " << GetLastError() << std::endl;
        return false;
    }

    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(token, FALSE, &tkp, sizeof(tkp), NULL, NULL)) {
        std::cerr << "AdjustTokenPrivileges failed, error: " << GetLastError() << std::endl;
        return false;
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        std::cerr << "The required privilege is not held by the client." << std::endl;
        return false;
    }

    return true;
}

bool core_debugger::attach(DWORD pid, HANDLE handle) {
    if (!enable_debug_privelege()) {
        std::cout << "Failed to adjust debug priveleges\n";
        return false;
    }

    if (!pid || !handle) {
        std::cout << "Failed to attach debugger\nProcess is not attached.\n";
        return false;
    }
   
    std::cout << "[debugger] Attaching to PID: " << pid << " Handle: " << handle << std::endl;

    target_pid = pid;
    target_handle = handle;
    run_handler();
    return true;
}

bool core_debugger::detach() {
    if (target_pid == 0) {
        std::cout << "Debugger is not attached.\n";
        return false;
    }

    stop_handler();
    return true;
}

DEBUG_EVENT core_debugger::get_current_debug_event() {
    return current_debug_event;
}

void core_debugger::handler() {
    debug::start();
    debug::print("[debugger] Handler started.\n");

    if (target_pid == 0) {
        debug::print_fmt("[debugger] Invalid target PID: %d", target_pid);
        return;
    }

    if (!is_target_process_running()) {
        debug::print_fmt("[debugger] Process %d does not exist", target_pid);
        return;
    }

    debug::print_fmt("[debugger] Attaching to process %d", target_pid);

    bool is_attached = false;

    if (!DebugActiveProcess(target_pid)) {
        DWORD error = GetLastError();
        debug::print_fmt("[debugger] Failed to attach debugger. Error: %d\n", error);
        return;
    }

    is_attached = true;
    debug::print_fmt("[debugger] Successfully attached to process %d", target_pid);

    while (true) {
        if (!is_thread_running) {
            debug::print("[debugger] Thread stop requested");
            break;
        }

        if (WaitForDebugEvent(&current_debug_event, 100)) {
            DWORD event_code = current_debug_event.dwDebugEventCode;
            DWORD process_id = current_debug_event.dwProcessId;
            DWORD thread_id = current_debug_event.dwThreadId;

            debug::print_fmt("[debugger] Caught Event: %d from process %d, thread %d",
                             event_code, process_id, thread_id);

            switch (event_code) {
            case CREATE_PROCESS_DEBUG_EVENT:
                debug::print("[debugger] Process created");
                // Закрываем дескриптор, чтобы избежать утечки ресурсов
                if (current_debug_event.u.CreateProcessInfo.hFile != NULL) {
                    CloseHandle(current_debug_event.u.CreateProcessInfo.hFile);
                }
                break;

            case CREATE_THREAD_DEBUG_EVENT:
                debug::print("[debugger] Thread created");
                break;

            case EXIT_THREAD_DEBUG_EVENT:
                debug::print("[debugger] Thread exited");
                break;

            case EXIT_PROCESS_DEBUG_EVENT:
                debug::print("[debugger] Process exited");
                if (process_id == target_pid) {
                    is_attached = false;
                    debug::print("[debugger] Target process has exited");
                }
                break;

            case LOAD_DLL_DEBUG_EVENT:
                debug::print("[debugger] DLL loaded");
                // Закрываем дескриптор, чтобы избежать утечки ресурсов
                if (current_debug_event.u.LoadDll.hFile != NULL) {
                    CloseHandle(current_debug_event.u.LoadDll.hFile);
                }
                break;

            case UNLOAD_DLL_DEBUG_EVENT:
                debug::print("[debugger] DLL unloaded");
                break;

            case OUTPUT_DEBUG_STRING_EVENT:
                debug::print("[debugger] Debug string output");
                break;

            case EXCEPTION_DEBUG_EVENT:
            {
                DWORD exception_code_ = current_debug_event.u.Exception.ExceptionRecord.ExceptionCode;
                DWORD64 exception_address = (DWORD64)current_debug_event.u.Exception.ExceptionRecord.ExceptionAddress;
                bool first_chance = current_debug_event.u.Exception.dwFirstChance != 0;

                debug::print_fmt("[debugger] Exception caught: 0x%X at address 0x%llX (first chance: %s)",
                                 exception_code_, exception_address, first_chance ? "yes" : "no");

                 // Определяем тип исключения
                DWORD continue_status = DBG_EXCEPTION_NOT_HANDLED;

                switch (exception_code_) {
                case EXCEPTION_BREAKPOINT:
                    debug::print("[debugger] Breakpoint exception");
                    continue_status = DBG_CONTINUE;
                    break;

                case EXCEPTION_SINGLE_STEP:
                    debug::print("[debugger] Single step exception");
                    continue_status = DBG_CONTINUE;
                    break;

                case EXCEPTION_ACCESS_VIOLATION:
                    debug::print_fmt("[debugger] Access violation at 0x%llX", exception_address);
                    continue_status = first_chance ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;
                    break;

                case EXCEPTION_DATATYPE_MISALIGNMENT:
                    debug::print("[debugger] Datatype misalignment");
                    continue_status = first_chance ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;
                    break;

                case EXCEPTION_ILLEGAL_INSTRUCTION:
                    debug::print("[debugger] Illegal instruction");
                    continue_status = first_chance ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;
                    break;

                case EXCEPTION_STACK_OVERFLOW:
                    debug::print("[debugger] Stack overflow");
                    continue_status = first_chance ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;
                    break;

                default:
                    debug::print_fmt("[debugger] Unhandled exception: 0x%X", exception_code_);
                    continue_status = first_chance ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;
                    break;
                }

                // Продолжаем выполнение с определенным статусом
                if (!ContinueDebugEvent(current_debug_event.dwProcessId,
                    current_debug_event.dwThreadId,
                    continue_status)) {
                    DWORD error = GetLastError();
                    debug::print_fmt("[debugger] Failed to continue debug event: %d", error);
                }

                // Важно: не вызываем ContinueDebugEvent повторно в общем блоке кода
                continue;  // Переходим к следующей итерации цикла
            }
            break;

            default:
                debug::print_fmt("[debugger] Unknown event: %d", event_code);
                break;
            }

            if (process_id == target_pid || process_id == 0) {
                if (!ContinueDebugEvent(process_id, thread_id, DBG_CONTINUE)) {
                    DWORD error = GetLastError();
                    debug::print_fmt("[debugger] Failed to continue debug event: %d", error);
                }
            }
            else {
                debug::print_fmt("[debugger] Ignoring event from unknown process: %d", process_id);
            }
        }
        else {
            DWORD error = GetLastError();
            if (error != ERROR_SEM_TIMEOUT) {
                debug::print_fmt("[debugger] WaitForDebugEvent failed: %d", error);
                break;
            }
        }
    }

    if (is_attached) {
        debug::print_fmt("[debugger] Attempting to detach from process %d...", target_pid);

        if (!is_target_process_running()) {
            DWORD error = GetLastError();
            debug::print_fmt("[debugger] Process %d no longer exists (error: %d)", target_pid, error);
            is_attached = false;
        }
        else {
            if (!DebugActiveProcessStop(target_pid)) {
                DWORD error = GetLastError();
                debug::print_fmt("[debugger] Failed to detach debugger. Error: %d\n", error);
            }
            else {
                debug::print("[debugger] Successfully detached debugger");
            }
        }
    }
    else {
        debug::print("[debugger] No need to detach, process already exited");
    }

    debug::print("[debugger] Handler stopped.");
    debug::stop();
    target_pid = 0;
    target_handle = 0;
}

void core_debugger::run_handler() {
    is_thread_running = true;
    debugger_thread = std::thread(&core_debugger::handler, this);
    debugger_thread.detach();
}

void core_debugger::stop_handler() {
    is_thread_running = false;
}

bool core_debugger::is_target_process_running() {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, target_pid);
    if (hProcess == NULL) {
        debug::print_fmt("[debugger] Process %d does not exist, disabling debugger.", target_pid);
        CloseHandle(hProcess);
        return false;
    }
    CloseHandle(hProcess);
    return true;
}