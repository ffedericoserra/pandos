#include <uriscv/liburiscv.h>
#include "../headers/types.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/scheduler.h"

// Riferimenti esterni alle variabili globali definite in init.c
extern int processCount;
extern int softBlockCount;
extern struct list_head readyQueue;
extern pcb_t *currentProcess;
extern int deviceSems[48]; // 48 semafori come da specifica
extern int pseudoClockSem;
extern unsigned int startTime; // Salvata all'inizio del time slice

void interruptHandler() {
    unsigned int cause = getCAUSE();
    state_t *oldState = (state_t *)BIOSDATAPAGE;
    unsigned int currentTOD;

    // 1. Processor Local Timer (PLT) - Linea 1
    if (cause & CAUSE_IP_LINE1) {
        setTIMER(TIMESLICE); // Acknowledge
        
        if (currentProcess != NULL) {
            STCK(currentTOD);
            currentProcess->p_time += (currentTOD - startTime); // Aggiorna tempo CPU
            copyState(oldState, &(currentProcess->p_s));
            insertProcPt(&readyQueue, currentProcess);
            currentProcess = NULL;
        }
        scheduler();
    }

    // 2. Interval Timer (Pseudo-clock) - Linea 2
    if (cause & CAUSE_IP_LINE2) {
        LDIT(PSECOND); // Acknowledge 100ms
        
        // Sblocca TUTTI i processi in attesa
        pcb_t *p;
        while ((p = removeBlocked(&pseudoClockSem)) != NULL) {
            insertProcPt(&readyQueue, p);
            softBlockCount--;
        }
        
        if (currentProcess == NULL) scheduler();
        LDST(oldState);
    }

    // 3. Dispositivi Esterni - Linee 3-7
    for (int line = 3; line <= 7; line++) {
        if (cause & (1 << line)) {
            // Legge la bitmap per la linea specifica
            unsigned int *bitMapAddr = (unsigned int *)0x10000040;
            unsigned int devBitMap = bitMapAddr[line - 3];
            
            for (int devNum = 0; devNum < 8; devNum++) {
                if (devBitMap & (1 << devNum)) {
                    // Calcolo indirizzo base
                    dtpreg_t *devReg = (dtpreg_t *)(0x10000054 + ((line - 3) * 0x80) + (devNum * 0x10));
                    int semIndex, status;

                    // Gestione speciale Terminale (Linea 7)
                    if (line == 7) {
                        // Priorità alla trasmissione (Index 40-47)
                        if (devReg->transm_status & 0xFF) { // Se c'è un interrupt di trasmissione
                            status = devReg->transm_status;
                            devReg->transm_command = 1; // ACK transm
                            semIndex = 40 + devNum;
                        } else { // Altrimenti ricezione (Index 32-39)
                            status = devReg->recv_status;
                            devReg->recv_command = 1; // ACK recv
                            semIndex = 32 + devNum;
                        }
                    } else {
                        // Dispositivi standard (Linee 3-6)
                        status = devReg->status;
                        devReg->command = 1; // ACK
                        semIndex = ((line - 3) * 8) + devNum;
                    }

                    // Operazione V sul semaforo corretto
                    pcb_t *p = removeBlocked(&(deviceSems[semIndex]));
                    if (p != NULL) {
                        p->p_s.reg_a0 = status; // Salva stato in a0
                        insertProcPt(&readyQueue, p);
                        softBlockCount--;
                    }

                    if (currentProcess == NULL) scheduler();
                    LDST(oldState);
                }
            }
        }
    }
}/* Device interrupt handler */
