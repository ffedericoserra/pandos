/* Exception handling and SYSCALL processing implementation. */

#include <uriscv/liburiscv.h>
#include <uriscv/cpu.h>

#include "../headers/types.h"
#include "../headers/const.h"

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"

#include "./headers/exception.h"
#include "./headers/interrupt.h"
#include "./headers/scheduler.h"
#include "./headers/syscall.h"

/*
Handles exceptions that must either be passed to the support level
or cause the termination of the current process.

If the process has no support structure (p_supportStruct == NULL),
the process cannot handle the exception and is therefore terminated.

Otherwise, the exception state is copied into the support structure
and control is transferred to the support-level handler with LDCXT.
*/
void passUpOrDie(int i){

    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);

    if(currentProcess->p_supportStruct==NULL){
        //"die"
        terminateProcess(0);  //must terminate the current process, otherwise it doesn't work
    }
    else{
        //pass up
        support_t *sup = currentProcess->p_supportStruct;

        //copy the old state
        copyState(&sup->sup_exceptState[i], exceptionState);
        
        LDCXT(sup->sup_exceptContext[i].stackPtr, sup->sup_exceptContext[i].status, sup->sup_exceptContext[i].pc);
    }
}

/*
Handles program traps by invoking passUpOrDie for general exceptions.
*/
void programTrapHandler(){
    passUpOrDie(GENERALEXCEPT);
}


/*
Handles TLB-related exceptions. The exception is either passed to the support level or the process is killed.
*/
void tlbExceptionHandler(){
    passUpOrDie(PGFAULTEXCEPT);
}

/*
Main exception dispatcher of the kernel.
It examines the cause register to determine the type of exception and calls the appropriate handler.
 */
void exceptionHandler() {
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);
    unsigned int cause = exceptionState->cause;

    /* Check if this is an interrupt */
    if (CAUSE_IS_INT(cause)) {
        InterruptHandler();
        return;
    }

    /* Extract exception code (RISC-V: lower 31 bits, no shift) */
    unsigned int excCode = cause & CAUSE_EXCCODE_MASK;

    /* SYSCALL exceptions:
     * EXC_ECU=8 (ecall from U-mode), EXC_ECM=11 (ecall from M-mode) */
    if (excCode == EXC_ECU || excCode == EXC_ECM) {
        syscallExceptionHandler();
        return;
    }

    /* TLB exceptions (codes 24-28 per spec) */
    if (excCode >= EXC_MOD && excCode <= EXC_UTLBS) {
        tlbExceptionHandler();
        return;
    }

    /* All other exceptions are Program Traps */
    programTrapHandler();
}


/*
 * SYSCALL exception handler.
 * Dispatches system calls based on the value in register a0.
 *
 * - Negative a0 in user mode ? PRIVINSTR program trap
 * - Negative a0 in kernel mode ? Nucleus SYSCALL (NSYS1-10)
 * - Non-negative a0 ? Pass Up or Die (support level SYSCALL)
 */
void syscallExceptionHandler() {
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);
    int syscallNumber = (int)exceptionState->reg_a0;
    
    /* User-mode check for privileged (negative) syscalls */
    if ((exceptionState->status & MSTATUS_MPP_MASK) == MSTATUS_MPP_U) {
        if (syscallNumber < 0) { /* Privileged syscall from user mode -> simulate PRIVINSTR trap */
            exceptionState->cause = PRIVINSTR;
            programTrapHandler();
            return;
        }
        /* Positive syscall from user mode -> pass up (no PC increment) */
        passUpOrDie(GENERALEXCEPT);
        return;
    }

    /* Kernel mode: positive or zero syscall -> pass up (no PC increment) */
    if (syscallNumber >= 0) {
        passUpOrDie(GENERALEXCEPT);
        return;
    }

    /* Negative syscall in kernel mode ? handle at Nucleus level.
     * Increment PC by WORDLEN to avoid infinite SYSCALL loop. */
    exceptionState->pc_epc += WORDLEN;

    unsigned int arg1 = exceptionState->reg_a1;
    unsigned int arg2 = exceptionState->reg_a2;
    unsigned int arg3 = exceptionState->reg_a3;

    switch (syscallNumber) {
        case CREATEPROCESS:
            createProcess((state_t *)arg1, (int)arg2, (support_t *)arg3);
            break;

        case TERMPROCESS:
            terminateProcess((int)arg1);
            break;

        case PASSEREN:
            passeren((int *)arg1);
            break;

        case VERHOGEN:
            verhogen((int *)arg1);
            break;

        case DOIO:
            doIO((int *)arg1, (int)arg2);
            break;

        case GETTIME:
            getCPUTime();
            break;

        case CLOCKWAIT:
            waitForClock();
            break;

        case GETSUPPORTPTR:
            getSupportData();
            break;

        case GETPROCESSID:
            getProcessID((int)arg1);
            break;

        case YIELD:
            yield();
            break;

        default:
            /* Non-existent Nucleus service -> Program Trap */
            programTrapHandler();
            break;
    }
}