/* Nucleus Initialization */

#include "../headers/types.h"
#include "../headers/const.h"
#include "headers/exception.h"
#include "../phase1/headers/asl.h"
#include "../phase1/headers/pcb.h"
#include "headers/scheduler.h"

// variabili globali
int processCount;
int softBlockCount;
struct list_head readyQueue;
pcb_t* currentProcess;
int deviceSemaphores[SEMDEVLEN];

cpu_t startTOD;     


int main(){

    // inizializzazione del Processor 0 Pass Up Vector
    passupvector_t *passupvector = (passupvector_t*) PASSUPVECTOR;
    passupvector->tlb_refill_handler = (memaddr)uTLB_RefillHandler;
    passupvector->tlb_refill_stackPtr = KERNELSTACK;
    passupvector->exception_handler = (memaddr)ExceptionHandler;
    passupvector->exception_stackPtr = KERNELSTACK;

    // inizializzazione delle strutture dati
    initPcbs();
    initASL();

    // inizializzazione delle variabili globali
    processCount = 0;
    softBlockCount = 0;
    mkEmptyProcQ(&readyQueue);
    currentProcess = NULL;

    for(int i = 0; i < SEMDEVLEN; i++)
        deviceSemaphores[i] = 0;


    // caricamento del Interval Timer a 100ms
    //*((memaddr *) INTERVALTMR) = PSECOND;
    LDIT(PSECOND);

    // creazione del processo Test
    extern void test();
    
    // allocazione del pcb 
    pcb_t *pcb = allocPcb();


    // set PC all'indirizzo test
    pcb->p_s.pc_epc = (memaddr) test;

    // set SP a RAMTOP 
    RAMTOP(pcb->p_s.reg_sp);

    // abilito gli interrupt
    pcb->p_s.mie = MIE_ALL;

    // abilito interrupt e kernel mode
    pcb->p_s.status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

    // inizializzazione dei campi del pcb
    
    pcb->p_parent = NULL;
    INIT_LIST_HEAD(&pcb->p_child);
    INIT_LIST_HEAD(&pcb->p_sib);
    
    pcb->p_time = 0;
    pcb->p_semAdd = NULL;
    pcb->p_supportStruct = NULL;

    //inserisco il processo nella Ready Queue e aumento il processCount
    insertProcQ(&readyQueue, pcb);
    processCount++;


    //chiamo lo scheduler
    scheduler();
}


