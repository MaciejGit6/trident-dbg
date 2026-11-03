#include "debugger.hpp"
#include <iostream>
#include <unistd.h>
#include <sys/ptrace.h>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: trident <executable>\n"
                  << "       trident --pid <pid>\n";
        return -1;
    }

    if (std::strcmp(argv[1], "--pid") == 0) {
        if (argc < 3) { std::cerr << "usage: trident --pid <pid>\n"; return -1; }

        char* end = nullptr;
        long parsed = std::strtol(argv[2], &end, 10);
        if (*argv[2] == '\0' || *end != '\0' || parsed <= 0) {
            std::cerr << "invalid pid: " << argv[2] << "\n";
            return -1;
        }
        pid_t pid = static_cast<pid_t>(parsed);
        if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) == -1) {
            std::perror("ptrace(PTRACE_ATTACH)");
            return -1;
        }
        // Read the exe path from /proc so symbol loading still works
        std::string exe_link = "/proc/" + std::to_string(pid) + "/exe";
        char exe_path[4096] = {};
        if (readlink(exe_link.c_str(), exe_path, sizeof(exe_path) - 1) == -1) {
            std::perror("readlink /proc/<pid>/exe");
            return -1;
        }
        Debugger dbg(pid, exe_path, /*attached=*/true);
        dbg.run();

    } else {
        pid_t pid = fork();
        if (pid == -1) {
            std::perror("fork");
            return -1;
        }
        if (pid == 0) {
            if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
                std::perror("ptrace(PTRACE_TRACEME)");
                _exit(127);
            }
            execl(argv[1], argv[1], nullptr);
            std::perror("execl");   // only reached if exec failed
            _exit(127);
        } else {
            Debugger dbg(pid, argv[1], /*attached=*/false);
            dbg.run();
        }
    }
    return 0;
}