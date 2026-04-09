#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <csignal>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

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