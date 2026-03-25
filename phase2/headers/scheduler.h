
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"
#include "listx.h"
#include "pcb.h"

// Esportazione delle variabili globali definite in init.c 
extern int processCount;
extern int softBlockCount;
extern struct list_head readyQueue;
extern pcb_t *currentProcess;
extern cpu_t startTOD;

// Dichiarazione della funzione principale dello scheduler 
void scheduler();

#endif
