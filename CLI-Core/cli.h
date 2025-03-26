#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <ctime>
#include <lmcons.h>
#include <iomanip>
#include <sstream>
#include "core/core.h"
#include "core/debugger/debugger.h";
#include "core/mapper/mapper.h"
#include "core/scanner/scanner.h"

namespace cli {
    using command_handler = std::function<void(const std::vector<std::string>&)>;
    static std::string help_message = 
R"""(
    +------------------------------------------------------------------------------+
    |                           COMMAND REFERENCE GUIDE                            |
    +------------------------------------------------------------------------------+

    BASIC COMMANDS
    -------------
      help                Display this help message
      clear               Clear the console screen
      status              Show current core status and connections

    PROCESS MANAGEMENT
    -----------------
      attach <PID>        Attach the core to a process with specified PID
      detach [force]      Detach from current process (resets context)
                          Use 'force' option to detach even if busy

    DEBUGGER OPERATIONS
    ------------------
      debugger attach     Attach debugger to current process
      debugger detach     Detach debugger from current process
      debugger status     Show current debugger state

    DRIVER MANAGEMENT
    ---------------
      mapper driver <name>  Set driver to be loaded (must be in "drivers" folder)
                            Example: mapper driver mydriver.sys
                            Default: driver.sys
      mapper load           Load the specified driver into the system
      mapper unload         Unload the currently loaded driver

    SYSTEM COMMANDS
    -------------
      run <command>       Execute system command (via std::system)
                          Note: This command will be deprecated in future versions

    EXAMPLES
    -------
      attach 1234         Attach to process with PID 1234
      debugger attach     Attach debugger to current process
      mapper driver custom_driver.sys
      mapper load         Load custom_driver.sys

    For more information or support, refer to the documentation.
)""";

    class cli_commands
    {
        std::thread cli_thread;
        std::unordered_map<std::string, command_handler> commands;
        std::vector<std::string> tokenize(const std::string& input) {
            std::vector<std::string> tokens;
            std::istringstream stream(input);
            std::string token;
            bool in_quotes = false;
            char quote_char = 0;

            while (stream >> std::noskipws >> token) {
                if ((token.front() == '"' || token.front() == '\'') && !in_quotes) {
                    in_quotes = true;
                    quote_char = token.front();
                    token.erase(0, 1);
                }

                if (in_quotes) {
                    if (token.back() == quote_char) {
                        token.pop_back();
                        in_quotes = false;
                    }
                    tokens.back() += " " + token;
                }
                else {
                    tokens.push_back(token);
                }
            }
            return tokens;
        }
        std::string get_welcome_message() {
            char username[UNLEN + 1];
            DWORD username_len = UNLEN + 1;
            GetUserNameA(username, &username_len);

            time_t now = time(0);
            tm local_time;
            localtime_s(&local_time, &now);

            char time_str[100];
            strftime(time_str, sizeof(time_str), "%H:%M:%S", &local_time);

            char date_str[100];
            strftime(date_str, sizeof(date_str), "%d.%m.%Y", &local_time);

            std::stringstream ss;
            ss << "    Core v1.0.0" << std::endl;
            ss << "    ----------------------------------------------------------------" << std::endl;
            ss << std::endl;

            ss << "    Hello, " << username;

            int padding = 64 - (strlen(username) + 7) - (strlen(date_str) + strlen(time_str) + 3);
            ss << std::string(padding, ' ') << date_str << " | " << time_str << std::endl;

            ss << std::endl;
            ss << "    > Type 'help' for available commands" << std::endl;
            ss << "    > System is ready" << std::endl;
            ss << std::endl;

            return ss.str();
        }
    public:
        cli_commands() {
            commands["help"] = [this] (const std::vector<std::string>& args) -> void { std::cout << help_message << std::endl; };
            commands["clear"] = [this] (const std::vector<std::string>& args) -> void { system("cls"); };
            commands["run"] = [this] (const std::vector<std::string>& args) -> void {
                if (args.empty() || args[0].empty()) {
                    std::cout << "\nInvalid usage!\nrun [system command]\n";
                    return;
                }
                system(args[0].c_str());
                std::cout << "Success." << std::endl;
            };
            commands["status"] = [this] (const std::vector<std::string>& args) -> void {
                switch (core::core::instance()->get_status()) {
                case core::status::idle:
                    std::cout << "IDLE.\n";
                    break;
                case core::status::attached:
                    std::cout << "ATTACHED.\n";
                    break;
                default:
                    break;
                }
            };
            commands["attach"] = [this] (const std::vector<std::string>& args) -> void {
                if (args.empty() || args[0].empty()) {
                    std::cout << "\nInvalid usage!\nattach [PID]\n";
                    return;
                }

                int pid = std::atoi(args[0].c_str());
                if (!core::core::instance()->attach(pid)) {
                    std::cout << "Failed.\n";
                    return;
                }

                std::cout << "Success.\n";
            };
            commands["detach"] = [this] (const std::vector<std::string>& args) -> void {
                if (args.empty() || args[0].empty()) {
                    scanner::instance()->reset();
                    if (!core::core::instance()->detach()) {
                        std::cout << "Failed to Detach target.\nYou can try to force detach with: detach [force]\n";
                        return;
                    }
                    std::cout << "Success.\n";
                    return;
                }
                else {
                    if (!_stricmp(args[0].c_str(), "force")) {
                        core::core::instance()->force_detach();
                    }
                    else {
                        std::cout << "\nInvalid usage!\ndetach [force]\n";
                    }
                }
            };
            //commands["info"] = [this] (const std::vector<std::string>& args) -> void {};
            /*commands["write_access"] = [this] (const std::vector<std::string>& args) -> void { 
            };*/
            commands["debugger"] = [this] (const std::vector<std::string>& args) -> void {
                if (args.empty() || args[0].empty()) {
                    std::cout << "Ivalid usage.\nCheck [help]\n";
                    return;
                }

                if (args[0] == "attach") {
                    if (!core_debugger::instance()->attach(core::core::instance()->get_pid(), core::core::instance()->get_handle())) {
                        std::cout << "Failed.\n";
                        return;
                    }
                    std::cout << "Success.\n";
                }

                if (args[0] == "detach") {
                    if (!core_debugger::instance()->detach()) {
                        std::cout << "Failed.\n";
                        return;
                    }
                    std::cout << "Success.\n";
                    return;
                }

                std::cout << "Ivalid usage.\nCheck [help]\n";
            };
            commands["mapper"] = [this] (const std::vector<std::string>& args) -> void {
                if (args.empty() || args[0].empty()) {
                    std::cout << "Ivalid usage.\nCheck [help]\n";
                    return;
                }

                if (args[0] == "load") {
                    if (!mapper::instance()->mmap()) {
                        std::cout << "Failed to mmap driver.\n";
                        return;
                    }
                    std::cout << "Success.\n";
                    return;
                }

                if (args[0] == "driver") {
                    if (args.size() < 2) {
                        std::cout << "Ivalid usage.\nCheck [help]\n";
                        return;
                    }
                    if (args[1].empty()) {
                        std::cout << "Ivalid usage.\nDriver name shoud be defined.\n";
                        return;
                    }

                    std::wstring name_wstr = std::wstring(args[1].begin(), args[1].end());
                    mapper::instance()->set_driver_name(name_wstr);
                    std::cout << "Success.\n";
                    return;
                }
                std::cout << "Ivalid usage.\nCheck [help]\n";
            };
            commands["scan"] = [this] (const std::vector<std::string>& args) -> void {
                auto core = core::core::instance();
                auto scanner = scanner::instance();

                if (!args.size()) {
                    scanner->setup(core->get_pid(), core->get_handle());
                    scanner->scan_regions();
                    scanner->print_regions();
                    return;
                }

                if (args.size() == 3) {
                    if (args[0] == "search" && args[1] == "int") {
                        int value = atoi(args[2].c_str());
                        scanner->setup(core->get_pid(), core->get_handle());
                        scanner->scan_regions();
                        scanner->search(value);
                        scanner->print_scanned_ints();
                        return;
                    }

                    if (args[0] == "filter" && args[1] == "int") {
                        int value = atoi(args[2].c_str());
                        scanner->setup(core->get_pid(), core->get_handle());
                        scanner->filter_int(value);
                        scanner->print_scanned_ints();
                        return;
                    }
                }

               
                std::cout << "Invalid usage!\nCheck [help]\n";
                for (auto& a : args) {
                    std::cout << a.c_str() << std::endl;
                }
                return;
            };
        }

        void loop() {
            std::string input;
            std::cout << get_welcome_message() << std::endl;
            while (true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                std::cout << "> ";
                if (!std::getline(std::cin, input)) break;

                auto tokens = tokenize(input);
                if (tokens.empty()) {
                    continue;
                }

                auto iterator = commands.find(tokens[0]);
                if (iterator != commands.end()) {
                    iterator->second({tokens.begin() + 1, tokens.end()});
                }
                else {
                    std::cout << "Unknown command\n";
                }
            }
        }

        void run() {
            cli_thread = std::thread(&cli::cli_commands::loop, this);
            cli_thread.detach();
        }
    };
}
#endif // !CLI_COMMANDS_H
