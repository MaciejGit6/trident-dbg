#pragma once
#include "breakpoint.hpp"
#include <string>
#include <map>
#include <sys/types.h>

class Debugger {
public:
    Debugger(pid_t pid) : m_pid{pid} {}
    void run();


private:
    pid_t m_pid;
    std::string  m_prog;
    std::intptr_t m_load_addr;
    std::map<std::intptr_t, Breakpoint> m_breakpoints;


    void handle_command(const std::string& line);
    void continue_execution();
    void step(); 
    void set_breakpoint(std::intptr_t addr);
    void dump_registers();
    std::vector<std::string> split_input(const std::string& s);
    void step_over_breakpoint();
    void handle_stop(int wait_status);

    void read_memory(std::intptr_t addr, size_t n_words);
    void list_breakpoints();
    void delete_breakpoint(std::intptr_t addr);
    void write_memory(std::intptr_t addr, uint64_t val);
    void print_help();
    std::intptr_t read_load_address();
};