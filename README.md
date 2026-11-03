# trident-dbg

A lightweight C++ debugger for x86_64 Linux. Built to explore OS internals, memory management, and the ptrace system call API.

I built Trident because I wanted to stop treating debuggers like black boxes and actually understand how they hijack a process. Instead of just wrapping an existing library, I wrote the core primitives from scratch to see how the OS actually handles execution control. It's been a deep dive into the ptrace system call and the constant battle between user-space and the kernel.

## Features

**Process Control** — Uses `ptrace` and `waitpid` to attach to processes, single-step through instructions, and resume execution.

**Breakpoint Injection** — Works by overwriting a byte of the target's code with an `INT 3` instruction (`0xCC`) to force a trap, then restoring the original byte afterward.

**Register & Memory Inspection** — Peek into (and modify) CPU registers and raw memory blocks while the process is paused.

**ELF Parsing** — Reads the binary's `.symtab` so you can break on function names like `main`, and works out where the binary was actually loaded (including PIE binaries with ASLR).

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

The fastest way to test it is with the included dummy program (`examples/dummy.cpp`), which the build above also compiles into `build/dummy`. From the `build/` directory:

```bash
./trident_dbg ./dummy
```

A short session:

```
Loaded 28 symbols.
Attached to process 21929
Load address: 0x5fa357ab2000  (PIE: add this to objdump offsets)
trident> b main
Breakpoint set at 0x5fa357ab3189
trident> c
Breakpoint hit at 0x5fa357ab3189
  RIP: 0x5fa357ab318a  RFLAGS: 0x246
  ...
trident> si
Stopped (SIGTRAP).
  RIP: 0x5fa357ab318d  RFLAGS: 0x246
  ...
```

(After a breakpoint hit RIP shows one byte past the breakpoint: that's the `INT 3` that just executed. Trident rewinds it before resuming.)

Available commands inside the debugger. Anywhere an `<addr>` is taken you can pass either a hex address (`0x5fa357ab3189`, or without the `0x`) or a symbol name (`main`):

| Command | Description |
|---|---|
| `break <addr>` / `b <addr>` | Set a breakpoint |
| `delete <addr>` / `del <addr>` | Remove a breakpoint |
| `info` / `bps` | List breakpoints |
| `continue` / `c` | Resume execution until the next breakpoint or signal |
| `step` / `si` | Execute one instruction and stop |
| `regs` | Dump general-purpose registers |
| `setreg <reg> <val>` | Write a hex value to a register (`rax`, `rip`, `r8`, ...) |
| `mem <addr> [n]` | Read n 8-byte words from address (default 1) |
| `memset <addr> <val>` | Write one 8-byte hex word to address |
| `help` / `?` | Show the command list |
| `quit` / `exit` | Kill the process (or detach from it, if attached with `--pid`) and exit |

Signals the target receives (e.g. `SIGSEGV`) are reported when it stops and are delivered to it on the next `continue`/`step`.

### Attaching with `--pid`

Most distros (and WSL) ship with `kernel.yama.ptrace_scope = 1`, which only allows tracing your own child processes. To attach to an arbitrary process, either run as root or relax it for the session:

```bash
sudo sysctl kernel.yama.ptrace_scope=0
```

On `quit`, Trident removes its breakpoints and detaches, so the process keeps running.

## How It Works

Trident forks the target process and immediately calls `PTRACE_TRACEME` in the child, which tells the kernel to pause it before the first instruction and hand control to the parent. From there, the debugger loop takes over — injecting `INT 3` bytes for breakpoints, using `PTRACE_GETREGS`/`PTRACE_SETREGS` to inspect CPU state, and `PTRACE_PEEKDATA`/`PTRACE_POKEDATA` to read and write the target's memory.

## Dependencies

- Linux x86_64
- CMake 3.14+
- C++17 compiler (GCC or Clang)
