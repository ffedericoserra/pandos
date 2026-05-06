/* Nucleus Initialization */

#include <uriscv/liburiscv.h>
#include "../headers/types.h"
#include "../headers/const.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/exceptions.h"
#include "./headers/scheduler.h"

/* Global variables */
int processCount;
int softBlockCount;
struct list_head readyQueue;
pcb_t *currentProcess;
pcb_t *rootProcess;
int deviceSemaphores[SEMDEVLEN];
cpu_t startTOD;

int main() {

    /* Populate Processor 0 Pass Up Vector */
    passupvector_t *passupvector = (passupvector_t *)PASSUPVECTOR;
    passupvector->tlb_refill_handler = (memaddr)uTLB_RefillHandler;
    passupvector->tlb_refill_stackPtr = KERNELSTACK;
    passupvector->exception_handler = (memaddr)exceptionHandler;
    passupvector->exception_stackPtr = KERNELSTACK;

    /* Initialize Level 2 data structures */
    initPcbs();
    initASL();

    /* Initialize global variables */
    processCount = 0;
    softBlockCount = 0;
    mkEmptyProcQ(&readyQueue);
    currentProcess = NULL;

    /* Initialize device semaphores to 0 (synchronization semaphores) */
    for (int i = 0; i < SEMDEVLEN; i++)
        deviceSemaphores[i] = 0;

    /* Load system-wide Interval Timer with 100 milliseconds */
    LDIT(PSECOND);

    /* Declare test process */
    extern void test();

    /* Allocate and initialize the first process */
    pcb_t *pcb = allocPcb();

    /* Set PC to test address */
    pcb->p_s.pc_epc = (memaddr)test;

    /* Set SP to RAMTOP */
    RAMTOP(pcb->p_s.reg_sp);

    /* Enable interrupts */
    pcb->p_s.mie = MIE_ALL;

    /* Enable interrupt and kernel mode */
    pcb->p_s.status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

    /* Initialize remaining PCB fields */
    pcb->p_time = 0;
    pcb->p_semAdd = NULL;
    pcb->p_supportStruct = NULL;

    /* Place on Ready Queue and increment Process Count */
    rootProcess = pcb;
    insertProcQ(&readyQueue, pcb);
    processCount++;

    /* Call the Scheduler */
    scheduler();
    /* Should never reach here */
    return 0;
}