#ifndef SYSCALL_H
#define SYSCALL_H

#include "../../headers/types.h"

/* SYSCALL exception handlers */
void createProcess(state_t *statep, int prio, support_t *supportp);
void terminateProcess(int pid);
void passeren(int *semaddr);
void verhogen(int *semaddr);
void doIO(int *commandAddr, int commandValue);
void getCPUTime();
void waitForClock();
void getSupportData();
void getProcessID(int parent);
void yield();

#endif