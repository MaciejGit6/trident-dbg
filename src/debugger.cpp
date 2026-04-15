#include "debugger.hpp"
#include <iostream>
#include <sstream>
#include <csignal>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>


void Debugger::run() {
    int wait_status;
    waitpid(m_pid, &wait_status, 0);
    std::cout << "Attached to  process " << m_pid << "\n";


    signal(SIGINT, SIG_IGN);

    std::string line;

    while (true) {
        std::cout << "trident> ";
        if (!std::getline(std::cin, line)) break;
        handle_command(line);
    }
}

void Debugger::handle_command(const std::string& line) {

    auto args = split_input(line);
    if (args.empty()) return;

    std::string cmd = args[0];

    if (cmd == "continue" || cmd == "c") {
        continue_execution();
    } else if (cmd == "break" || cmd == "b") {
        if (args.size() < 2) return;
        std::intptr_t addr = std::stol(args[1], 0, 16);
        set_breakpoint(addr);
    } else if (cmd == "regs") {
        dump_registers();
    } else if (cmd == "quit" || cmd == "exit") {
        kill(m_pid, SIGKILL);
        exit(0);
    } else {
        std::cout << "Unknown command:  " << cmd << "\n";
    }
}

void Debugger::continue_execution() {
    int wait_status;
    ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);
    waitpid(m_pid, &wait_status, 0);

    if (WIFSTOPPED(wait_status)) {
        std::cout << " Stopped. Signal: " << WSTOPSIG(wait_status) << "\n";
        dump_registers();
    } else if (WIFEXITED(wait_status)) {
        std::cout << "Process exited.\n";
        exit(0);
    }
}

void Debugger::set_breakpoint(std::intptr_t addr) {
    Breakpoint bp(m_pid, addr);

    bp.enable();
    m_breakpoints[addr] = bp;
    std::cout << "Breakpoint set at 0x" << std::hex << addr << std::dec << "\n";
}


void Debugger::dump_registers() {
    user_regs_struct regs;
    ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs);
    std::cout << std::hex
              << "  RIP: 0x" << regs.rip
              << "  RSP: 0x" << regs.rsp
              << "  RBP: 0x" << regs.rbp
              << "\n"
              << "  RAX: 0x" << regs.rax
              << "  RBX: 0x" << regs.rbx
              << "  RCX: 0x" << regs.rcx
              << "\n"
              << std::dec;
}


std::vector<std::string> Debugger::split_input(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string item;
    while (ss >> item) out.push_back(item);
    return out;
}