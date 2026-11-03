#include "breakpoint.hpp"
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/ptrace.h>


Breakpoint::Breakpoint(pid_t pid, std::intptr_t addr)
    : m_pid{pid}, m_addr{addr}, m_enabled{false}, m_saved_data{0} {}

void Breakpoint::enable() {
    if (m_enabled) return;   // never save 0xCC as the "original" byte
    errno = 0;
    long data = ptrace(PTRACE_PEEKDATA, m_pid, m_addr, nullptr);
    if (data == -1 && errno != 0) {
        std::cerr << "enable: PEEKDATA failed at 0x" << std::hex << m_addr << std::dec
                  << ": " << std::strerror(errno) << "\n";
        return;
    }
    m_saved_data = static_cast<uint8_t>(data & 0xFF);
    long patched = ((data & ~0xFFL) | 0xCC);
    ptrace(PTRACE_POKEDATA, m_pid, m_addr, patched);
    m_enabled = true;
}

void Breakpoint::disable() {
    if (!m_enabled) return;
    errno = 0;
    long data = ptrace(PTRACE_PEEKDATA, m_pid, m_addr, nullptr);
    if (data == -1 && errno != 0) {
        std::cerr << "disable: PEEKDATA failed at 0x" << std::hex << m_addr << std::dec
                  << ": " << std::strerror(errno) << "\n";
        return;
    }
    long restored = ((data & ~0xFFL) | m_saved_data);
    ptrace(PTRACE_POKEDATA, m_pid, m_addr, restored);
    m_enabled = false;
}

bool Breakpoint::is_enabled() const { return m_enabled; }

std::intptr_t Breakpoint::get_addr() const { return m_addr; }
