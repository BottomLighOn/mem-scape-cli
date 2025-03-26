#include "output_pipe.h"
#ifdef ERROR
#undef ERROR
#endif // ERROR

namespace debug {

    const std::string PIPE_NAME = "\\\\.\\pipe\\debugger_pipe";

    class DebuggerImpl
    {
    public:
        DebuggerImpl() : h_pipe(INVALID_HANDLE_VALUE), connected(false) {
            // Path to debugger console (should be in the same directory)
            char buffer[MAX_PATH];
            GetModuleFileNameA(NULL, buffer, MAX_PATH);
            std::string path(buffer);
            size_t pos = path.find_last_of("\\/");
            debugger_path = path.substr(0, pos + 1) + "debugger_console.exe";
        }

        ~DebuggerImpl() {
            disconnect();
        }

        // Start the debugger console
        bool start_debugger_console() {
            // Check if executable file exists
            DWORD file_attr = GetFileAttributesA(debugger_path.c_str());
            if (file_attr == INVALID_FILE_ATTRIBUTES) {
                std::cerr << "Error: Debugger console file not found: " << debugger_path << std::endl;
                return false;
            }

            // Launch debugger console
            STARTUPINFOA si = {0};
            PROCESS_INFORMATION pi = {0};
            si.cb = sizeof(STARTUPINFOA);

            if (!CreateProcessA(
                NULL,                           // Module name (NULL = use command line)
                const_cast<char*>(debugger_path.c_str()), // Command line
                NULL,                           // Process security attributes
                NULL,                           // Thread security attributes
                FALSE,                          // Handle inheritance
                CREATE_NEW_CONSOLE,             // Creation flags
                NULL,                           // Environment block
                NULL,                           // Current directory
                &si,                            // Startup info
                &pi                             // Process info
                )) {
                std::cerr << "Error launching debugger console: " << GetLastError() << std::endl;
                return false;
            }

            // Close process and thread handles
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            // Give time for console to start
            std::this_thread::sleep_for(std::chrono::seconds(1));

            return true;
        }

        // Connect to debugger console
        bool connect() {
            std::lock_guard<std::mutex> lock(pipe_mutex);

            if (connected) {
                return true;
            }

            // Try to connect to named pipe
            for (int i = 0; i < 10; i++) {
                h_pipe = CreateFileA(
                    PIPE_NAME.c_str(),           // Pipe name
                    GENERIC_WRITE,               // Access mode (write only)
                    0,                           // Share mode
                    NULL,                        // Security attributes
                    OPEN_EXISTING,               // Open mode
                    0,                           // Flags and attributes
                    NULL                         // Template
                );

                if (h_pipe != INVALID_HANDLE_VALUE) {
                    // Set message mode
                    DWORD mode = PIPE_READMODE_MESSAGE;
                    if (SetNamedPipeHandleState(h_pipe, &mode, NULL, NULL)) {
                        connected = true;
                        return true;
                    }
                    else {
                        CloseHandle(h_pipe);
                        h_pipe = INVALID_HANDLE_VALUE;
                    }
                }

                // If connection failed, try to start debugger console
                if (i == 0) {
                    start_debugger_console();
                }

                // Wait and try again
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            std::cerr << "Failed to connect to debugger console." << std::endl;
            return false;
        }

        // Disconnect from debugger console
        void disconnect() {
            //std::lock_guard<std::mutex> lock(pipe_mutex);

            // Проверяем, что соединение активно и дескриптор пайпа валидный
            if (connected && h_pipe != INVALID_HANDLE_VALUE) {
                try {
                    // Отправляем команду выхода только если соединение активно
                    print("EXIT_DEBUGGER");

                    // Закрываем дескриптор пайпа
                    if (h_pipe != INVALID_HANDLE_VALUE) {
                        CloseHandle(h_pipe);
                        h_pipe = INVALID_HANDLE_VALUE;
                    }
                }
                catch (...) {
                    // Игнорируем любые исключения при отключении
                    // Это предотвратит аварийное завершение
                    if (h_pipe != INVALID_HANDLE_VALUE) {
                        CloseHandle(h_pipe);
                        h_pipe = INVALID_HANDLE_VALUE;
                    }
                }
            }

            // В любом случае сбрасываем флаг подключения
            connected = false;
            if (h_pipe != INVALID_HANDLE_VALUE) {
                CloseHandle(h_pipe);
                h_pipe = INVALID_HANDLE_VALUE;
            }
        }

        // Print message to debugger console
        bool print(const std::string& message) {
            std::lock_guard<std::mutex> lock(pipe_mutex);

            if (!connected) {
                return false;
            }

            DWORD bytes_written;
            if (!WriteFile(
                h_pipe,                         // Pipe handle
                message.c_str(),                // Buffer to write
                static_cast<DWORD>(message.length() + 1), // Number of bytes to write
                &bytes_written,                 // Number of bytes written
                NULL                            // Not using async I/O
                )) {
                    // Write error, debugger console may have been closed
                CloseHandle(h_pipe);
                h_pipe = INVALID_HANDLE_VALUE;
                connected = false;
                return false;
            }

            return true;
        }

    private:
        HANDLE h_pipe;
        bool connected;
        std::mutex pipe_mutex;
        std::string debugger_path;
    };

    Debugger& Debugger::instance() {
        static Debugger instance;
        return instance;
    }

    Debugger::Debugger() : impl_(std::make_unique<DebuggerImpl>()) {}
    Debugger::~Debugger() = default;

    bool Debugger::connect() {
        return impl_->connect();
    }

    void Debugger::disconnect() {
        if (impl_) {
            impl_->disconnect();
        }
    }

    bool Debugger::print(const std::string& message) {
        return impl_ == nullptr ? 0 : impl_->print(message);
    }

    bool Debugger::print(MessageType type, const std::string& message) {
        std::string prefix;
        switch (type) {
        case MessageType::ERROR: prefix = "ERROR: "; break;
        case MessageType::WARNING: prefix = "WARNING: "; break;
        case MessageType::SUCCESS: prefix = "SUCCESS: "; break;
        default: prefix = ""; break;
        }
        return impl_ == nullptr ? 0 : impl_->print(prefix + message);
    }

    bool Debugger::error(const std::string& message) {
        return print(MessageType::ERROR, message);
    }

    bool Debugger::warning(const std::string& message) {
        return print(MessageType::WARNING, message);
    }

    bool Debugger::success(const std::string& message) {
        return print(MessageType::SUCCESS, message);
    }

}
