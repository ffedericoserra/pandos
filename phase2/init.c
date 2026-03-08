/* Nucleus Initialization */

#include "types.h"
#include "const.h"
#include "exception.h"

int processCount;
int softBlockCount;
struct list_head readyQueue;
pcb_t* currentProcess;
int deviceSemaphores[SEMDEVLEN];



int main(){


    passupvector_t *passupvector = PASSUPVECTOR;
    passupvector->tlb_refill_handler = (memaddr)uTLB_RefillHandler;
    passupvector->tlb_refill_stackPtr = KERNELSTACK;
    passupvector->exception_handler = (memaddr)exceptionHandler;
    passupvector->exception_stackPtr = KERNELSTACK;


    initPcbs();
    initASL();

    processCount = 0;
    softBlockCount = 0;
    mkEmptyProcQ(readyQueue);
    currentProcess = NULL;

    for(int i = 0; i<SEMDEVLEN; i++)
        deviceSemaphores[i] = 0;


    /* Load system-wide Interval Timer with 100 milliseconds */
    *((memaddr *) INTERVALTMR) = PSECOND;

    /* Declare test process */
    extern void test();
    

    pcb_t *pcb = allocPcb();


    /* set PC to test address */
    pcb->p_s.pc_epc = (memaddr) test;

    /* set SP to RAMTOP */
    RAMTOP(pcb->p_s.reg_sp);

    /* Enable interrupts */
    pcb->p_s.mie = MIE_ALL;

    /* Enable interrupt and kernel mode */
    pcb->p_s.status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

    /* Initialize PCB fields */
    pcb->p_parent = NULL;
    INIT_LIST_HEAD(&pcb->p_child);
    INIT_LIST_HEAD(&pcb->p_sib);

    pcb->p_time = 0;
    pcb->p_semAdd = NULL;
    pcb->p_supportStruct = NULL;


    insertProcQ(&readyQueue, pcb);
    processCount++;



    scheduler();
}


