/* Device interrupt handler */

#include <uriscv/liburiscv.h>
#include <uriscv/cpu.h>
#include "../headers/types.h"
#include "../headers/const.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/scheduler.h"
#include "./headers/interrupts.h"

/* Interrupting Devices Bit Map base address */
#define INTDEV_BITMAP 0x10000040

/* Pseudo-clock semaphore is the last device semaphore (index 48) */
#define PSEUDOCLOCK_SEM (SEMDEVLEN - 1)

/* Device status: ready/idle means no interrupt pending */
#define DEV_STATUS_READY 1

/* Terminal status mask: lower byte contains status code */
#define TERM_STATUS_MASK 0xFF

// Riferimenti esterni alle variabili globali definite in init.c
extern int processCount;
extern int softBlockCount;
extern struct list_head readyQueue;
extern pcb_t *currentProcess;
extern int deviceSemaphores[48]; // 48 semafori come da specifica
extern cpu_t startTOD;

/*
 * Handle a non-timer device interrupt.
 * intLineNo: interrupt line (3-7), devNo: device number (0-7).
 */
static void handleDeviceInterrupt(state_t *oldState, int intLineNo, int devNo) {
    int status;
    int semIndex;

    /* Calculate device register base address */
    memaddr devRegAddr = START_DEVREG +
        ((intLineNo - 3) * 0x80) + (devNo * 0x10);

    if (intLineNo == 7) {
        /* Terminal device: two sub-devices (transmit has higher priority) */
        termreg_t *termReg = (termreg_t *)devRegAddr;

        if ((termReg->transm_status & TERM_STATUS_MASK) != DEV_STATUS_READY) {
            /* Transmit interrupt pending */
            status = termReg->transm_status;
            termReg->transm_command = ACK;
            semIndex = (4 * DEVPERINT) + devNo;             /* 32 + devNo */
        } else {
            /* Receive interrupt pending */
            status = termReg->recv_status;
            termReg->recv_command = ACK;
            semIndex = (4 * DEVPERINT) + DEVPERINT + devNo; /* 40 + devNo */
        }
    } else {
        /* Non-terminal device */
        dtpreg_t *devReg = (dtpreg_t *)devRegAddr;
        status = devReg->status;
        devReg->command = ACK;
        semIndex = (intLineNo - 3) * DEVPERINT + devNo;
    }

    /* V operation on the device semaphore */
    deviceSemaphores[semIndex]++;
    pcb_t *p = removeBlocked(&deviceSemaphores[semIndex]);
    if (p != NULL) {
        p->p_s.reg_a0 = status;
        insertProcQ(&readyQueue, p);
        softBlockCount--;
    }

    /* Return control to current process or call scheduler */
    if (currentProcess != NULL) {
        LDST(oldState);
    } else {
        scheduler();
    }
}

/*
 * Main interrupt exception handler.
 *
 * µRISCV cause register for interrupts:
 *   bit 31 = 1 (interrupt flag)
 *   bits 30:0 = interrupt number (IL_CPUTIMER=7, IL_TIMER=3, IL_DISK=17, etc.)
 */
void InterruptHandler() {
    state_t *oldState = GET_EXCEPTION_STATE_PTR(0);
    cpu_t currentTOD;
    unsigned int cause = oldState->cause;

    /* Extract the interrupt number from the cause register */
    unsigned int intNo = cause & CAUSE_EXCCODE_MASK;

    /* 1. Processor Local Timer (PLT) - highest priority */
    if (intNo == IL_CPUTIMER) {
        /* Acknowledge PLT by reloading timer, scaled to clock cycles */
        setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));

        if (currentProcess != NULL) {
            /* Update accumulated CPU time */
            STCK(currentTOD);
            currentProcess->p_time += (currentTOD - startTOD);

            /* Save processor state into current process PCB */
            copyState(&(currentProcess->p_s), oldState);

            /* Put process back on ready queue */
            insertProcQ(&readyQueue, currentProcess);
        }

        currentProcess = NULL;
        scheduler();
        return;
    }

    /* 2. Interval Timer (Pseudo-clock tick) */
    if (intNo == IL_TIMER) {
        /* Acknowledge by reloading with 100ms */
        LDIT(PSECOND);

        /* Unblock ALL processes waiting on pseudo-clock semaphore */
        pcb_t *p;
        while ((p = removeBlocked(&deviceSemaphores[PSEUDOCLOCK_SEM])) != NULL) {
            insertProcQ(&readyQueue, p);
            softBlockCount--;
        }
        /* Reset pseudo-clock semaphore to 0 */
        deviceSemaphores[PSEUDOCLOCK_SEM] = 0;

        /* Return control to current process or call scheduler */
        if (currentProcess != NULL) {
            LDST(oldState);
        } else {
            scheduler();
        }
        return;
    }

    /* 3. External device interrupts (IL_DISK=17 through IL_TERMINAL=21)
     *
     * Semaphore layout:
     *   Disk:     sem[0..7]   (intLineNo=3, IL_DISK=17)
     *   Flash:    sem[8..15]  (intLineNo=4, IL_FLASH=18)
     *   Ethernet: sem[16..23] (intLineNo=5, IL_ETHERNET=19)
     *   Printer:  sem[24..31] (intLineNo=6, IL_PRINTER=20)
     *   Terminal transmit: sem[32..39] (intLineNo=7, IL_TERMINAL=21)
     *   Terminal receive:  sem[40..47] (intLineNo=7, IL_TERMINAL=21)
     *   Pseudo-clock: sem[48]
     *
     * intLineNo = intNo - 14  (IL_DISK=17 → intLineNo=3, etc.)
     */
    if (intNo >= IL_DISK && intNo <= IL_TERMINAL) {
        int intLineNo = intNo - 14; /* Convert IL_* constant to line number 3-7 */
        int bitmapIndex = intLineNo - 3; /* Index into bitmap array (0-4) */

        /* Read the interrupting devices bitmap for this line */
        unsigned int *bitMapAddr = (unsigned int *)INTDEV_BITMAP;
        unsigned int devBitMap = bitMapAddr[bitmapIndex];

        /* Find highest priority device (lowest device number) */
        for (int devNo = 0; devNo < DEVPERINT; devNo++) {
            if (devBitMap & (1 << devNo)) {
                handleDeviceInterrupt(oldState, intLineNo, devNo);
                return;
            }
        }
    }

    /* Unrecognized or spurious interrupt: return control safely */
    if (currentProcess != NULL) {
        LDST(oldState);
    } else {
        scheduler();
    }
}