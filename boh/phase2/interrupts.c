#include <uriscv/liburiscv.h>
#include "../headers/types.h"
#include "../headers/const.h"
#include "../headers/listx.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/scheduler.h"

// Riferimenti esterni alle variabili globali definite in init.c
extern int processCount;
extern int softBlockCount;
extern struct list_head readyQueue;
extern pcb_t *currentProcess;
extern int deviceSemaphores[48]; // 48 semafori come da specifica
extern cpu_t startTOD;

void interruptHandler() {
    unsigned int cause = getCAUSE();
    state_t *oldState = (state_t *)BIOSDATAPAGE;
    unsigned int currentTOD;

    // 1. Processor Local Timer (PLT) - Linea 1
    if (CAUSE_IP_GET(cause, IL_CPUTIMER)) {
        setTIMER(TIMESLICE); // Acknowledge
        
        if (currentProcess != NULL) {
            STCK(currentTOD);
            currentProcess->p_time += (currentTOD - startTOD); // Aggiorna tempo CPU
            copyState(&(currentProcess->p_s),oldState);
            insertProcQ(&readyQueue, currentProcess);
            currentProcess = NULL;
        }
        scheduler();
    }

    // 2. Interval Timer (Pseudo-clock) - Linea 2
    if (CAUSE_IP_GET(cause, IL_TIMER)) {    
        LDIT(PSECOND); // Acknowledge 100ms
        
        // Sblocca TUTTI i processi in attesa
        pcb_t *p;
        while ((p = removeBlocked(&deviceSemaphores[PSEUDOCLOCK_SEM])) != NULL) {
            insertProcQ(&readyQueue, p);
            softBlockCount--;
        }
        
        if (currentProcess == NULL) scheduler();
        LDST(oldState);
    }

    // 3. Dispositivi Esterni - Linee 3-7
    for (int line = IL_DISK; line <= IL_TERMINAL; line++) {
        if (CAUSE_IP_GET(cause, line)) {
            // Legge la bitmap per la linea specifica
            unsigned int *bitMapAddr = (unsigned int *)0x10000040;
            unsigned int devBitMap = bitMapAddr[line - 3];
            
            for (int devNum = 0; devNum < 8; devNum++) {
                if (devBitMap & (1 << devNum)) {
                    
                    // CALCOLO INDIRIZZO E USO DELLA UNION
                    // Castiamo l'indirizzo base alla union devreg_t
                    devreg_t *devReg = (devreg_t *)(0x10000054 + ((line - 3) * 0x80) + (devNum * 0x10));
                    
                    int semIndex, status;

                    if (line == 7) { 
                        // Se è un terminale, accedo tramite il campo .term della union
                        // Priorità alla trasmissione
                        if (devReg->term.transm_status & 0xFF) { 
                            status = devReg->term.transm_status;
                            devReg->term.transm_command = 1; // ACK transm
                            semIndex = 40 + devNum; // Indici 40-47 per transm
                        } else {
                            status = devReg->term.recv_status;
                            devReg->term.recv_command = 1; // ACK recv
                            semIndex = 32 + devNum; // Indici 32-39 per recv
                        }
                    } else {
                        // Per gli altri dispositivi (3-6), accedo tramite il campo .dtp
                        status = devReg->dtp.status;
                        devReg->dtp.command = 1; // ACK standard
                        semIndex = ((line - 3) * 8) + devNum;
                    }

                    // Operazione V sul semaforo corretto
                    pcb_t *p = removeBlocked(&(deviceSemaphores[semIndex]));
                    if (p != NULL) {
                        p->p_s.reg_a0 = status; // Ritorniamo lo status al processo
                        insertProcQ(&readyQueue, p);
                        softBlockCount--;
                    }

                    // Se non c'è un processo corrente, chiamiamo lo scheduler
                    if (currentProcess == NULL) scheduler();
                    // Altrimenti torniamo al processo interrotto
                    LDST(oldState);
                }
            }
        }
    }

}/* Device interrupt handler */
