# PandOSsh

PandOSsh is an educational multiprocessor operating system designed for the µRISCV architecture, structured around six levels of abstraction inspired by Dijkstra's "THE" operating system model.

The implementation presented in this repository is part of the Operating Systems course project held at the **University of Bologna** for the academic year 2025/2026. Full details can be found at the [official course page](https://www.cs.unibo.it/~renzo/so/progetto.shtml), under the Progetto section.

This repository currently contains the implementation of two of the three phases:

* **Phase 1 - The Queue Managers**: handles Process Control Blocks (PCBs) and the Active Semaphore List (ASL).
* **Phase 2 - The Nucleus**: implements the kernel layer with process lifecycle management, preemptive round-robin scheduling, system call services (NSYS1-10), exception handling, and interrupt-driven device I/O.

[`PandOSSh_Doc.md`](PandOSSh_Doc.md) contains the full documentation for both phases.

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

1. **Build the project:**
   ```bash
   cmake -B build
   cmake --build build
   ```

2. **Run the emulator:**
* Start `uriscv`
* Load the `config_machine.json` file under Simulator > Open Configuration
* Power On, then Continue
* Correct execution will print `System halted` after completing all test iterations, with `p1 finishes OK -- TTFN` visible under Terminal0
