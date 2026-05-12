# PandOSsh

PandOSsh is an educational multiprocessor operating system designed for the µRISCV architecture, structured around six levels of abstraction inspired by Dijkstra's "THE" operating system model.

The implementation presented in this repository is part of the Operating Systems course project held at the **University of Bologna** for the academic year 2025/2026. Full details can be found at the [official course page](https://www.cs.unibo.it/~renzo/so/progetto.shtml), under the Progetto section.

This repository contains the implementation of all three phases:

* **Phase 1 - The Queue Managers**: handles Process Control Blocks (PCBs) and the Active Semaphore List (ASL).
* **Phase 2 - The Nucleus**: implements the kernel layer with process lifecycle management, preemptive round-robin scheduling, system call services (NSYS1-10), exception handling, and interrupt-driven device I/O.
* **Phase 3 - The Support Level**: virtual memory with a FIFO-replaced swap pool backed by per-U-proc flash devices, a Support-level general exception handler implementing SYS2/SYS4/SYS5/SYS6, and an interactive shell that spawns user programs (`fibEight`, `echo`, `fibEleven`, `uname`, `date`, `sl`, `calc`).

[`PandOSSh_Doc.md`](PandOSSh_Doc.md) contains the full documentation for all three phases.

## Authors
* Yuri Disalvatore
* Leonardo Carletti
* Giacomo Bruno
* Federico Serra

## Prerequisites
To compile and run this project, you need the following tools installed:
* **µRISCV Emulator**: github.com/virtualsquare/uriscv/releases/latest
* **GNU Toolchain for RISC-V** (`gcc-riscv64-unknown-elf`): installed with the µRISCV Emulator
* **CMake**

## Building and Running

### Phase 1 / Phase 2

1. Build the kernel:
   ```bash
   cmake -B build
   cmake --build build
   ```
2. Run the emulator:
   * Start `uriscv`
   * Load `config_machine.json` under Simulator > Open Configuration
   * Power On, then Continue
   * Correct execution prints `System halted` after completing all test iterations, with `p1 finishes OK -- TTFN` visible under Terminal0

### Phase 3

The Phase 3 build replaces `phase2/p2test.c` with the Phase 3 modules and additionally produces the user-space binaries that ship as flash-device images.

1. Build the kernel:
   ```bash
   cmake -B build
   cmake --build build
   ```
2. Build the testers:
   ```bash
   make -C testers all
   ```
   The two steps together produce:
   * `build/MultiPandOS.core.uriscv` — kernel image
   * `testers/{shell,fibEight,echo,fibEleven,uname,date,sl,calc}.uriscv` — flash-device images, one per ASID
3. Run the emulator:
   * Start `uriscv`
   * Load `phase3_config_machine.json` under Simulator > Open Configuration
   * Power On, then Continue
4. The shell prints `PandOSsh shell` and a `$ ` prompt at Terminal0. Available commands:

   | Command | Effect |
   |---|---|
   | `uname`     | prints `PandOSsh` |
   | `date`      | prints `Sat  5 Nov 06:15:00 PST 1955` |
   | `sl`        | renders an ASCII-art locomotive |
   | `fibEight`  | recursive Fibonacci(8) — prints success on completion |
   | `fibEleven` | recursive Fibonacci(11) — exercises FIFO eviction in the swap pool |
   | `echo`      | prompts for a string and echoes it back |
   | `calc`      | one-shot single-digit calculator (`<digit><op><digit>`); flash7 is disabled by default in the JSON, enable it to use |
   | `exit`      | terminates the shell, the OS halts cleanly |

   Unknown commands print `command not found`.
