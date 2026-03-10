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
Handles system calls issued by processes.
The syscall number is stored in register a0.
Arguments are passed in the remaining registers.
If the syscall is invoked in user mode, it is treated as a program trap.
 */
void SyscallExceptionHandler(){
    state_t *exceptionState = GET_EXCEPTION_STATE_PTR(GENERALEXCEPT);

    if ((exceptionState->status & MSTATUS_MPP_MASK) == MSTATUS_MPP_U){
        ProgramTrapHandler();
        return;
    }

    //nota per il gruppo: scheduler se è richiamato dentro le funzioni richiamate nel switch si può togliere qui
    exceptionState->pc += WORDLEN;

    int syscallNumber = exceptionState->reg_a0;

    // Dispatch the syscall based on its identifier
    switch(syscallNumber){

        case CREATEPROCESS:
            createProcess();
            break;

        case TERMPROCESS:
            terminateProcess();
            break;

        case PASSEREN:
            passeren();
            break;

        case VERHOGEN:
            verhogen();
            break;

        case DOIO:
            doIO();
            break;

        case GETTIME:
            getCPUTime();
            break;

        case CLOCKWAIT:
            waitClock();
            break;

        case GETSUPPORTPTR:
            getSupportPtr();
            break;

        case GETPROCESSID:
            getProcessID();
            break;

        case YIELD:
            yield();
            break;

        default:
            ProgramTrapHandler();
    }
}
