#pragma once
#include "breakpoint.hpp"
#include <string>
#include <map>
#include <sys/types.h>
#include <unordered_map>

class Debugger {
public:
    Debugger(pid_t pid) : m_pid{pid} {}
    void run();


private:
    pid_t m_pid;
    std::string  m_prog;
    std::intptr_t m_load_addr;
    std::map<std::intptr_t, Breakpoint> m_breakpoints;

    std::unordered_map<std::string, std::intptr_t> m_symbols;  // name -> file offset
    void load_symbols();
    std::intptr_t resolve(const std::string& name_or_addr);


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
    bool ptrace_check(const char* op, long ret) const;
    void set_register(const std::string& name, uint64_t val);
};