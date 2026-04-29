/* Scheduler */


#include "headers/scheduler.h"
#include "../headers/const.h"
#include <uriscv/liburiscv.h>


void scheduler(){

    if (emptyProcQ(&readyQueue)) {
        // Caso 1: Nessun processo nel sistema.
        if(processCount == 0){
            HALT();
        }
        // Caso 2: Ci sono processi, ma sono tutti bloccati
        else if (processCount > 0 && softBlockCount > 0) {
            // Imposta il Wait State
            
            // Abilita gli interrupt globali nel registro STATUS
            setMIE(MIE_ALL & ~MIE_MTIE_MASK);
            unsigned int status = getSTATUS(); 
            status |= MSTATUS_MIE_MASK; 
            setSTATUS(status);
            
            // Mette la CPU in attesa di un interrupt (che non sia del PLT)
            WAIT();
        }
        // Caso 3: Deadlock. Ci sono processi, ma nessuno è pronto e nessuno è in attesa (soft-block)
        else if (processCount > 0 && softBlockCount == 0) {
            PANIC();
        }
    // La coda ready non è vuota
    }else{

        // Rimuovi il primo processo pronto e impostalo come processo corrente
        currentProcess = removeProcQ(&readyQueue);
        
        // Record start time for CPU time accounting
        STCK(startTOD);

        // Carica il PLT con il time slice (5 millisecondi), scalato in cicli di clock
        setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));
        
        // Ripristina lo stato del processore caricando i registri dal PCB 
        LDST(&(currentProcess->p_s));

    }
}
