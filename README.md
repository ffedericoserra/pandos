# PandOSsh

PandOSsh is an educational multiprocessor operating system designed for the µRISCV architecture. It is structured around six levels of abstraction, inspired by Dijkstra's "THE" operating system model. This repository currently contains the implementation of one of three phases of implenetation: **Phase 1: The Queue Managers**, which handles Process Control Blocks (PCBs) and the Active Semaphore List (ASL).

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

1. **Run the emulator:**
* Start `ursicv`
* Load the `config_machine.json` file under Simulator > Open Configuration
* Power On, then Continue
* Correct execution will print 'So Long and Thanks for all The Fish' under Terminal0
