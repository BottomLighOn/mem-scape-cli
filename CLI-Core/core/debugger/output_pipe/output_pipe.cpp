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
            char buffer[MAX_PATH];
            GetModuleFileNameA(NULL, buffer, MAX_PATH);
            std::string path(buffer);
            size_t pos = path.find_last_of("\\/");
            debugger_path = path.substr(0, pos + 1) + "debugger_console.exe";
        }

        ~DebuggerImpl() {
            disconnect();
        }

        bool start_debugger_console() {
            DWORD file_attr = GetFileAttributesA(debugger_path.c_str());
            if (file_attr == INVALID_FILE_ATTRIBUTES) {
                std::cerr << "Error: Debugger console file not found: " << debugger_path << std::endl;
                return false;
            }

            STARTUPINFOA si = {0};
            PROCESS_INFORMATION pi = {0};
            si.cb = sizeof(STARTUPINFOA);

            if (!CreateProcessA(
                NULL,                           
                const_cast<char*>(debugger_path.c_str()), 
                NULL,                           
                NULL,                           
                FALSE,                          
                CREATE_NEW_CONSOLE,             
                NULL,                          
                NULL,                          
                &si,                            
                &pi                            
                )) {
                std::cerr << "Error launching debugger console: " << GetLastError() << std::endl;
                return false;
            }

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            std::this_thread::sleep_for(std::chrono::seconds(1));

            return true;
        }

        bool connect() {
            std::lock_guard<std::mutex> lock(pipe_mutex);

            if (connected) {
                return true;
            }

            for (int i = 0; i < 10; i++) {
                h_pipe = CreateFileA(
                    PIPE_NAME.c_str(),           
                    GENERIC_WRITE,            
                    0,                           
                    NULL,                        
                    OPEN_EXISTING,             
                    0,                           
                    NULL                         
                );

                if (h_pipe != INVALID_HANDLE_VALUE) {
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

                if (i == 0) {
                    start_debugger_console();
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            std::cerr << "Failed to connect to debugger console." << std::endl;
            return false;
        }

        void disconnect() {

            if (connected && h_pipe != INVALID_HANDLE_VALUE) {
                try {
                    print("EXIT_DEBUGGER");

                    if (h_pipe != INVALID_HANDLE_VALUE) {
                        CloseHandle(h_pipe);
                        h_pipe = INVALID_HANDLE_VALUE;
                    }
                }
                catch (...) {
                    if (h_pipe != INVALID_HANDLE_VALUE) {
                        CloseHandle(h_pipe);
                        h_pipe = INVALID_HANDLE_VALUE;
                    }
                }
            }

            connected = false;
            if (h_pipe != INVALID_HANDLE_VALUE) {
                CloseHandle(h_pipe);
                h_pipe = INVALID_HANDLE_VALUE;
            }
        }

        bool print(const std::string& message) {
            std::lock_guard<std::mutex> lock(pipe_mutex);

            if (!connected) {
                return false;
            }

            DWORD bytes_written;
            if (!WriteFile(
                h_pipe,
                message.c_str(),
                static_cast<DWORD>(message.length() + 1),
                &bytes_written,
                NULL
                )) {

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
