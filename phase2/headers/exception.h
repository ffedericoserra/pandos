/* placeholder */
#ifndef EXCEPTION_H
#define EXCEPTION_H

/*
Exception handling interface.
This module implements the kernel-level exception management, 
including interrupts, TLB exceptions, program traps and syscalls.
*/

//Main exception dispatcher
void ExceptionHandler();

//System call handler
void SyscallExceptionHandler();

//TLB refill handler
void uTLB_RefillHandler();

//Program trap handler
void ProgramTrapHandler();

//TLB exception handler
void TLBExceptionHandler();

//Pass Up or Die mechanism
void PassUpOrDie(int i);

#endif