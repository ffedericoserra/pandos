/* SYSCALL exception handling */

#include <uriscv/liburiscv.h>
#include "../headers/types.h"
#include "../headers/const.h"
#include "../headers/listx.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/scheduler.h"
#include "./headers/syscalls.h"

/* Pseudo-clock semaphore index */
#define PSEUDOCLOCK_SEM (SEMDEVLEN - 1)

/* Helper functions declarations */
static void terminateRecursive(pcb_t *proc);
static pcb_t *findProcessByPid(pcb_t *root, int pid);
static void updateCurrentProcessState();


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

    /* Init new process */
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
void terminateProcess(int pid) {     // (NSYS2)
    /* Select target process by pid */
    pcb_t *target;
    if (pid == 0) {
        target = currentProcess;
    } else {
        target = findProcessByPid(rootProcess, pid);

        /* Process not found, return to caller */
        if (target == NULL) {
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
        STCK(currentTOD);
        currentProcess->p_time += (currentTOD - startTOD);
    }

    /* Remove target from parent's children list */
    outChild(target);

    terminateRecursive(target);

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

    if (*semaddr <= 0) { /* Unblock process (aka: insert it in readyQueue) */
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
     * Each device register is 0x10 bytes.
     * Lines 3-7 have 8 devices each, with 0x80 bytes per line.
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
    int intLineNo = offset / 0x80 + 3;      /* Interrupt lines 3-7 */
    int devNo = (offset % 0x80) / 0x10;     /* Device number 0-7 */

    int semIndex;
    if (intLineNo == 7) {
        /* Determine transmit vs receive from offset */
        unsigned int withinDev = offset % 0x10;
        if (withinDev == 0x0C) {
            /* transm_command */
            semIndex = (4 * DEVPERINT) + devNo;                 /* 32 + devNo */
        } else {
            /* recv_command */
            semIndex = (4 * DEVPERINT) + devNo + DEVPERINT;     /* 40 + devNo */
        }
    } else {
        semIndex = (intLineNo - 3) * DEVPERINT + devNo;
    }

    /* Block current process */
    updateCurrentProcessState();

    /* P on device semaphore */
    deviceSemaphores[semIndex]--;           // TODO: passeren(deviceSemaphores[semIndex]);
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
    /* P on pseudo-clock semaphore */
    updateCurrentProcessState();
    deviceSemaphores[PSEUDOCLOCK_SEM]--;            // TODO: passeren(deviceSemaphores[PSEUDOCLOCK_SEM]);
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
 */
void getProcessID(int parent) {
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);

    if (parent == 0) {
        exceptionState->reg_a0 = currentProcess->p_pid;
    } else {
        if (currentProcess->p_parent == NULL) {
            exceptionState->reg_a0 = 0;         /* Root has not parent */       // TODO: Valutare se valorizzare a 0 (non specificiato nelle specifiche)
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
        list_add_tail(&currentProcess->p_list, &readyQueue);        // TODO: forse su pcb.c ho funzione dedicata?
        currentProcess = NULL;
        scheduler();
    }
}


/* ------------------------ HELPERS ------------------------ */

/*
 * Copy the saved exception state into the current process PCB
 * and update accumulated CPU time.
 */
static void updateCurrentProcessState() {
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
    if (root == NULL) return NULL;
    if (root->p_pid == pid) return root;

    /* Search children */
    pcb_t *child;
    struct list_head *iter;
    list_for_each(iter, &root->p_child) {
        child = container_of(iter, pcb_t, p_sib);
        pcb_t *found = findProcessByPid(child, pid);
        if (found != NULL) return found;        // TODO: forse posso evitare check found != NULL
    }
    return NULL;
}

/* void terminateCurrentAndChild(pcb_t *proc) {
    while (!emptyChild(proc)) {
        pcb_t *p_child = removeChild(proc);
        terminateCurrentAndChild(p_child);
    }

    outProcQ(&readyQueue, proc);
    processCount--;
} */