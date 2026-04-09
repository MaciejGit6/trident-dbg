# trident-dbg
A lightweight C++ debugger for x86_64 Linux. Built to explore OS internals, memory management, and the ptrace system call API


Trident is an interactive, user-space debugger for x86_64 Linux applications, written entirely in modern C++. It was engineered from the ground up to explore the complex boundary between user-space execution and the operating system kernel.

Rather than relying on existing debugging frameworks, Trident implements core debugging primitives from scratch. Under the hood, it leverages the ptrace system call to intercept, control, and observe the execution state of child processes.

Core Architecture & Capabilities:

Execution Control: Implements process attachment, single-stepping, and continuation using ptrace and waitpid state management.

Software Breakpoints: Injects INT 3 (0xCC) trap instructions directly into process memory to halt execution at specific memory addresses.

State Inspection: Reads and mutates CPU hardware registers and raw memory blocks within the target's address space.

Binary Parsing: Navigates ELF (Executable and Linkable Format) layouts to map execution states to underlying binary structures.

This project serves as a practical demonstration of low-level systems programming, robust memory management, and a deep understanding of Linux OS internals.
