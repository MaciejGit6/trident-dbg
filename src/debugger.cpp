#include "debugger.hpp"
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <elf.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>

// Returns the base address the main executable is mapped at: the start of
// the mapping that covers file offset 0 of m_prog (the ELF header).
std::intptr_t Debugger::read_load_address() {
    char resolved[PATH_MAX];
    std::string prog_path = realpath(m_prog.c_str(), resolved) ? resolved : m_prog;

    std::string maps_path = "/proc/" + std::to_string(m_pid) + "/maps";
    std::ifstream maps(maps_path);
    std::string line;
    while (std::getline(maps, line)) {
        // Lines look like: 555555554000-555555556000 r--p 00000000 fd:01 12345  /path/to/binary
        std::istringstream ss(line);
        std::string range, perms, offset, dev, inode, path;
        ss >> range >> perms >> offset >> dev >> inode >> path;
        if (path != prog_path) continue;
        if (std::stoull(offset, nullptr, 16) != 0) continue;
        return static_cast<std::intptr_t>(std::stoull(range, nullptr, 16));
    }
    return 0;
}

void Debugger::print_help() {
    std::cout <<
        "Commands:\n"
        "  break <addr|sym> / b         Set a breakpoint at hex address or symbol\n"
        "  delete <addr|sym> / del      Remove breakpoint\n"
        "  info          / bps          List all breakpoints\n"
        "  continue      / c            Resume execution\n"
        "  step          / si           Execute one instruction\n"
        "  regs                         Dump all general-purpose registers\n"
        "  mem <addr|sym> [n]           Read n 8-byte words from address\n"
        "  memset <addr|sym> <val>      Write one 8-byte word to address\n"
        "  setreg <reg> <val>           Write hex value to a named register\n"
        "  quit          / exit         Kill (or detach from) process and exit\n"
        "  help          / ?            Show this message\n";
}

void Debugger::load_symbols() {
    std::ifstream f(m_prog, std::ios::binary);
    if (!f) { std::cerr << "Cannot open " << m_prog << " for symbol parsing\n"; return; }

    Elf64_Ehdr ehdr;
    if (!f.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr)) ||
        std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        std::cerr << "Not a 64-bit ELF binary\n"; return;
    }
    m_is_pie = (ehdr.e_type == ET_DYN);

    if (ehdr.e_shnum == 0 || ehdr.e_shstrndx >= ehdr.e_shnum) {
        std::cerr << "No section headers\n"; return;
    }

    // Read section headers
    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    f.seekg(ehdr.e_shoff);
    f.read(reinterpret_cast<char*>(shdrs.data()), ehdr.e_shnum * sizeof(Elf64_Shdr));

    // Load section-name string table
    const Elf64_Shdr& shstrtab_hdr = shdrs[ehdr.e_shstrndx];
    std::vector<char> shstrtab(shstrtab_hdr.sh_size + 1, '\0');
    f.seekg(shstrtab_hdr.sh_offset);
    f.read(shstrtab.data(), shstrtab_hdr.sh_size);

    // Find .symtab and its associated .strtab
    Elf64_Shdr* symtab_hdr  = nullptr;
    Elf64_Shdr* strtab_hdr  = nullptr;
    for (auto& shdr : shdrs) {
        if (shdr.sh_name >= shstrtab_hdr.sh_size) continue;
        std::string sec_name = &shstrtab[shdr.sh_name];
        if (shdr.sh_type == SHT_SYMTAB && sec_name == ".symtab") symtab_hdr = &shdr;
        if (shdr.sh_type == SHT_STRTAB && sec_name == ".strtab") strtab_hdr = &shdr;
    }
    if (!symtab_hdr || !strtab_hdr) {
        std::cerr << "No .symtab found (stripped binary?)\n"; return;
    }

    // Read string table (extra NUL guards against an unterminated last string)
    std::vector<char> strtab(strtab_hdr->sh_size + 1, '\0');
    f.seekg(strtab_hdr->sh_offset);
    f.read(strtab.data(), strtab_hdr->sh_size);

    // Read symbol entries
    size_t n_syms = symtab_hdr->sh_size / sizeof(Elf64_Sym);
    std::vector<Elf64_Sym> syms(n_syms);
    f.seekg(symtab_hdr->sh_offset);
    f.read(reinterpret_cast<char*>(syms.data()), n_syms * sizeof(Elf64_Sym));

    for (const auto& sym : syms) {
        if (sym.st_name == 0 || sym.st_value == 0) continue;
        if (sym.st_name >= strtab_hdr->sh_size) continue;
        std::string name = &strtab[sym.st_name];
        m_symbols[name] = static_cast<std::intptr_t>(sym.st_value);
    }
    std::cout << "Loaded " << m_symbols.size() << " symbols.\n";
}

// Accepts a hex address (with or without 0x) or a symbol name.
// Returns 0 on failure. Throws std::invalid_argument / std::out_of_range
// on malformed numbers; handle_command catches those.
std::intptr_t Debugger::resolve(const std::string& s) {
    auto it = m_symbols.find(s);
    if (it != m_symbols.end()) {
        // st_value is a link-time virtual address. Non-PIE (ET_EXEC) binaries
        // are loaded exactly there; PIE (ET_DYN) binaries are linked at 0 and
        // relocated by the load base.
        return it->second + (m_is_pie ? m_load_addr : 0);
    }

    size_t pos = 0;
    std::intptr_t addr = 0;
    try {
        addr = static_cast<std::intptr_t>(std::stoull(s, &pos, 16));
    } catch (const std::invalid_argument&) {
        pos = 0;
    }
    if (pos != s.size()) {
        std::cerr << "Symbol not found: " << s << "\n";
        return 0;
    }
    return addr;
}

void Debugger::run() {
    int wait_status;
    waitpid(m_pid, &wait_status, 0);
    if (WIFEXITED(wait_status) || WIFSIGNALED(wait_status)) {
        std::cerr << "Process " << m_pid << " terminated before the debugger could take over\n";
        return;
    }

    m_load_addr = read_load_address();
    load_symbols();

    std::cout << "Attached to process " << m_pid << "\n";
    if (m_load_addr)
        std::cout << "Load address: 0x" << std::hex << m_load_addr << std::dec
                  << (m_is_pie ? "  (PIE: add this to objdump offsets)" : "  (non-PIE)") << "\n";

    signal(SIGINT, SIG_IGN);

    std::string line;

    while (true) {
        std::cout << "trident> ";
        if (!std::getline(std::cin, line)) break;
        try {
            handle_command(line);
        } catch (const std::invalid_argument&) {
            std::cout << "Invalid number\n";
        } catch (const std::out_of_range&) {
            std::cout << "Number out of range\n";
        }
    }
    quit();
}

void Debugger::handle_command(const std::string& line) {

    auto args = split_input(line);
    if (args.empty()) return;

    std::string cmd = args[0];

    if (cmd == "continue" || cmd == "c") {
        continue_execution();
    } else if (cmd == "break" || cmd == "b") {
        if (args.size() < 2) { std::cout << "usage: break <addr|sym>\n"; return; }
        std::intptr_t addr = resolve(args[1]);
        if (addr) set_breakpoint(addr);
    } else if (cmd == "step" || cmd == "si") {
        step();
    } else if (cmd == "mem") {
        if (args.size() < 2) {
            std::cout << "usage: mem <addr> [n_words]\n";
            return;
        }
        std::intptr_t addr = resolve(args[1]);
        size_t        n    = args.size() >= 3 ? std::stoul(args[2]) : 1;
        if (addr) read_memory(addr, n);
    } else if (cmd == "info" || cmd == "bps") {
        list_breakpoints();
    } else if (cmd == "delete" || cmd == "del") {
        if (args.size() < 2) { std::cout << "usage: del <addr>\n"; return; }
        std::intptr_t addr = resolve(args[1]);
        if (addr) delete_breakpoint(addr);
    } else if (cmd == "memset") {
        if (args.size() < 3) { std::cout << "usage: memset <addr> <val>\n"; return; }
        std::intptr_t addr = resolve(args[1]);
        uint64_t      val  = std::stoull(args[2], nullptr, 16);
        if (addr) write_memory(addr, val);
    } else if (cmd == "setreg") {
        if (args.size() < 3) { std::cout << "usage: setreg <reg> <val>\n"; return; }
        uint64_t val = std::stoull(args[2], nullptr, 16);
        set_register(args[1], val);
    } else if (cmd == "regs") {
        dump_registers();
    } else if (cmd == "help" || cmd == "?") {
        print_help();
    } else if (cmd == "quit" || cmd == "exit") {
        quit();
    } else {
        std::cout << "Unknown command:  " << cmd << "\n";
    }
}

void Debugger::quit() {
    if (m_attached) {
        // Leave the process the way we found it: remove INT3s, then detach.
        for (auto& [addr, bp] : m_breakpoints) bp.disable();
        if (m_stopped_at_bp) {
            user_regs_struct regs;
            if (ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs) != -1) {
                regs.rip -= 1;
                ptrace(PTRACE_SETREGS, m_pid, nullptr, &regs);
            }
        }
        if (ptrace(PTRACE_DETACH, m_pid, nullptr, nullptr) == -1)
            std::perror("ptrace(PTRACE_DETACH)");
        else
            std::cout << "Detached from process " << m_pid << "\n";
    } else {
        kill(m_pid, SIGKILL);
        waitpid(m_pid, nullptr, 0);
    }
    std::exit(0);
}

void Debugger::continue_execution() {
    int wait_status;
    if (step_over_breakpoint(wait_status) && !WIFSTOPPED(wait_status)) {
        handle_stop(wait_status);   // process died while stepping over the bp
        return;
    }

    ptrace(PTRACE_CONT, m_pid, nullptr, m_pending_sig);
    m_pending_sig = 0;
    waitpid(m_pid, &wait_status, 0);

    handle_stop(wait_status);
}

void Debugger::step() {
    int wait_status;
    // If we're sitting on a breakpoint, stepping over it already executes
    // exactly one instruction; don't single-step a second time.
    if (!step_over_breakpoint(wait_status)) {
        ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, m_pending_sig);
        m_pending_sig = 0;
        waitpid(m_pid, &wait_status, 0);
    }

    handle_stop(wait_status);
}

void Debugger::set_breakpoint(std::intptr_t addr) {
    if (m_breakpoints.count(addr)) {
        std::cout << "Breakpoint already set at 0x" << std::hex << addr << std::dec << "\n";
        return;
    }
    Breakpoint bp(m_pid, addr);
    bp.enable();
    if (!bp.is_enabled()) return;   // enable() already printed the error
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
    it->second.disable();
    m_breakpoints.erase(it);

    // If we're stopped just past this INT3, rewind RIP so the original
    // instruction still executes on the next continue/step.
    if (m_stopped_at_bp) {
        user_regs_struct regs;
        if (ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs) != -1 &&
            static_cast<std::intptr_t>(regs.rip) - 1 == addr) {
            regs.rip -= 1;
            ptrace(PTRACE_SETREGS, m_pid, nullptr, &regs);
            m_stopped_at_bp = false;
        }
    }
    std::cout << "Breakpoint at 0x" << std::hex << addr << std::dec << " removed.\n";
}


void Debugger::dump_registers() {
    user_regs_struct regs;
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
    long ret = ptrace(PTRACE_POKEDATA, m_pid, addr, val);
    if (!ptrace_check("POKEDATA", ret)) return;
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

// If the tracee is stopped on an enabled breakpoint, execute the original
// instruction underneath it (exactly one instruction) and re-arm the INT3.
// Two cases:
//   - we just hit the INT3 (m_stopped_at_bp): RIP = bp + 1, rewind it;
//   - we arrived at bp by single-stepping/setreg: RIP = bp, the INT3 hasn't run.
// Returns true if it stepped (wait_status is then filled in).
bool Debugger::step_over_breakpoint(int& wait_status) {
    user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs) == -1) return false;

    std::intptr_t bp_addr = static_cast<std::intptr_t>(regs.rip);
    if (m_stopped_at_bp) bp_addr -= 1;
    m_stopped_at_bp = false;

    auto it = m_breakpoints.find(bp_addr);
    if (it == m_breakpoints.end() || !it->second.is_enabled()) return false;

    it->second.disable();                                        // restore original byte
    if (static_cast<std::intptr_t>(regs.rip) != bp_addr) {
        regs.rip = static_cast<unsigned long long>(bp_addr);     // rewind RIP by 1
        ptrace(PTRACE_SETREGS, m_pid, nullptr, &regs);
    }

    ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, m_pending_sig);   // step over real instruction
    m_pending_sig = 0;
    waitpid(m_pid, &wait_status, 0);

    if (WIFSTOPPED(wait_status))
        it->second.enable();                                     // re-arm
    return true;
}

void Debugger::handle_stop(int wait_status) {
    m_stopped_at_bp = false;
    if (WIFEXITED(wait_status)) {
        std::cout << "Process exited with code " << WEXITSTATUS(wait_status) << "\n";
        std::exit(0);
    }
    if (WIFSIGNALED(wait_status)) {
        int sig = WTERMSIG(wait_status);
        std::cout << "Process killed by signal " << sig << " (" << strsignal(sig) << ")\n";
        std::exit(0);
    }
    if (WIFSTOPPED(wait_status)) {
        int sig = WSTOPSIG(wait_status);
        if (sig == SIGTRAP) {
            siginfo_t info;
            ptrace(PTRACE_GETSIGINFO, m_pid, nullptr, &info);
            user_regs_struct regs;
            ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs);
            std::intptr_t possible_bp = static_cast<std::intptr_t>(regs.rip) - 1;
            // INT3 reports SI_KERNEL (or TRAP_BRKPT); a single-step reports TRAP_TRACE.
            bool int3 = info.si_code == SI_KERNEL || info.si_code == TRAP_BRKPT;
            if (int3 && m_breakpoints.count(possible_bp)) {
                m_stopped_at_bp = true;
                std::cout << "Breakpoint hit at 0x" << std::hex << possible_bp << std::dec << "\n";
            } else {
                std::cout << "Stopped (SIGTRAP).\n";
            }
        } else {
            // Deliver real signals (SIGSEGV, SIGTERM, ...) on the next resume
            // instead of silently swallowing them. SIGINT/SIGSTOP are the
            // debugger's own interrupts and are not forwarded.
            if (sig != SIGINT && sig != SIGSTOP) m_pending_sig = sig;
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

    // Writing RIP moves us off whatever INT3 we were stopped past.
    if (name == "rip") m_stopped_at_bp = false;

    std::cout << name << " = 0x" << std::hex << val << std::dec << "\n";
}
