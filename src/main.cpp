#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <csignal>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <cstdint> 

class Breakpoint {
public:
    Breakpoint() = default;
    Breakpoint(pid_t pid, std::intptr_t addr) 
        : m_pid{pid}, m_addr{addr}, m_enabled{false}, m_saved_data{0} {}

    void enable() {
        auto data = ptrace(PTRACE_PEEKDATA, m_pid, m_addr, nullptr);
        m_saved_data = static_cast<uint8_t>(data & 0xFF);
        uint64_t data_with_int3 = ((data & ~0xFF) | 0xCC); // inject trap
        ptrace(PTRACE_POKEDATA, m_pid, m_addr, data_with_int3);
        m_enabled = true;
    }

    void disable() {
        auto data = ptrace(PTRACE_PEEKDATA, m_pid, m_addr, nullptr);
        uint64_t restored_data = ((data & ~0xFF) | m_saved_data);
        ptrace(PTRACE_POKEDATA, m_pid, m_addr, restored_data);
        m_enabled = false;
    }

    bool is_enabled() const { return m_enabled; }

private:
    pid_t m_pid;
    std::intptr_t m_addr;
    bool m_enabled;
    uint8_t m_saved_data;
};


class Debugger {
public:
    Debugger(pid_t pid) : m_pid{pid} {}

    void run() {
        int wait_status;
        waitpid(m_pid, &wait_status, 0);
        std::cout << "Attached to process " << m_pid << "\n";

        signal(SIGINT, SIG_IGN);

        std::string line;
        while (true) {
            std::cout << "trident> ";
            if (!std::getline(std::cin, line)) break;
            
            auto args = split_input(line);
            if (args.empty()) continue;

            std::string cmd = args[0];

            if (cmd == "continue" || cmd == "c") {
                ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);
                waitpid(m_pid, &wait_status, 0);
                
                if (WIFSTOPPED(wait_status)) {
                    std::cout << "Stopped. Signal: " << WSTOPSIG(wait_status) << "\n";
                    dump_registers(); // Show where we stopped
                } else if (WIFEXITED(wait_status)) {
                    break;
                }
            } 
            // NEW: The break command!
            else if (cmd == "break" || cmd == "b") {
                if (args.size() < 2) continue;
                std::intptr_t addr = std::stol(args[1], 0, 16);
                
                Breakpoint bp(m_pid, addr);
                bp.enable();
                m_breakpoints[addr] = bp; // Save it in our private map
                
                std::cout << "Breakpoint set at 0x" << std::hex << addr << std::dec << "\n";
            }
            // NEW: Command to read registers
            else if (cmd == "regs") {
                dump_registers();
            }
            else if (cmd == "quit" || cmd == "exit") {
                kill(m_pid, SIGKILL);
                break;
            }
        }
    }

private:
    pid_t m_pid;
    std::map<std::intptr_t, Breakpoint> m_breakpoints; // Safe from the outside world

    // We pulled your split_input function in here
    std::vector<std::string> split_input(const std::string& s) {
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ' ')) {
            if (!item.empty()) out.push_back(item);
        }
        return out;
    }

    // A helper to show CPU state
    void dump_registers() {
        user_regs_struct regs;
        ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs);
        std::cout << std::hex << "  RIP: 0x" << regs.rip << "  RAX: 0x" << regs.rax << std::dec << "\n";
    }
};



int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "no exec to debug!\n";
        return -1;
    }
    pid_t pid = fork();
    if (pid == 0) {
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        execl(argv[1], argv[1], nullptr);
    } else {
        Debugger dbg(pid);
        dbg.run();
    }
    return 0;
}