#include "debugger.hpp"
#include <iostream>
#include <unistd.h>
#include <sys/ptrace.h>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: trident <executable>\n"
                  << "       trident --pid <pid>\n";
        return -1;
    }

    if (std::strcmp(argv[1], "--pid") == 0) {
        if (argc < 3) { std::cerr << "usage: trident --pid <pid>\n"; return -1; }

        pid_t pid = static_cast<pid_t>(std::stoi(argv[2]));
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
        Debugger dbg(pid, exe_path);
        dbg.run();

    } else {
        pid_t pid = fork();
        if (pid == 0) {
            ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
            execl(argv[1], argv[1], nullptr);
        } else {
            Debugger dbg(pid, argv[1]);
            dbg.run();
        }
    }
    return 0;
}