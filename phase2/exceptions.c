/* Exception handling and SYSCALL processing implementation. */

#include "/headers/const.h"

#include "exception.h"
#include "init.h"
#include "interrupt.h"
#include "scheduler.h"
#include "syscall.h"


/*
Handles exceptions that must either be passed to the support level
or cause the termination of the current process.

If the process has no support structure (p_supportStruct == NULL),
the process cannot handle the exception and is therefore terminated.

Otherwise, the exception state is copied into the support structure
and control is transferred to the support-level handler with LDCXT.
*/
void PassUpOrDie(int i){
//note per il gruppo: currrentProcess va dichiarato in un file, LDCXT è una funzione macro fornita da urisc-v, 
//terminateProcess() e scheduler() vanno implementate in altri file
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(i);

    if(currentProcess->p_supportStruct==NULL){
        //"die"
        terminateProcess();  //must terminate the current process, otherwise it doesn't work
        scheduler();
    }
    else{
        //pass up
        support_t *sup = currentProcess->p_supportStruct;

        //copy the old state
        sup->sup_exceptState[i] = *exceptionState;

        LDCXT(sup->sup_exceptContext[i].stackPtr, sup->sup_exceptContext[i].status, sup->sup_exceptContext[i].pc);
    }
}

/*
Handles TLB refill exceptions.
This handler loads a default TLB entry and then restores the state saved in the BIOS data page.
*/
void uTLB_RefillHandler() {
    setENTRYHI(0x80000000);
    setENTRYLO(0x00000000);
    TLBWR();
    LDST((state_t*) BIOSDATAPAGE);
}

/*
Handles program traps by invoking PassUpOrDie for general exceptions.
*/
void ProgramTrapHandler(){
    PassUpOrDie(GENERALEXCEPT);
}


/*
Handles TLB-related exceptions. The exception is either passed to the support level or the process is killed.
*/
void TLBExceptionHandler(){
    PassUpOrDie(PGFAULTEXCEPT);
}

/*
Main exception dispatcher of the kernel.
It examines the cause register to determine the type of exception and calls the appropriate handler.
 */
void ExceptionHandler() {

    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(GENERALEXCEPT);

    unsigned int cause = exceptionState->cause;
    int excCode = (cause & GETEXECCODE) >> CAUSESHIFT;

    //check interrupts for interrupt handler
    if (CAUSE_IP_GET(cause, IL_TIMER) ||
    CAUSE_IP_GET(cause, IL_CPUTIMER) ||
    CAUSE_IP_GET(cause, IL_IPI) ||
    CAUSE_IP_GET(cause, IL_DISK) ||
    CAUSE_IP_GET(cause, IL_FLASH) ||
    CAUSE_IP_GET(cause, IL_PRINTER) ||
    CAUSE_IP_GET(cause, IL_TERMINAL)) {

        interruptHandler();
        return;
    }

    //TLB exception handler
    if (excCode == TLBINVLDL || excCode == TLBINVLDS) {
        TLBExceptionHandler();
        return;
    }

    //Syscall handler
    if (excCode == SYSEXCEPTION) {
        SyscallExceptionHandler();
        return;
    }

    //Program Trap handler
    ProgramTrapHandler();
}


/*
 * SYSCALL exception handler.
 * Dispatches system calls based on the value in register a0.
 *
 * - Negative a0 in user mode → PRIVINSTR program trap
 * - Negative a0 in kernel mode → Nucleus SYSCALL (NSYS1-10)
 * - Non-negative a0 → Pass Up or Die (support level SYSCALL)
 */
void SyscallExceptionHandler() {
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(0);
    int syscallNumber = exceptionState->reg_a0;
    
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

    /* Negative syscall in kernel mode → handle at Nucleus level.
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
