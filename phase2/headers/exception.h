#ifndef EXCEPTION_H
#define EXCEPTION_H

#include "../../headers/const.h"
#include "../../headers/types.h"

/* Main exception dispatcher */
void exceptionHandler();

/* System call handler */
void syscallExceptionHandler();

/* TLB refill handler */
void uTLB_RefillHandler();

/* Program trap handler */
void programTrapHandler();

/* TLB exception handler */
void tlbExceptionHandler();

/* Pass Up or Die mechanism */
void passUpOrDie(int i);

#endif