#include "debugger.hpp"
#include <iostream>
#include <unistd.h>
#include <sys/ptrace.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: trident <executable>\n";
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