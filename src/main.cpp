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

std::vector<std::string> split_input(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ' ')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

void execute_debugger(pid_t child_pid) {
    int wait_status;
    waitpid(child_pid, &wait_status, 0);
    std::cout << "Attached to process " << child_pid << "\n";

   
    signal(SIGINT, SIG_IGN);

    //test
    std::string line;
    while (true) {
        std::cout << "trident> ";
        if (!std::getline(std::cin, line)) break;
        auto args = split_input(line);
        if (args.empty()) continue;

        if (args[0] == "continue" || args[0] == "c") {
            ptrace(PTRACE_CONT, child_pid, nullptr, nullptr);
            waitpid(child_pid, &wait_status, 0);
            if (WIFEXITED(wait_status)) break;
        } else if (args[0] == "quit") {
            kill(child_pid, SIGKILL);
            break;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        execl(argv[1], argv[1], nullptr);
    } else {
        execute_debugger(pid);
    }
    return 0;
}