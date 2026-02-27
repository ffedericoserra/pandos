/* SYSCALL exception handling */

/* 
  ACCESS TO REGISTERS:
    #define reg_a0 gpr[24]
    #define reg_a1 gpr[25]
    #define reg_a2 gpr[26]
    #define reg_a3 gpr[27]
    #define reg_a4 gpr[28]
*/

#include <uriscv/types.h>
#include "../headers/types.h"
#include "../headers/const.h"
// #include "../headers/listx.h"

#include "../phase1/headers/asl.h"
#include "../phase1/headers/pcb.h"

/** 
 * Create a new process
*/
void createProcess(state_t *statep, int prio, support_t *supportp) {           // HOW TO CALL IN HANDLER: createProcess(current_proc->p_s->reg_a1, current_proc->p_s->reg_a2, current_proc->p_s->reg_a3)
    pcb_t* new_proc;
    new_proc = allocPcb();      // allocPcb() handles required setup of pid (incrementally), time (0), semadd (NULL)

    if (new_proc == NULL) {
        current_proc->p_s->reg_a0 = -1;        // TODO: to verify that acces to register is done like this; it must refer to the process blocked at the time of the excecption
        return;
    }

    new_proc->p_s = statep;                     // from reg a1 (statep)
    new_proc->p_supportStruct = supportp;	    // from reg a3 (supportp), or NULL if not provided
    new_proc->p_prio = prio;                    // TODO: priority to be set here manually?
    insertProcQ(process_queue, new_proc);       // TODO: process_queue actual name to be set in init.c
    insertChild(current_proc, new_proc);        // TODO: current_proc actual name to be set in init.c
    process_count++;                            // TODO: name to actual in init.c

    current_proc->p_s->reg_a0 = new_proc->p_pid;        // TODO: verify if access to reg_a0 correct (should be proc at time of excpection)

    return;
}

/* Terminate process specified by pid and all its progeny or the calling process if pid is 0 */
void terminateProcess(int pid) {     // (NSYS2)
    return;
}

void p() {                  // (NSYS3)
    return;
}                    

void v() {                  // (NSYS4)
    return;
} 

void doio() {                // (NSYS5)
    return;
} 

float getCPUTime() {          // (NSYS6)
    return;
} 

void waitForClock() {        // (NSYS7)
    return;
} 

void getSupportData() {      // (NSYS8)
    return;
}

int getProcessID() {        // (NSYS9)
    return;
}

void yield() {               // (NSYS10)
    return;
}