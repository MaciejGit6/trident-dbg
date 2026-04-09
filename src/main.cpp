#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdint>
#include <csignal>       // For signal handling
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>    // For user_regs_struct

class Breakpoint {
public:
    pid_t pid;
    std::intptr_t addr;
    bool enabled;
    uint8_t saved_data;

    Breakpoint(pid_t pid, std::intptr_t addr) 
        : pid{pid}, addr{addr}, enabled{false}, saved_data{0} {}

    void enable() {
        long data = ptrace(PTRACE_PEEKDATA, pid, addr, nullptr);
        saved_data = static_cast<uint8_t>(data & 0xFF);
        uint64_t data_with_int3 = ((data & ~0xFF) | 0xCC);
        ptrace(PTRACE_POKEDATA, pid, addr, data_with_int3);
        enabled = true;
    }
};

std::vector<std::string> split_input(const std::string& s, char delimiter) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

void dump_registers(pid_t child_pid) {
    user_regs_struct regs;
    ptrace(PTRACE_GETREGS, child_pid, nullptr, &regs);
    std::cout << std::hex << "  RIP: 0x" << regs.rip << "  RAX: 0x" << regs.rax << std::dec << "\n";
}

void execute_debugger(pid_t child_pid) {
    int wait_status;
    waitpid(child_pid, &wait_status, 0);
    std::cout << "Attached to process " << child_pid << "\n";


    signal(SIGINT, SIG_IGN);

    std::string line;
    while (true) {
        std::cout << "trident> ";
        if (!std::getline(std::cin, line)) break;
        
        auto args = split_input(line, ' ');
        if (args.empty()) continue;
        std::string command = args[0];

        if (command == "continue" || command == "c") {
            ptrace(PTRACE_CONT, child_pid, nullptr, nullptr);
            waitpid(child_pid, &wait_status, 0);

            if (WIFSTOPPED(wait_status)) {
                int sig = WSTOPSIG(wait_status);
                if (sig == SIGINT) {
                    std::cout << "\n[INTERRUPT] Child paused by user signal.\n";
                } else if (sig == SIGTRAP) {
                    std::cout << "\n[TRAP] Child hit a breakpoint.\n";
                }
                dump_registers(child_pid);
            } else if (WIFEXITED(wait_status)) {
                std::cout << "Child exited normally.\n";
                break;
            }
        } 
        else if (command == "break" || command == "b") {
            if (args.size() < 2) continue;
            std::intptr_t addr = std::stol(args[1], 0, 16);
            Breakpoint bp(child_pid, addr);
            bp.enable();
            std::cout << "Breakpoint set at 0x" << std::hex << addr << std::dec << "\n";
        }
        else if (command == "exit" || command == "quit") {
            kill(child_pid, SIGKILL);
            break;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <program_to_debug>\n";
        return -1;
    }

    pid_t child_pid = fork();
    if (child_pid == 0) {
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        execl(argv[1], argv[1], nullptr);
    } else {
        execute_debugger(child_pid);
    }
    return 0;
}