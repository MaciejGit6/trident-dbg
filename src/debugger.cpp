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
    step_over_breakpoint();   //rewinding RIP, restore byte; single-step past it, re-arm

    ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);

    int wait_status;
    waitpid(m_pid, &wait_status, 0);

    handle_stop(wait_status);
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

void Debugger::step_over_breakpoint() {
    user_regs_struct regs;
    ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs);

    std::intptr_t possible_bp = static_cast<std::intptr_t>(regs.rip) - 1;
    auto it = m_breakpoints.find(possible_bp);
    if (it == m_breakpoints.end() || !it->second.is_enabled()) return;

    it->second.disable();                                        // restore original byte
    regs.rip = static_cast<unsigned long long>(possible_bp);    // rewind RIP by 1
    ptrace(PTRACE_SETREGS, m_pid, nullptr, &regs);

    ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr);         // step over real instruction
    int wait_status;
    waitpid(m_pid, &wait_status, 0);

    it->second.enable();                                         // re-arm
}

void Debugger::handle_stop(int wait_status) {
    if (WIFEXITED(wait_status)) {
        std::cout << "Process exited with code " << WEXITSTATUS(wait_status) << "\n";
        exit(0);
    }
    if (WIFSTOPPED(wait_status)) {
        int sig = WSTOPSIG(wait_status);
        if (sig == SIGTRAP) {
            user_regs_struct regs;
            ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs);
            std::intptr_t possible_bp = static_cast<std::intptr_t>(regs.rip) - 1;
            if (m_breakpoints.count(possible_bp))
                std::cout << "Breakpoint hit at 0x" << std::hex << possible_bp << std::dec << "\n";
            else
                std::cout << "Stopped (SIGTRAP).\n";
        } else {
            std::cout << "Stopped. Signal: " << sig << " (" << strsignal(sig) << ")\n";
        }
        dump_registers();
    }
}