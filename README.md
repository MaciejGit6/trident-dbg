# trident-dbg

A lightweight C++ debugger for x86_64 Linux. Built to explore OS internals, memory management, and the ptrace system call API.

I built Trident because I wanted to stop treating debuggers like black boxes and actually understand how they hijack a process. Instead of just wrapping an existing library, I wrote the core primitives from scratch to see how the OS actually handles execution control. It's been a deep dive into the ptrace system call and the constant battle between user-space and the kernel.

## Features

**Process Control** — Uses `ptrace` and `waitpid` to attach to processes, single-step through instructions, and resume execution.

**Breakpoint Injection** — Works by overwriting a byte of the target's code with an `INT 3` instruction (`0xCC`) to force a trap, then restoring the original byte afterward.

**Register & Memory Inspection** — Peek into (and modify) CPU registers and raw memory blocks while the process is paused.

**ELF Parsing** — Navigates ELF structures to map the binary layout and locate where things are actually happening in memory.

## Build

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

```bash
./trident_dbg <executable>
# Attach to an already-running process
./trident_dbg --pid <pid>
```

The fastest way to test it is with the included dummy program:

```bash
# In one terminal, compile the dummy target
g++ examples/dummy.cpp -o examples/dummy

# Then run the debugger against it
./trident_dbg examples/dummy
```

Available commands inside the debugger:

| Command | Description |
|---|---|
| `break <addr>` / `b <addr>` | Set a breakpoint at a hex address |
| `continue` / `c` | Resume execution |
| `regs` | Dump CPU registers |
| `quit` / `exit` | Kill the process and exit |
| `step` / `si`               | Execute one instruction and stop      |
| `mem <addr> [n]`            | Read n 8-byte words from address (default 1) |

## How It Works

Trident forks the target process and immediately calls `PTRACE_TRACEME` in the child, which tells the kernel to pause it before the first instruction and hand control to the parent. From there, the debugger loop takes over — injecting `INT 3` bytes for breakpoints, using `PTRACE_GETREGS`/`PTRACE_SETREGS` to inspect CPU state, and `PTRACE_PEEKDATA`/`PTRACE_POKEDATA` to read and write the target's memory.

## Dependencies

- Linux x86_64
- CMake 3.14+
- C++17 compiler (GCC or Clang)
