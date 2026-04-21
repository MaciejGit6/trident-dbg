#include "debugger.hpp"
#include <iostream>
#include <sstream>
#include <csignal>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <fstream>

std::intptr_t Debugger::read_load_address() {
    std::string maps_path = "/proc/" + std::to_string(m_pid) + "/maps";
    std::ifstream maps(maps_path);
    std::string line;
    while (std::getline(maps, line)) {
        //Lines look like: 555555554000-555555556000 r--p 00000000 fd:01 12345  /path/to/binary
        if (line.find(m_prog) == std::string::npos) continue;
        if (line.find("r-xp") == std::string::npos) continue;   //executable segment
        std::intptr_t addr = std::stol(line, nullptr, 16);
        return addr;
    }
    return 0;
}

void Debugger::print_help() {
    std::cout 
        "Commands:\n"
        "  break <addr>  / b <addr>     Set a breakpoint at hex address\n"
        "  delete <addr> / del <addr>   Remove breakpoint at hex address\n"
        "  info          / bps          List all breakpoints\n"
        "  continue      / c            Resume execution\n"
        "  step          / si           Execute one instruction\n"
        "  regs                         Dump all general-purpose registers\n"
        "  mem <addr> [n]               Read n 8-byte words from address\n"
        "  memset <addr> <val>          Write one 8-byte word to address\n"
        "  quit          / exit         Kill process and exit\n"
        "  help          / ?            Show this message\n";
        "  setreg <reg> <val>           Write hex value to a named register\n"
}

void Debugger::run() {
    int wait_status;
    waitpid(m_pid, &wait_status, 0);

    m_load_addr = read_load_address();

    std::cout << "Attached to process " << m_pid << "\n";
    if (m_load_addr)
        std::cout << "Load address: 0x" << std::hex << m_load_addr << std::dec
                  << "  (add this to objdump offsets for PIE binaries)\n";


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
    } else if (cmd == "step" || cmd == "si") {
        step();
    } else if (cmd == "mem") {
        if (args.size() < 2) {
            std::cout << "usage: mem <addr> [n_words]\n";
            return;
        }
        std::intptr_t addr   = std::stol(args[1], nullptr, 16);
        size_t        n      = args.size() >= 3 ? std::stoul(args[2]) : 1;
        read_memory(addr, n);
    } else if (cmd == "info" || cmd == "bps") {
        list_breakpoints();
    } else if (cmd == "delete" || cmd == "del") {
        if (args.size() < 2) { std::cout << "usage: del <addr>\n"; return; }
        std::intptr_t addr = std::stol(args[1], nullptr, 16);
        delete_breakpoint(addr);
    } else if (cmd == "memset") {
        if (args.size() < 3) { std::cout << "usage: memset <addr> <val>\n"; return; }
        std::intptr_t addr = std::stol(args[1], nullptr, 16);
        uint64_t      val  = std::stoull(args[2], nullptr, 16);
        write_memory(addr, val);
    } else if (cmd == "setreg") {
        if (args.size() < 3) { std::cout << "usage: setreg <reg> <val>\n"; return; }
        uint64_t val = std::stoull(args[2], nullptr, 16);
        set_register(args[1], val);
    } else if (cmd == "regs") {
        dump_registers();
    } else if (cmd == "help" || cmd == "?") {
        print_help();
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

void Debugger::step() {
    step_over_breakpoint();  

    ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr);

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

void Debugger::list_breakpoints() {
    if (m_breakpoints.empty()) {
        std::cout << "No breakpoints set.\n";
        return;
    }
    int i = 0;
    for (const auto& [addr, bp] : m_breakpoints) {
        std::cout << "  [" << i++ << "] 0x" << std::hex << addr << std::dec
                  << "  " << (bp.is_enabled() ? "enabled" : "disabled") << "\n";
    }
}

void Debugger::delete_breakpoint(std::intptr_t addr) {
    auto it = m_breakpoints.find(addr);
    if (it == m_breakpoints.end()) {
        std::cout << "No breakpoint at 0x" << std::hex << addr << std::dec << "\n";
        return;
    }
    if (it->second.is_enabled())
        it->second.disable();
    m_breakpoints.erase(it);
    std::cout << "Breakpoint at 0x" << std::hex << addr << std::dec << " removed.\n";
}


void Debugger::dump_registers() {
    user_regs_struct regs;
    ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs);
    long ret = ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs);
    if (!ptrace_check("GETREGS", ret)) return;

    std::cout << std::hex
              << "  RIP: 0x" << regs.rip   << "  RFLAGS: 0x" << regs.eflags << "\n"
              << "  RSP: 0x" << regs.rsp   << "  RBP:    0x" << regs.rbp    << "\n"
              << "  RAX: 0x" << regs.rax   << "  RBX:    0x" << regs.rbx    << "\n"
              << "  RCX: 0x" << regs.rcx   << "  RDX:    0x" << regs.rdx    << "\n"
              << "  RSI: 0x" << regs.rsi   << "  RDI:    0x" << regs.rdi    << "\n"
              << "  R8:  0x" << regs.r8    << "  R9:     0x" << regs.r9     << "\n"
              << "  R10: 0x" << regs.r10   << "  R11:    0x" << regs.r11    << "\n"
              << "  R12: 0x" << regs.r12   << "  R13:    0x" << regs.r13    << "\n"
              << "  R14: 0x" << regs.r14   << "  R15:    0x" << regs.r15    << "\n"
              << std::dec;
}

void Debugger::read_memory(std::intptr_t addr, size_t n_words) {
    std::cout << std::hex;
    for (size_t i = 0; i < n_words; ++i) {
        std::intptr_t cur = addr + static_cast<std::intptr_t>(i * 8);
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, m_pid, cur, nullptr);
        if (word == -1 && errno != 0) {
            std::cerr << "  0x" << cur << ":  read error: " << std::strerror(errno) << "\n";
            break;
        }
        std::cout << "  0x" << cur << ":  0x" << static_cast<uint64_t>(word) << "\n";
    }
    std::cout << std::dec;
}

void Debugger::write_memory(std::intptr_t addr, uint64_t val) {
    long ret = ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs);
    if (!ptrace_check("GETREGS", ret)) return;
    std::cout << "Wrote 0x" << std::hex << val
              << " to 0x" << addr << std::dec << "\n";
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

bool Debugger::ptrace_check(const char* op, long ret) const {
    if (ret == -1 && errno != 0) {
        std::cerr << "ptrace(" << op << ") failed: " << std::strerror(errno) << "\n";
        return false;
    }
    return true;
}

void Debugger::set_register(const std::string& name, uint64_t val) {
    user_regs_struct regs;
    long ret = ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs);
    if (!ptrace_check("GETREGS", ret)) return;

    bool found = true;
    if      (name == "rax") regs.rax = val;
    else if (name == "rbx") regs.rbx = val;
    else if (name == "rcx") regs.rcx = val;
    else if (name == "rdx") regs.rdx = val;
    else if (name == "rsi") regs.rsi = val;
    else if (name == "rdi") regs.rdi = val;
    else if (name == "rbp") regs.rbp = val;
    else if (name == "rsp") regs.rsp = val;
    else if (name == "rip") regs.rip = val;
    else if (name == "r8" ) regs.r8  = val;
    else if (name == "r9" ) regs.r9  = val;
    else if (name == "r10") regs.r10 = val;
    else if (name == "r11") regs.r11 = val;
    else if (name == "r12") regs.r12 = val;
    else if (name == "r13") regs.r13 = val;
    else if (name == "r14") regs.r14 = val;
    else if (name == "r15") regs.r15 = val;
    else { std::cout << "Unknown register: " << name << "\n"; found = false; }

    if (!found) return;

    ret = ptrace(PTRACE_SETREGS, m_pid, nullptr, &regs);
    if (!ptrace_check("SETREGS", ret)) return;

    std::cout << name << " = 0x" << std::hex << val << std::dec << "\n";
}