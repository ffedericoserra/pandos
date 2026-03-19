/* SYSCALL exception handling */

/* 
  ACCESS TO REGISTERS:
    #define reg_a0 gpr[24]
    #define reg_a1 gpr[25]
    #define reg_a2 gpr[26]
    #define reg_a3 gpr[27]
    #define reg_a4 gpr[28]
*/

#include <uriscv/types.h>
#include "../headers/types.h"
#include "../headers/const.h"
// #include "../headers/listx.h"

#include "../phase1/headers/asl.h"
#include "../phase1/headers/pcb.h"

static void terminateCurrentAndChild(pcb_t *proc);

/** 
  * When requested, this service causes a new process, said to be a progeny of the caller, to be created.
  * a1 should contain a pointer to a processor state (state_t *). This processor state is to be used as
  * 1The CAUSE_IS_INT macro checks if the most significant bit of the parameter is 1, if the most significant bit of the
  * cause register is 1 it means that the exception was caused by an interrupt.
  * 5
  * the initial state for the newly created process. The process requesting the NSYS1 service continues
  * to exist and to execute. If the new process cannot be created due to lack of resources (e.g. no more
  * free PCBs), an error code of -1 is placed/returned in the caller’s a0, otherwise, return the process id
  * of the newly created process in the caller’s a0. Good design calls for tight/strong cohesion and loose
  * coupling between modules/classes/OS Levels, etc. Level 2 implements PCBs, and Level 3 utilizes
  * queues of PCBs to create a basic multiprogramming environment. However, it is the Support Level
  * that handles address translation as well as all exceptions beyond I/O interrupts and the first eight
  * system calls (and then, only if in kernel-mode). The design question then is how to provide Support
  * Level access to PCB fields that will only be used in the Support Level. The standard approach, at
  * least in systems-level programming such as an OS, is to define a structure containing the additional
  * Support Level fields (support_t) and then add a pointer (support_t *) to the PCB. The Support
  * Level code needing access to these fields will execute a NSYS8 [Section 6.8] which returns a pointer
  * to the Current Process’s support_t structure. This provides Support Level access to relevant PCB
  * fields while hiding the Level 3 (and Level 2) PCB fields. The NSYS1 service is requested by the calling
  * process by placing the value -1 in a0, a pointer to a processor state in a1, a process priority value in
  * a2, (optionally) a pointer to a Support Structure in a3, and then executing the SYSCALL instruction.
  * The following C code can be used to request a NSYS1:
  * int retValue = SYSCALL(CREATEPROCESS, state_t *statep, int prio, support_t *supportp);
  * Where the mnemonic constant CREATEPROCESS has the value of -1.
  * The newly populated PCB is placed on the Ready Queue and is made a child of the Current
  * Process. Process Count is incremented by one, and control is returned to the Current Process.
*/
void createProcess(state_t *statep, int prio, support_t *supportp) {           // HOW TO CALL IN HANDLER: createProcess(current_proc->p_s->reg_a1, current_proc->p_s->reg_a2, current_proc->p_s->reg_a3)
    pcb_t* new_proc;
    new_proc = allocPcb();      // allocPcb() handles required setup of pid (incrementally), time (0), semadd (NULL)

    if (new_proc == NULL) {
        currentProcess->p_s->reg_a0 = -1;        // TODO: to verify that access to register is done like this; it must refer to the process blocked at the time of the excecption (the caller)
        return;
    }

    new_proc->p_s = statep;                     // from reg a1 (statep)
    new_proc->p_supportStruct = supportp;	    // from reg a3 (supportp), or NULL if not provided
    new_proc->p_prio = prio;                    // TODO: priority to be set here manually?
    insertChild(currentProcess, new_proc);      
    insertProcQ(readyQueue, new_proc);          
    processCount++;

    currentProcess->p_s->reg_a0 = new_proc->p_pid;        // TODO: verify if access to reg_a0 correct (should be proc at time of excpection)
}

/** 
  * This services causes the executing process or another process to cease to exist. In addition, recursively,
  * all progeny of that process are terminated as well. Execution of this instruction does not complete
  * until all progeny are terminated, after which the Scheduler should be called. The NSYS2 service is
  * requested by the calling process by placing the value -2 in a0 and then executing the SYSCALL
  * instruction.
  * The following C code can be used to request a NSYS2:
  * SYSCALL(TERMINATEPROCESS, int pid, 0, 0);
  * Where the mnemonic constant TERMINATEPROCESS has the value of -2.
  * This service terminates the calling process if PID is zero, the process whose identifier is PID
  * otherwise.
  */
void terminateProcess(int pid) {     // (NSYS2) 
    pcb_t proc_to_terminate = xxx;    // TODO: Get process by pid or caller if pid = 0
    terminateCurrentAndChild(proc_to_terminate);

    schedule();
}

/**
  * 6.3 Passeren (P) (NSYS3)
  * This service requests the Nucleus to perform a P operation on a semaphore. The P or NSYS3 service is
  * requested by the calling process by placing the value -3 in a0, the physical address of the semaphore
  * to be P’ed in a1, and then executing the SYSCALL instruction. Depending on the value of the
  * semaphore, the value of the semaphore is decreased and control is returned to the Current Process,
  * or this process is blocked on the ASL (transitions from “running” to “blocked”) and the Scheduler is
  * called.
  * The following C code can be used to request a NSYS3:
  * SYSCALL(PASSEREN, int *semaddr, 0, 0);
  * Where the mnemonic constant PASSEREN has the value of -3.
  */
void passeren(int *semaddr) {                  // (NSYS3)
    if (*semaddr > 0) {
        *(semaddr--);
    } else {
        currentProcess->p_s = *GET_EXCEPTION_STATE_PTR();
        insertBlocked(semaddr, currentProcess);
        currentProcess = NULL;
        schedule();
    }
}                    

/** 
  * This service requests the Nucleus to perform a V operation on a semaphore. The V or NSYS4 service
  * is requested by the calling process by placing the value -4 in a0, the physical address of the semaphore
  * to be V’ed in a1, and then executing the SYSCALL instruction. The V operation is non blocking,
  * depending on the value of the semaphore, control is either returned to the Current Process, or the
  * semaphore value is increased.
  * The following C code can be used to request a NSYS4:
  * SYSCALL(VERHOGEN, int *semaddr, 0, 0);
  * Where the mnemonic constant VERHOGEN has the value of -4.
  */
void verhogen(int *semaddr) {                  // (NSYS4)
    (*semaddr)++;

    
} 

void doIO() {                // (NSYS5)
    return;
} 

float getCPUTime() {          // (NSYS6)
    return;
} 

void waitForClock() {        // (NSYS7)     // TODO: waitClock su altro file
    return;
} 

void getSupportData() {      // (NSYS8)
    return;
}

int getProcessID() {        // (NSYS9)
    return;
}

void yield() {               // (NSYS10)
    return;
}



void terminateCurrentAndChild(pcb_t *proc) {
    while (!emptyChild(proc)) {
        pcb_t *p_child = removeChild(proc);
        terminateCurrentAndChild(p_child);
    }

    outProcQ(&readyQueue, proc);
    processCount--;
}