# PandOSsh Phase 1

This is the **Phase 1** implementation of the **PandOSsh** operating system.
This layer, known as "The Queue Managers", implements the fundamental data structures used to track processes and synchronization primitives. It does not execute processes yet; it simply manages the memory and relationships of the structures that *will* represent them.

## What is this?

Phase 1 implements Level 2 of the [THE operating system model](https://en.wikipedia.org/wiki/THE_multiprogramming_system). It focuses on two key entities:

1.  **Process Control Blocks (PCBs)**: The state descriptors for active processes.
2.  **Active Semaphore List (ASL)**: A system to manage processes blocked on semaphores.

All data structures are allocated statically at compile time to avoid dynamic memory management overhead during the early kernel boot stages.

## Features

* **PCB Management**:
    * Allocation and deallocation from a static free list (`pcbFree_h`).
    * **Priority Queues**: Process queues are maintained as doubly-linked circular lists, strictly ordered by priority.
    * **Process Trees**: Support for parent/child/sibling relationships to track process lineage.
* **Semaphore Management**:
    * **ASL**: A sorted linked list of active semaphores (semaphores with at least one blocked process).
    * Efficient handling of blocked process queues associated with specific semaphore keys.
* **Linux-like Lists**: Uses `listx.h`, a subset of the standard Linux Kernel list implementation (circular, doubly-linked, type-oblivious).

## Implementation Details

### The PCB Module (`pcb.c`)
We treat `pcb_t` structures as the active entities.
* **Storage**: An array `pcbTable` (via `pcbFree_table`) of size `MAXPROC` (20) holds all potential processes.
* **Queues**: `insertProcQ` enforces priority ordering (higher integer = higher priority). If priorities match, FIFO is used.
* **Trees**: Each PCB contains pointers (`p_parent`, `p_child`, `p_sib`) to navigate the process tree efficiently.

### The ASL Module (`asl.c`)
The ASL maps a semaphore address (integer key) to a queue of blocked PCBs.
* **Storage**: A static array `semd_table` holds the semaphore descriptors.
* **Lookup**: The `semd_h` list is sorted by the semaphore key (`s_key`) to optimize the search during insertion and removal operations.
* **Behavior**:
    * `insertBlocked`: Links a PCB to a semaphore. If the semaphore isn't active, a descriptor is allocated and inserted into the sorted ASL.
    * `removeBlocked`: Unlinks the head of the semaphore's queue. If the queue becomes empty, the descriptor is returned to the free list.

## Compiling and Running

The project uses `cmake` and a cross-compiler for the µRISCV architecture.

**1. Build**
```bash
cmake -B build
cmake --build build
```

**2. Run**
Execute the emulator using the provided machine configuration:
```bash
uriscv
```
*Note: Ensure `config_machine.json` is in the working directory.*

**3. Test Output**
The system will load `p1test.c`. Check the **Terminal 0** window in the emulator.
* **Success**: Displays "So Long and Thanks for All the Fish" followed by "System Halted".
* **Failure**: Displays "Kernel Panic".
