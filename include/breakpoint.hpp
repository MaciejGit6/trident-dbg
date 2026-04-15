#pragma once
#include <cstdint>
#include <sys/types.h>



class Breakpoint {
public:
    Breakpoint() : m_pid{0}, m_addr{0}, m_enabled{false}, m_saved_data{0} {}

    Breakpoint(pid_t pid, std::intptr_t addr);

    void enable();
    void disable();
    bool is_enabled() const;
    
    std::intptr_t get_addr() const;

private:
    pid_t m_pid;
    std::intptr_t m_addr;
    bool m_enabled;
    uint8_t m_saved_data;
};