/* SYSCALL exception handling */

#include <uriscv/liburiscv.h>
#include "../headers/types.h"
#include "../headers/const.h"
#include "../headers/utils.h"
#include "../headers/listx.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/scheduler.h"
#include "./headers/syscalls.h"

#define PSEUDOCLOCK_SEM      (SEMDEVLEN - 1)            /* Pseudo-clock semaphore index */

/* Device register layout constants */
#define DEV_REG_SIZE    0x10    /* Size of a single device register (16 bytes) */
#define DEV_LINE_SIZE   0x80    /* Size of all device registers on one interrupt line (8 * 0x10) */
#define TERM_LINE_NO    7       /* Interrupt line number for terminal devices */
#define TRANSM_CMD_OFF  0x0C    /* Offset of transm_command within a terminal device register */
#define TERM_SEM_START  (4 * DEVPERINT)  /* First semaphore index for terminal devices (32) */

static void terminateRecursive(pcb_t *proc);
static pcb_t *findProcessByPid(pcb_t *root, int pid);
static inline void updateCurrentProcessState();


/*
 * NSYS1 - CreateProcess
 * Creates a new process as a child of the current process.
 * Returns new PID in caller's a0, or -1 on failure.
 */
void createProcess(state_t *statep, int prio, support_t *supportp) {
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);
    
    pcb_t* newProc = allocPcb();
    if (newProc == NULL) {
        exceptionState->reg_a0 = -1;
        LDST(exceptionState);
        return;
    }

    /* Initialize new process */
    copyState(&newProc->p_s, statep);
    newProc->p_prio = prio;
    newProc->p_supportStruct = supportp;

    /* Make it a child of current process */
    insertChild(currentProcess, newProc);

    /* Place it on readyQueue */
    insertProcQ(&readyQueue, newProc);
    processCount++;

    /* Return the new PID to the caller */
    exceptionState->reg_a0 = newProc->p_pid;
    LDST(exceptionState);
}

/*
 * NSYS2 - TerminateProcess
 * Terminates the current process (if pid==0) or the process with the given PID.
 * Recursively terminates all progeny. Calls the scheduler afterwards.
 */
void terminateProcess(int pid) {
    /* Select target process by pid */
    pcb_t *target;
    if (pid == 0) {
        target = currentProcess;
    } else {
        target = findProcessByPid(rootProcess, pid);

        if (target == NULL) { /* Process not found, return to caller */
            state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);
            LDST(exceptionState);
            return;
        }
    }

    /* Check if the current process is the target or a descendant of the target.
     * If so, we need to call the scheduler after termination. */
    int currentDies = (target == currentProcess);
    if (!currentDies) {
        pcb_t *ancestor = currentProcess->p_parent;
        while (ancestor != NULL) {
            if (ancestor == target) {
                currentDies = 1;
                break;
            }
            ancestor = ancestor->p_parent;
        }
    }

    /* Update CPU time if current process is being terminated */
    if (currentDies) {
        cpu_t currentTOD;
        STCK(currentTOD);   /* Read TOD clock and store its value in currentTOD */
        currentProcess->p_time += (currentTOD - startTOD); /* CPU time passed since last dispatch */
    }

    /* Remove target from parent's children list */
    outChild(target);

    terminateRecursive(target); /* Terminate process and its progeny */

    if (currentDies) {
        currentProcess = NULL;
        scheduler();
    } else { /* current process surivives, return to the caller */
        state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);
        LDST(exceptionState);
    }
}

/*
 * NSYS3 - Passeren (P operation)
 * Decrements the semaphore. If the result is < 0, the process is blocked.
 */
void passeren(int *semaddr) {
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);

    (*semaddr)--;

    if (*semaddr < 0) { /* Block the current process on this semaphore */
        updateCurrentProcessState();
        insertBlocked(semaddr, currentProcess);
        currentProcess = NULL;
        scheduler();
    } else { /* return to caller */
        LDST(exceptionState);
    }
}                    

/*
 * NSYS4 - Verhogen (V operation)
 * Increments the semaphore. If there's a blocked process, unblock it.
 */
void verhogen(int *semaddr) {
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);

    (*semaddr)++;

    if (*semaddr <= 0) { /* Unblock process */
        pcb_t *unblocked = removeBlocked(semaddr);
        if (unblocked != NULL) {
            insertProcQ(&readyQueue, unblocked);
        }
    }

    LDST(exceptionState);
}

/*
 * NSYS5 - DoIO
 * Initiates an I/O operation and blocks the current process.
 * commandAddr points to the device's command register.
 * commandValue is the command to write.
 */
void doIO(int *commandAddr, int commandValue) {
    /* Determine which device semaphore corresponds to this command address.
     * Device registers start at START_DEVREG (0x10000054).
     * Each device register is DEV_REG_SIZE (0x10) bytes.
     * Lines 3-7 have 8 devices each, with DEV_LINE_SIZE (0x80) bytes per line.
     *
     * For non-terminal devices, the command field is at offset 0x04.
     * For terminal devices:
     *   recv_command is at offset 0x04
     *   transm_command is at offset 0x0C
     */
    memaddr cmdAddr = (memaddr)commandAddr;

    /* Calculate the device register base address from the command address.
     * Non-terminal: command at base + 0x04, so base = cmdAddr - 0x04
     * Terminal recv_command:   base + 0x04, so base = cmdAddr - 0x04
     * Terminal transm_command: base + 0x0C, so base = cmdAddr - 0x0C
     */
    unsigned int offset = cmdAddr - START_DEVREG;
    int intLineNo = offset / DEV_LINE_SIZE + 3;              /* Interrupt lines 3-7 */
    int devNo = (offset % DEV_LINE_SIZE) / DEV_REG_SIZE;     /* Device number 0-7 */

    int semIndex;
    if (intLineNo == TERM_LINE_NO) {
        /* Determine transmit vs receive from offset */
        unsigned int withinDevOffset = offset % DEV_REG_SIZE;
        if (withinDevOffset == TRANSM_CMD_OFF) { /* transmit */
            semIndex = TERM_SEM_START + devNo;
        } else {                                 /* receive */
            semIndex = TERM_SEM_START + DEVPERINT + devNo;
        }
    } else {
        semIndex = (intLineNo - 3) * DEVPERINT + devNo;
    }

    /* P on device semaphore.
     * Different logic from `passeren()`: always block unconditionally, must write command
     * to device register before calling scheduler, and must increment softBlockCount. */
    updateCurrentProcessState(); 
    deviceSemaphores[semIndex]--;
    insertBlocked(&deviceSemaphores[semIndex], currentProcess);
    softBlockCount++;

    /* Initiate I/O by writing the command to the device register */
    *commandAddr = commandValue;

    currentProcess = NULL;
    scheduler();
} 

/*
 * NSYS6 - GetCPUTime
 * Returns accumulated CPU time (in microseconds) for the current process.
 */
void getCPUTime() {
   state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);

   cpu_t currentTOD;
   STCK(currentTOD);

   /* Return total time: accumulated + time in current quantum */
   exceptionState->reg_a0 = currentProcess->p_time + (currentTOD - startTOD);
   LDST(exceptionState);
} 

/*
 * NSYS7 - WaitForClock
 * Blocks the current process on the pseudo-clock semaphore.
 */
void waitForClock() {
    /* P on pseudo-clock semaphore.
     * Different logic from `passeren()`: always block unconditionally,
     * and must increment softBlockCount. */
    updateCurrentProcessState();
    deviceSemaphores[PSEUDOCLOCK_SEM]--;
    insertBlocked(&deviceSemaphores[PSEUDOCLOCK_SEM], currentProcess);
    softBlockCount++;

    currentProcess = NULL;
    scheduler();
} 

/*
 * NSYS8 - GetSupportData
 * Returns a pointer to the current process's support structure.
 */
void getSupportData() {
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);
    exceptionState->reg_a0 = (unsigned int)currentProcess->p_supportStruct;
    LDST(exceptionState);
}

/*
 * NSYS9 - GetProcessID
 * Returns the PID of the current process (if parent==0) or
 * the PID of the parent process (if parent!=0).
 * Note: `parent` is just a flag.
 */
void getProcessID(int parent) {
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);

    if (parent == 0) {
        exceptionState->reg_a0 = currentProcess->p_pid;
    } else {
        if (currentProcess->p_parent == NULL) {
            exceptionState->reg_a0 = 0;         /* Root has no parent */
        } else {
            exceptionState->reg_a0 = currentProcess->p_parent->p_pid;
        }
    }

    LDST(exceptionState);
}

/*
 * NSYS10 - Yield
 * Relinquishes the CPU. The process is placed at the back of the ready queue.
 * If it's the only ready process, it continues running.
 */
void yield() {
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);

    if (emptyProcQ(&readyQueue)) { /* it's the only process: continue running */
        LDST(exceptionState);
    } else {
        updateCurrentProcessState();
        /* We are using list_add_tail instead of insertProcQ to ensure the process
         * is placed at the back of the queue regardless of priority. */
        list_add_tail(&currentProcess->p_list, &readyQueue);  
        currentProcess = NULL;
        scheduler();
    }
}


/* ------------------------ HELPERS ------------------------ */

/*
 * Copy the saved exception state into the current process PCB
 * and update accumulated CPU time.
 */
static inline void updateCurrentProcessState() {
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);
    copyState(&currentProcess->p_s, exceptionState);

    cpu_t currentTOD;
    STCK(currentTOD);
    currentProcess->p_time += (currentTOD - startTOD);
}

/*
 * Recursively terminate a process and all its progeny.
 * Handles removing from ready queue or unblocking from semaphores.
 */
static void terminateRecursive(pcb_t *proc) {
    /* Recursively terminate all children first */
    while (!emptyChild(proc)) {
        pcb_t *child = removeChild(proc);
        terminateRecursive(child);
    }

    /* Remove from ready queue (if it's there) */
    if (outProcQ(&readyQueue, proc) != NULL) {
        /* Was on the ready queue */
    }
    /* Remove from semaphore (if blocked) */
    else if (proc->p_semAdd != NULL) {
        /* Save semaphore address before outBlocked clears it */
        int *savedSemAddr = proc->p_semAdd;
        outBlocked(proc);
        /* Adjust semaphore value since we're removing a blocked process */
        (*savedSemAddr)++;
        /* If blocked on a device semaphore, adjust soft-block count */
        if (savedSemAddr >= &deviceSemaphores[0] &&
            savedSemAddr <= &deviceSemaphores[SEMDEVLEN - 1]) {
            softBlockCount--;
        }
    }

    freePcb(proc);
    processCount--;
}

/*
 * Find a process by PID in the process tree rooted at `root`.
 * Returns NULL if not found.
 */
static pcb_t *findProcessByPid(pcb_t *root, int pid) {
    if (root == NULL)       return NULL;
    if (root->p_pid == pid) return root;

    /* Search children */
    pcb_t *child;
    struct list_head *iter;
    list_for_each(iter, &root->p_child) {
        child = container_of(iter, pcb_t, p_sib);
        pcb_t *found = findProcessByPid(child, pid);
        if (found != NULL) return found;
    }
    return NULL;
}
