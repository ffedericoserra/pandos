# PandOSsh Technical Documentation

## 1. Project Overview

PandOSsh is an educational multiprocessor operating system designed to run on the
uRISCV architecture. The core purpose of this project is to implement the foundational
layers of an OS kernel, starting from data structure management up to process
scheduling and device handling.

The implementation described in this document has been implemented by Yuri
Disalvatore, Leonardo Carletti, Giacomo Bruno and Federico Serra.

### Project Roadmap

The development of PandOSsh is divided into three distinct phases:

- **Phase 1: The Queue Managers**
  - This phase implements Level 2 of the architecture.
  - It focuses on the logical management of processes and synchronization
    primitives (semaphores) required for the subsequent layers.
- **Phase 2: The Nucleus**
  - This phase implements Level 3 of the architecture.
  - It builds the kernel layer on top of Phase 1's data structures, providing process
    lifecycle management (creation, termination), preemptive round-robin scheduling
    with a 5ms time slice, ten nucleus system calls (NSYS1-10), exception dispatching
    with the pass-up-or-die mechanism, and interrupt-driven device I/O through
    semaphore-based synchronization.
  - It relies on the uRISCV BIOS for low-level exception routing, processor state
    save/restore, and hardware timer access.
- **Phase 3: The Support Level**
  - This phase implements Level 4 of the architecture.
  - It adds virtual memory and user programs on top of the Nucleus. Each user
    process ("U-proc") gets its own private page table, its own ASID, and its
    own flash device as backing store. Pages are loaded on demand into a shared
    swap pool by the Pager when the process touches them.
  - It provides four user-callable services (`SYS2 TERMINATE`,
    `SYS4 WRITETERMINAL`, `SYS5 READTERMINAL`, `SYS6 EXECUTE`) and an
    interactive shell that lets the user run the provided test programs by
    name.
  - It relies on Phase 2's pass-up-or-die mechanism to route TLB-Invalid
    exceptions and program traps into per-U-proc Support handlers, and on the
    Nucleus syscalls (NSYS1-5, NSYS8) for process management, semaphore
    operations, and device I/O.

This document details the implementation and architecture of all three phases.

## 2. Repository Structure

The repository is organized to separate generic kernel headers, architecture-specific
code, and phase-specific implementations.

### Directory Layout

- **headers/**: Contains global header files used across different phases.
  - `const.h`: Defines system constants.
  - `types.h`: Defines core data structures like `pcb_t` (Process Control Block), `semd_t`
    (Semaphore Descriptor), and `support_t`.
  - `listx.h`: An implementation of circular, doubly-linked lists derived from the Linux
    Kernel.
  - `utils.h`: Utility helper functions shared across kernel modules (e.g. `copyState()`).
- **phase1/**: Contains the source code specific to Phase 1.
  - `pcb.c`, `headers/pcb.h`: Implementation of PCB allocation, queues, and trees.
  - `asl.c`, `headers/asl.h`: Implementation of the Active Semaphore List (ASL).
  - `p1test.c`: The provided test code to verify the functionality of Phase 1 modules.
- **phase2/**: Contains the source code specific to Phase 2.
  - `init.c`: Nucleus initialization and entry point (`main()`).
  - `scheduler.c`, `headers/scheduler.h`: Round-robin preemptive scheduler.
  - `exceptions.c`, `headers/exceptions.h`: Exception dispatcher and pass-up-or-die mechanism.
  - `interrupts.c`, `headers/interrupts.h`: Device and timer interrupt handling.
  - `syscalls.c`, `headers/syscalls.h`: Nucleus system call implementations (NSYS1-10).
  - `p2test.c`: The provided test code to verify the functionality of Phase 2 modules.
- **phase3/**: Contains the source code specific to Phase 3.
  - `initProc.c`, `headers/initProc.h`: InstantiatorProcess (replaces the Phase 2 `test`). Initialises the swap pool, the support-level semaphores, the per-U-proc Support structures, and launches the shell.
  - `vmSupport.c`, `headers/vmSupport.h`: TLB-Refill handler, the Pager, the swap pool table, and the flash I/O helper.
  - `sysSupport.c`, `headers/sysSupport.h`: Support-level general exception handler (dispatches SYS2, SYS4, SYS5, SYS6) and `programTrap`.
- **testers/**: User-space programs that ship as flash-device images for Phase 3.
  - `shell.c`: Interactive shell that maps command names to ASIDs and spawns children via `SYS6 EXECUTE`.
  - `calc.c`: One-shot single-digit calculator.
  - `echo.c`, `fibEight.c`, `fibEleven.c`, `uname.c`, `date.c`, `sl.c`: Provided testers exercising SYS4, SYS5, and the paging path.
  - `print.c`, `h/print.h`, `h/tconst.h`: Shared print wrapper and user-side syscall numbers.
  - `Makefile`: Builds each `.c` into a `.uriscv` flash image.
- `CMakeLists.txt`: The root configuration file for the build system.
- `config_machine.json`: Configuration file for the uRISCV emulator (used for Phase 1 and Phase 2 standalone tests).
- `phase3_config_machine.json`: Configuration file for the uRISCV emulator for Phase 3 (256 RAM frames, eight flash devices, terminal0 enabled).
- `klog.c`: A utility library for circular log buffering.

## 3. Module Reference: Phase 1

Phase 1 implements two main modules: the Process Control Block (PCB) Manager and
the Active Semaphore List (ASL) Manager.

### 3.1. PCB Manager (phase1/pcb.c)

This module manages the allocation, deallocation, organization, and hierarchy of Process
Control Blocks (`pcb_t`). It maintains a pool of free PCBs (`pcbFree_h`) and supports
process queues (ordered by priority) and process trees (parent-child relationships).

**Key Functions:**

- **`void initPcbs()`**
  - Initializes the `pcbFree_h` list by adding all elements from the static array
    `pcbFree_table` (size `MAXPROC`). This is called once during system initialization.
- **`pcb_t* allocPcb()`**
  - Returns `NULL` if `pcbFree_h` is empty. Otherwise, it removes a PCB from the free
    list, initializes all fields to `NULL` or `0` (resetting pointers and state), assigns a new
    PID, and returns the pointer. This ensures no stale data persists.
- **`void freePcb(pcb_t* p)`**
  - Inserts the PCB pointed to by `p` back onto the `pcbFree_h` list, making it available
    for future allocation.
- **`void insertProcQ(struct list_head* head, pcb_t* p)`**
  - Inserts PCB `p` into the queue `head` based on priority (`p_prio`). The list is ordered
    by descending priority (higher integer = higher priority). If priorities are equal, `p`
    is inserted after existing elements of the same priority (FIFO for equal priority).
- **`pcb_t* removeProcQ(struct list_head* head)`**
  - Removes and returns the first PCB (highest priority) from the queue `head`.
    Returns `NULL` if the queue is empty.
- **`pcb_t* outProcQ(struct list_head* head, pcb_t* p)`**
  - Removes a specific PCB `p` from the queue `head`. If `p` is not in the queue, it
    returns `NULL`. It scans the list to confirm `p`'s presence before removal.
- **`void insertChild(pcb_t* prnt, pcb_t* p)`**
  - Establishes a parent-child relationship. Sets `p->p_parent = prnt` and adds `p` to
    `prnt`'s child list (`p_child`) via the sibling list `p_sib`.
- **`pcb_t* removeChild(pcb_t* p)`**
  - Removes the first child of PCB `p`. It unlinks the child from the sibling list, clears
    its parent pointer, and returns the child. Returns `NULL` if `p` has no children.

### 3.2. ASL Manager (phase1/asl.c)

This module implements the Active Semaphore List (ASL). It manages semaphores
(represented by integer addresses `s_key`) and the processes blocked on them. The ASL is
a sorted linked list of active semaphore descriptors (`semd_t`). Active means at least one
process is waiting on that semaphore.

**Key Functions:**

- **`void initASL()`**
  - Initializes the `semdFree_h` (free descriptors) and `semd_h` (active list). Populates
    `semdFree_h` with all static descriptors from `semd_table`.
- **`int insertBlocked(int* semAdd, pcb_t* p)`**
  - Inserts PCB `p` into the queue of the semaphore with key `semAdd`:
    1. Searches the ASL for a descriptor with key `semAdd`.
    2. If found, appends `p` to its process queue.
    3. If not found, allocates a new `semd_t` from `semdFree_h`, initializes it with
       `semAdd`, inserts it into the sorted ASL, and then appends `p`.
    4. Returns `TRUE` if allocation fails (no free descriptors), otherwise `FALSE`.
- **`pcb_t* removeBlocked(int* semAdd)`**
  - Searches the ASL for `semAdd`. If found, removes the head PCB from that
    semaphore's queue. If the queue becomes empty, the semaphore descriptor is
    removed from the ASL and returned to the free list. Returns the unblocked PCB
    or `NULL` if not found.
- **`pcb_t* outBlocked(pcb_t* p)`**
  - Removes a specific PCB `p` from the semaphore queue it is currently blocked on
    (indicated by `p->p_semAdd`). Logic handles descriptor deallocation if the queue
    becomes empty. Returns `p` or `NULL` on error.
- **`pcb_t* headBlocked(int* semAdd)`**
  - Returns the PCB at the head of the queue for semaphore `semAdd` without
    removing it. Returns `NULL` if the semaphore is not active or the queue is empty.

**Helper Functions:**

- **`static inline semd_t* getSemdFromContainer(int* key)`**
  - Extract a semaphore from its list container and returns its pointer. Relies on the
    `container_of` list macro.
- **`static semd_t* getSemdByKey(int* key)`**
  - Search for a semaphore descriptor in the ASL by its key. If found return a pointer
    to the semaphore, else return `NULL`.
- **`static inline void freeSemd(semd_t *sem)`**
  - Insert the semaphore `sem` at the head of the `semdFree_h` list.
- **`static inline semd_t* allocSemd()`**
  - Allocate a semaphore descriptor from the `semdFree_h` list. Return a pointer to
    the allocated semaphore, or `NULL` if the free list is empty.
- **`static inline void initSemd(semd_t *sem, int *key)`**
  - Initialize a semaphore descriptor values with selected key, by setting its `s_key` to
    `key` and initializing its process queue.

## 4. Module Reference: Phase 2

Phase 2 implements the Nucleus (kernel) of PandOSsh. It is composed of four modules:
Initialization, Scheduler, Exception Handling, and Interrupt Handling. Together they provide
process lifecycle management, preemptive round-robin scheduling, system call services
(NSYS1-10), and device I/O through interrupt-driven semaphores.

### 4.1. Global Nucleus State

The following global variables, declared in `init.c` and shared via `extern` declarations in
module headers, represent the entire kernel state:

| Variable | Type | Description |
|---|---|---|
| `processCount` | `int` | Total number of processes in the system (ready, running, or blocked). |
| `softBlockCount` | `int` | Number of processes blocked on device or pseudo-clock semaphores (not user semaphores). |
| `readyQueue` | `struct list_head` | Priority-ordered queue of processes ready to execute. FIFO in case of equal priorities. |
| `currentProcess` | `pcb_t *` | Pointer to the currently running process, or `NULL` if no process is running (e.g., during scheduling). |
| `deviceSemaphores[49]` | `int[]` | One semaphore per device sub-device (48 entries) plus one for the pseudo-clock timer (last index). |

The following global variables are utility variables used in Phase 2 modules.

| Variable | Type | Description |
|---|---|---|
| `rootProcess` | `pcb_t *` | Pointer to the root process (the first process created at boot). Used to search the entire process tree by PID. |
| `startTOD` | `cpu_t` | Time Of Day clock snapshot taken when the current process was dispatched; used for CPU time accounting. |

### 4.2. Initialization (phase2/init.c)

The `main()` function is the nucleus entry point, called by the BIOS after hardware reset.

**Key Functions:**

- **`int main()`**
  - Performs the following steps in order:
    1. **Pass Up Vector setup**: Writes the addresses of `exceptionHandler` and
       `uTLB_RefillHandler` into the Pass Up Vector at `PASSUPVECTOR`, along with
       `KERNELSTACK` as the exception stack pointer. This tells the BIOS where to transfer
       control on any exception.
    2. **Phase 1 initialization**: Calls `initPcbs()` and `initASL()` to set up the free PCB pool
       and Active Semaphore List.
    3. **Global variable initialization**: Sets `processCount` and `softBlockCount` to 0,
       initializes `readyQueue` as empty, sets `currentProcess` to `NULL`, and zeroes all 49
       device semaphores.
    4. **Interval Timer**: Loads the system-wide Interval Timer with `PSECOND` (100ms) via
       `LDIT()`. This starts the pseudo-clock, which will fire periodic interrupts.
    5. **First process creation**: Allocates a PCB, sets its PC to the `test()` function address,
       sets SP to `RAMTOP`, enables all interrupts (`MIE_ALL`), sets kernel mode with
       interrupts enabled on return (`MSTATUS_MPIE_MASK | MSTATUS_MPP_M`), stores it
       in `rootProcess`, places it on the ready queue, and increments `processCount`.
    6. **Scheduler invocation**: Calls `scheduler()`, which dispatches the first process. Control
       never returns to `main()`.

### 4.3. Scheduler (phase2/scheduler.c)

This module implements preemptive round-robin scheduling with a 5ms time slice
(`TIMESLICE`). It is called whenever the current process blocks, yields, terminates, or
has its time slice expire.

**Key Functions:**

- **`void scheduler()`**
  - Examines the ready queue and system state to decide the next action:
    1. **Ready queue non-empty**: Removes the highest-priority process from `readyQueue`,
       sets it as `currentProcess`, records the current TOD clock into `startTOD` (for CPU
       time accounting), loads the Processor Local Timer (PLT) with `TIMESLICE`, and
       dispatches the process via `LDST(&currentProcess->p_s)`.
    2. **Ready queue empty, `processCount == 0`**: No processes exist in the system. Calls
       `HALT()` to stop the machine.
    3. **Ready queue empty, `softBlockCount > 0`**: All remaining processes are blocked
       waiting for I/O or the pseudo-clock. Enters a **Wait State**: enables all interrupts
       except the PLT timer (`MIE_ALL & ~MIE_MTIE_MASK`), enables global interrupts in
       the STATUS register, and calls `WAIT()`. The PLT is excluded to prevent false
       time-slice interrupts while no process is running.
    4. **Ready queue empty, `softBlockCount == 0`**: Deadlock detected (processes exist but
       none can make progress). Calls `PANIC()`.

### 4.4. Exception Handling (phase2/exceptions.c)

This module is the main exception dispatcher. The BIOS saves the interrupted processor
state into the BIOS Data Page (accessible via `GET_EXCEPTION_STATE_PTR(0)`) and
transfers control to `exceptionHandler()`.

**Key Functions:**

- **`void exceptionHandler()`**
  - Reads the `cause` register from the saved exception state and dispatches to the
    appropriate handler:

    | Cause | Handler |
    |---|---|
    | Interrupt (bit 31 set) | `InterruptHandler()` |
    | SYSCALL from User mode (`EXC_ECU=8`) or Machine mode (`EXC_ECM=11`) | `syscallExceptionHandler()` |
    | TLB exceptions (`EXC_MOD` through `EXC_UTLBS`) | `tlbExceptionHandler()` |
    | All other exception codes | `programTrapHandler()` |

- **`void syscallExceptionHandler()`**
  - Handles the SYSCALL exception by examining register `a0` for the syscall number:
    - **User mode + negative syscall number**: Privileged operation from user mode.
      Simulates a `PRIVINSTR` program trap (sets cause, calls `programTrapHandler()`).
    - **Non-negative syscall number** (any mode): Not a nucleus syscall. Passes to the
      support level via `passUpOrDie(GENERALEXCEPT)`.
    - **Kernel mode + negative syscall number**: Nucleus syscall. Increments PC by
      `WORDLEN` (to skip past the SYSCALL instruction), extracts arguments from registers
      `a1`, `a2`, `a3`, and dispatches to the appropriate NSYS handler via a switch statement.
- **`void passUpOrDie(int i)`**
  - Implements the "pass up or die" mechanism for exceptions the nucleus cannot handle:
    - If `currentProcess->p_supportStruct == NULL`: The process has no support-level
      handler. Calls `terminateProcess(0)` to kill it.
    - Otherwise: Copies the exception state into `sup_exceptState[i]` and transfers control
      to the support-level handler via `LDCXT()` using the context stored in
      `sup_exceptContext[i]`.
  - The index `i` is `PGFAULTEXCEPT` (0) for TLB exceptions or `GENERALEXCEPT` (1) for
    program traps and support-level syscalls.
- **`void programTrapHandler()`** / **`void tlbExceptionHandler()`**
  - Wrappers that call `passUpOrDie()` with the appropriate exception type index.

### 4.5. Interrupt Handling (phase2/interrupts.c)

This module handles all hardware interrupts. The uRISCV `cause` register encodes the
interrupt number in bits 30:0 when bit 31 is set.

**Key Functions:**

- **`void InterruptHandler()`**
  - Processes interrupts by priority:
    1. **Processor Local Timer (`IL_CPUTIMER=7`)** - Highest priority.
       Acknowledges by reloading the PLT with `TIMESLICE * TIMESCALE`.
       Saves the interrupted process state into its PCB via `copyState()`.
       Updates accumulated CPU time: `p_time += currentTOD - startTOD`.
       Places the process back on `readyQueue`.
       Sets `currentProcess = NULL` and calls `scheduler()`.
    2. **Interval Timer (`IL_TIMER=3`)** - Pseudo-clock tick.
       Acknowledges by reloading with `PSECOND` (100ms) via `LDIT()`.
       Performs a **V-all**: unblocks *every* process waiting on
       `deviceSemaphores[PSEUDOCLOCK_SEM]` (index 48), decrementing `softBlockCount`
       for each.
       Resets the pseudo-clock semaphore to 0.
       Returns to the interrupted process via `LDST()` if one exists, otherwise calls
       `scheduler()`.
    3. **External Device Interrupts (`IL_DISK=17` through `IL_TERMINAL=21`)**.
       Converts the interrupt number to a line number: `intLineNo = intNo - 14`.
       Reads the **Interrupting Devices Bit Map** at `0x10000040` to identify which device
       on that line caused the interrupt.
       Selects the lowest-numbered (highest priority) interrupting device and calls
       `handleDeviceInterrupt()`.

**Helper Functions:**

- **`static void handleDeviceInterrupt(state_t *oldState, int intLineNo, int devNo)`**
  - Handles a single device interrupt:
    1. Calculates the device register address: `START_DEVREG + (intLineNo-3)*0x80 + devNo*0x10`.
    2. For **terminal devices** (line 7): checks transmit status first (higher priority). If
       transmit has a pending interrupt, saves its status and ACKs; otherwise handles receive.
    3. For **non-terminal devices**: reads status and sends ACK command.
    4. Performs a **V operation** on the corresponding device semaphore: increments the
       semaphore, removes the blocked process (if any), stores the device status in the
       unblocked process's `reg_a0` (as return value for `doIO`), places it on `readyQueue`,
       and decrements `softBlockCount`.
    5. Returns to the interrupted process via `LDST()` or calls `scheduler()` if no process
       was running.

#### Device Semaphore Layout

The 49-entry `deviceSemaphores` array maps to devices as follows:

| Index | Device Type | Interrupt Line |
|---|---|---|
| 0-7 | Disk | 3 |
| 8-15 | Flash | 4 |
| 16-23 | Ethernet | 5 |
| 24-31 | Printer | 6 |
| 32-39 | Terminal transmit | 7 |
| 40-47 | Terminal receive | 7 |
| 48 | Pseudo-clock | (Interval Timer) |

Formula for non-terminal devices: `semIndex = (intLineNo - 3) * 8 + devNo`
Terminal transmit: `semIndex = 32 + devNo`
Terminal receive: `semIndex = 40 + devNo`

### 4.6. System Calls (phase2/syscalls.c)

This module implements the ten nucleus system calls (NSYS1-10). All syscalls follow the
same pattern: the BIOS saves the caller's state to the BIOS Data Page before invoking
the exception handler. Syscall arguments are passed in registers `a1`-`a3`, and return
values are placed in `a0` of the saved exception state before returning via `LDST()`.

Blocking syscalls (Passeren, DoIO, WaitForClock) do not return to the caller immediately;
instead they call `scheduler()` to dispatch another process.

**Key Functions:**

- **`void createProcess(state_t *statep, int prio, support_t *supportp)`** - NSYS1
  - Creates a new process as a child of the current process:
    1. Allocates a PCB via `allocPcb()`. Returns -1 in `a0` if allocation fails.
    2. Copies the provided processor state into the new PCB.
    3. Sets the new process's priority and support structure.
    4. Inserts it as a child of `currentProcess` and places it on the ready queue.
    5. Returns the new PID in the caller's `a0`.
- **`void terminateProcess(int pid)`** - NSYS2
  - Terminates a process and all its descendants:
    1. If `pid == 0`, targets the current process. Otherwise, searches the entire process
       tree starting from `rootProcess` for a process with the given PID.
    2. Determines whether the current process will die (either it is the target or a
       descendant of it).
    3. If the current process dies, updates its accumulated CPU time before termination.
    4. Removes the target from its parent's child list via `outChild()`.
    5. Calls `terminateRecursive()` to recursively free the entire subtree.
    6. If the current process died, sets `currentProcess = NULL` and calls `scheduler()`.
       Otherwise, returns control to the caller via `LDST()`.
- **`void passeren(int *semaddr)`** - NSYS3
  - Performs a P (wait) operation on a semaphore:
    1. Decrements `*semaddr`.
    2. If the result is negative, the process must block: saves the current process state
       via `updateCurrentProcessState()`, inserts it into the semaphore's blocked queue,
       and calls `scheduler()`.
    3. If the result is non-negative, the process continues: returns via `LDST()`.
- **`void verhogen(int *semaddr)`** - NSYS4
  - Performs a V (signal) operation on a semaphore:
    1. Increments `*semaddr`.
    2. If the result is less than or equal to 0, there was a blocked process: removes it from
       the semaphore queue and places it on the ready queue.
    3. Returns control to the caller via `LDST()`.
- **`void doIO(int *commandAddr, int commandValue)`** - NSYS5
  - Initiates an I/O operation and blocks the caller until completion:
    1. Calculates the device semaphore index from `commandAddr` using the device register
       memory layout: `offset = cmdAddr - START_DEVREG`, `intLineNo = offset / 0x80 + 3`,
       `devNo = (offset % 0x80) / 0x10`. For terminals, distinguishes transmit
       (`offset % 0x10 == 0x0C`) from receive.
    2. Saves the current process state and performs a P operation on the device semaphore.
    3. Increments `softBlockCount`.
    4. Writes `commandValue` to the device's command register to initiate the I/O.
    5. Sets `currentProcess = NULL` and calls `scheduler()`.
  - The interrupt handler will later V the semaphore when the device completes, unblocking
    the process and placing the device status in its `a0` register.
- **`void getCPUTime()`** - NSYS6
  - Returns the total CPU time consumed by the current process:
    1. Reads the current TOD clock.
    2. Computes total time as `p_time + (currentTOD - startTOD)` (accumulated time plus
       time in the current quantum).
    3. Places the result in the caller's `a0` and returns via `LDST()`.
- **`void waitForClock()`** - NSYS7
  - Blocks the current process until the next pseudo-clock tick (100ms interval):
    1. Saves the current process state.
    2. Performs a P operation on `deviceSemaphores[48]` (the pseudo-clock semaphore).
    3. Increments `softBlockCount`.
    4. Calls `scheduler()`.
  - Every 100ms, the Interval Timer interrupt handler unblocks all processes waiting on
    this semaphore (V-all operation).
- **`void getSupportData()`** - NSYS8
  - Returns the current process's support structure pointer in `a0` via `LDST()`.
- **`void getProcessID(int parent)`** - NSYS9
  - Returns a PID in `a0`:
    - If `parent == 0`: returns the current process's PID.
    - If `parent != 0`: returns the parent's PID, or 0 if the current process is the root.
- **`void yield()`** - NSYS10
  - Relinquishes the CPU voluntarily:
    - If the ready queue is empty (only process), continues running via `LDST()`.
    - Otherwise, saves the current process state, places it at the tail of the ready queue,
      and calls `scheduler()`. Per spec, the process is not immediately re-executed even if
      it has the highest priority.

**Helper Functions:**

- **`static inline void updateCurrentProcessState()`**
  - Copies the BIOS-saved exception state into `currentProcess->p_s` via `copyState()`.
  - Updates accumulated CPU time: `p_time += currentTOD - startTOD`.
  - Called before any blocking syscall to preserve the caller's state in its PCB.
- **`static void terminateRecursive(pcb_t *proc)`**
  - Recursively terminates all children first (depth-first).
  - Removes the process from the ready queue (if queued) or from its semaphore (if blocked).
  - If the process was blocked on a device semaphore, adjusts the semaphore value
    (increment, since the waiter is being removed) and decrements `softBlockCount`.
  - Frees the PCB and decrements `processCount`.
- **`static pcb_t *findProcessByPid(pcb_t *root, int pid)`**
  - Recursively searches the process tree rooted at `root` for a process with the given PID.
  - Returns a pointer to the matching PCB, or `NULL` if not found.

## 5. Module Reference: Phase 3

Phase 3 implements the Support Level on top of the Nucleus. It adds virtual
memory, user programs, an interactive shell, and four user-callable services.
It is composed of three modules:

- `phase3/initProc.c` - the InstantiatorProcess, which replaces the Phase 2 `test`.
  Initialises shared resources (swap pool, semaphores, per-U-proc Support
  structures) and starts the shell.
- `phase3/vmSupport.c` - the Pager, the TLB-Refill handler, the swap pool table,
  and the flash I/O helper. Handles all virtual memory work.
- `phase3/sysSupport.c` - the Support-level general exception handler, the four
  user-callable services (SYS2, SYS4, SYS5, SYS6), and the orderly termination
  path (`programTrap`).

Each U-proc lives in `kuseg` (`0x80000000` ... `0xC0000000`), has its own
32-entry private page table, and is backed by its own flash device (one per ASID
in the range `1..UPROCMAX`). ASID 1 is reserved for the shell.

### 5.1. Shared State

The following global variables are declared in `phase3/initProc.c` and shared
across all three modules via `extern` declarations in
`phase3/headers/initProc.h`:

| Variable | Type | Description |
|---|---|---|
| `supports[UPROCMAX]` | `support_t[]` | One Support structure per U-proc. Holds the page table and the two exception contexts (one for TLB exceptions, one for general exceptions). |
| `uprocStates[UPROCMAX]` | `state_t[]` | Initial processor state for each U-proc. Loaded by `SYS6 EXECUTE` when the process is created. |
| `masterSem` | `int` | Synchronisation semaphore, initial value 0. V'd by the shell when it exits; P'd by `InstantiatorProcess` so the kernel halts only after the shell terminates. |
| `shellSem` | `int` | Synchronisation semaphore, initial value 0. V'd by any non-shell U-proc when it exits; P'd by the shell after each `SYS6 EXECUTE` so the shell waits for the child. |
| `termRdSem` | `int` | Mutex semaphore for terminal-0 reads, initial value 1. |
| `termWrSem` | `int` | Mutex semaphore for terminal-0 writes, initial value 1. |
| `flashSem[UPROCMAX]` | `int[]` | One mutex semaphore per flash device, each initial value 1. |

The swap-pool table is declared in `phase3/vmSupport.c`:

| Variable | Type | Description |
|---|---|---|
| `swapPool[POOLSIZE]` | `swap_t[]` | 16 entries, one per swap-pool frame. Records the `(ASID, page number, PTE pointer)` triple currently held in each frame. |
| `swapPoolSem` | `int` | Mutex semaphore for the swap pool, initial value 1. Held across the swap-pool walk and the surrounding TLB invalidation. |

### 5.2. Initialization (phase3/initProc.c)

*This section is a placeholder. Full documentation will be added during the
review of `phase3/initProc.c`.*

The `test()` function is the Phase 3 entry point and replaces the Phase 2 test
program. At boot it initialises the swap pool, all support-level semaphores,
and the per-U-proc Support structures, then creates the shell U-proc (ASID 1)
and waits on `masterSem` until the shell terminates. The other U-procs (ASIDs
2..`UPROCMAX`) are launched on demand by the shell through `SYS6 EXECUTE`.

### 5.3. Virtual Memory Support (phase3/vmSupport.c)

*This section is a placeholder. Full documentation will be added during the
review of `phase3/vmSupport.c`.*

This module provides three pieces of functionality:

- `uTLB_RefillHandler()` - runs on every TLB miss whose target PTE is already
  valid. Copies the PTE into the TLB and resumes the U-proc.
- `pager()` - runs on TLB-Invalid exceptions. Fetches the missing page from the
  U-proc's flash device into a swap-pool frame, evicting another frame if
  necessary. Serialises access to the swap pool with `swapPoolSem`.
- `flashOp()` - wraps the flash device-register protocol for single-block read
  and write operations.

### 5.4. Support-Level Exception Handler (phase3/sysSupport.c)

This module is the entry point for every non-TLB exception the Nucleus passes
up to a U-proc with a non-`NULL` `p_supportStruct`. It handles the four
user-callable services and the orderly termination path. All functions in this
file run on the U-proc's `sup_stackGen` stack, in kernel mode with interrupts
enabled.

**Key Functions:**

- **`void supportGeneralHandler()`**
  - Entry point referenced by `sup_exceptContext[GENERALEXCEPT].pc`. Recovers
    the Support pointer with `SYS8 GETSUPPORTPTR`, reads the saved exception
    state at `sup->sup_exceptState[GENERALEXCEPT]`, and dispatches:

    | Cause | Action |
    |---|---|
    | `EXC_ECU` (8) - ECALL from U-mode | SYSCALL, dispatch on `a0` |
    | `EXC_ECM` (11) - ECALL from M-mode | SYSCALL, dispatch on `a0` |
    | Anything else | `programTrap()` |

    For SYSCALLs, dispatches on register `a0`:

    | `a0` | Service | Handler |
    |---|---|---|
    | 2 (`TERMINATE`) | SYS2 | `doSys2` |
    | 4 (`WRITETERMINAL`) | SYS4 | `doSys4` |
    | 5 (`READTERMINAL`) | SYS5 | `doSys5` |
    | 6 (`EXECUTE`) | SYS6 | `doSys6` |
    | other | unknown | `programTrap` |

- **`static void doSys2(support_t *sup)`** - SYS2 TERMINATE
  - Orderly termination of the calling U-proc. Delegates to `programTrap()`.

- **`static void doSys4(support_t *sup, state_t *st)`** - SYS4 WRITETERMINAL
  - Writes `a2` characters from the buffer at virtual address `a1` to terminal
    0, under mutual exclusion on `termWrSem`. Rejects (via `programTrap`) any
    length outside `[0, MAXSTRLENG]` or any buffer that falls outside `kuseg`.
  - Sends each character by issuing `SYS5 DOIO` with the command word
    `(char << 8) | TRANSMITCHAR`. Stops on the first device error.
  - Returns in `a0`: the number of characters written on success, or the
    negated low byte of the device status on error.

- **`static void doSys5(support_t *sup, state_t *st)`** - SYS5 READTERMINAL
  - Reads characters one at a time from terminal 0 into the buffer at virtual
    address `a1`, under mutual exclusion on `termRdSem`. Stops at a newline
    (`'\n'`) or after `MAXSTRLENG` characters. Rejects (via `programTrap`) any
    buffer that falls outside `kuseg`.
  - Fetches each character by issuing `SYS5 DOIO` with the command word
    `RECEIVECHAR`. The received byte is in bits 15..8 of the returned status;
    the device status is in bits 7..0.
  - Returns in `a0`: the number of characters read on success, or the negated
    low byte of the device status on error.

- **`static void doSys6(support_t *sup, state_t *st)`** - SYS6 EXECUTE
  - Spawns a new U-proc by ASID. Only the shell (ASID 1) may call this; every
    other caller is terminated via `programTrap`. The target ASID must be in
    `[2, UPROCMAX]`.
  - Calls `resetUprocResources(asid)` to clear any leftover state from a
    previous incarnation of that ASID. Rebuilds the initial processor state in
    `uprocStates[asid - 1]`. Asks the Nucleus to create the process with
    `NSYS1 CREATEPROCESS`.
  - P's `shellSem`, blocking the shell until the child terminates and V's the
    same semaphore (via `programTrap`).
  - Returns 0 in `a0` once the child has terminated.

- **`void programTrap(support_t *sup)`**
  - Orderly termination path used on every fatal U-proc error: program traps,
    unknown SYSCALLs, address or length validation failures, and flash errors
    passed up from the Pager.
  - If the dying U-proc is the shell, V's `masterSem` so that
    `InstantiatorProcess` wakes. Otherwise V's `shellSem` so that the shell
    resumes from its `SYS6 EXECUTE` block.
  - Asks the Nucleus to recursively destroy the U-proc and its subtree via
    `NSYS2 TERMPROCESS`.

**Helper Functions:**

- **`static int validUserAddr(memaddr addr)`**
  - Returns 1 if `addr` lies within `[KUSEGSTART, KUSEGEND)`
    (`0x80000000` ... `0xC0000000`), 0 otherwise. Used by SYS4 and SYS5 to
    reject pointers that would let the kernel read or write outside the
    U-proc's address space.

- **`static void resetUprocResources(int asid)`**
  - Clears any swap-pool entries owned by `asid` (under `swapPoolSem`),
    invalidates the TLB, and re-zeroes the `V` (valid) bit on every PTE in
    that ASID's page table.
  - Called by `doSys6` before the new process is created. Without this step,
    the Pager could later see a frame still marked as belonging to a previous
    incarnation of `asid` and write the new process's memory back over the
    previous incarnation's flash image.

## 6. Emulator Configuration Notes

### TLB Floor Address

The `tlb-floor-address` field in `config_machine.json` (and in `phase3_config_machine.json`) is set to `"0x80000000"`:

```json
"tlb-floor-address": "0x80000000"
```

This value defines the boundary below which virtual addresses bypass the TLB and are
translated directly (mapped to physical memory). In uRISCV, address `0x80000000`
is the start of **KSEG0** - the kernel segment where the nucleus code and data are loaded.

By setting the TLB floor to `0x80000000`, all kernel addresses (which live in KSEG0) are
accessed without TLB lookup. Only addresses above this threshold go through TLB translation.
This is essential because the kernel must be able to execute before any TLB entries are set
up - at boot time, during exception handling, and whenever the TLB is being refilled.

The previous default value was `0xFFFFFFFF`, which meant all addresses would go through the
TLB.

### Phase 3 Configuration (`phase3_config_machine.json`)

Phase 3 ships its own emulator configuration, distinct from the one used for
the Phase 1 and Phase 2 standalone tests:

| Field | Value | Reason |
|---|---|---|
| `num-ram-frames` | 256 | OS frames + 16-frame swap pool + headroom |
| `tlb-size` | 16 | Standard µRISCV TLB |
| `tlb-floor-address` | `0x80000000` | Same as Phase 1/2: only `kuseg` goes through the TLB |
| `flash0` ... `flash7` | `testers/{shell,fibEight,echo,fibEleven,uname,date,sl,calc}.uriscv` | One image per ASID 1..8 |
| `terminal0` | `term0.uriscv` (enabled) | Shell prompt and tester I/O |

`flash7` (`calc`) is shipped disabled in the JSON; flip its `enabled` flag to
enable the calculator program from the shell.
