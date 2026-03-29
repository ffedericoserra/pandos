
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "../../headers/types.h"
#include "../../headers/listx.h"
#include "../../phase1/headers/pcb.h"

// Esportazione delle variabili globali definite in init.c 
extern int processCount;
extern int softBlockCount;
extern struct list_head readyQueue;
extern pcb_t *currentProcess;
extern cpu_t startTOD;

// Dichiarazione della funzione principale dello scheduler 
void scheduler();

#endif
