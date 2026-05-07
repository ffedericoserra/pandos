/* Support-level general exception handling */

#include <uriscv/liburiscv.h>
#include <uriscv/types.h>
#include <uriscv/cpu.h>

#include "../headers/types.h"
#include "../headers/const.h"
#include "../headers/utils.h"

#include "headers/sysSupport.h"
#include "headers/initProc.h"
#include "headers/vmSupport.h"

#define SHELL_ASID 1                                            /* ASID reserved for the shell U-proc (only one allowed to SYS6) */
#define TERM0_ADDR ((termreg_t *)(START_DEVREG + (21 - 17) * 0x80))  /* Terminal 0 register block (line IL_TERMINAL=21, devNo=0) */

static void doSys2(support_t *sup);
static void doSys4(support_t *sup, state_t *st);
static void doSys5(support_t *sup, state_t *st);
static void doSys6(support_t *sup, state_t *st);
static int validUserAddr(memaddr a);
static void resetUprocResources(int asid);


extern int swapPoolSem;
extern swap_t swapPool[POOLSIZE]; 
/*
 * supportGeneralHandler
 * Entry point for every non-TLB exception the Nucleus passes up via
 * passUpOrDie(GENERALEXCEPT). For SYSCALLs (ECALL from U-mode) it
 * dispatches on a0 to SYS2/SYS4/SYS5/SYS6; everything else is a fatal
 * error for the offending U-proc and goes through programTrap.
 */
void supportGeneralHandler() {
    support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);
    state_t   *st  = &sup->sup_exceptState[GENERALEXCEPT];

    unsigned int excCode = st->cause & CAUSE_EXCCODE_MASK;
    if (excCode != EXC_ECU && excCode != EXC_ECM) { /* not a SYSCALL: program trap */
        programTrap(sup);
        return;
    }

    /* SYSCALL: dispatch on a0. PC is bumped by each handler before resuming. */
    int sysNo = (int)st->reg_a0;
    switch (sysNo) {
        case TERMINATE:     doSys2(sup); break;
        case WRITETERMINAL: doSys4(sup, st); break;
        case READTERMINAL:  doSys5(sup, st); break;
        case EXECUTE:       doSys6(sup, st); break;
        default:            programTrap(sup); break;
    }
}

/*
 * SYS2 - Terminate
 * Orderly U-proc termination. Delegates to programTrap, which V's the
 * appropriate sync semaphore and SYS2's via the Nucleus.
 */
static void doSys2(support_t *sup) {
    programTrap(sup);
}

/*
 * SYS4 - WriteTerminal
 * Writes `a2` characters from virtual address `a1` to terminal 0 under
 * mutual exclusion on termWrSem. Returns the count in a0 on success or
 * the negated low byte of the device status on the first error.
 * Address/length errors terminate the U-proc via programTrap.
 */
static void doSys4(support_t *sup, state_t *st) {
    memaddr addr = (memaddr)st->reg_a1;
    int     len  = (int)st->reg_a2;

    if (len < 0 || len > MAXSTRLENG) { /* length out of range */
        programTrap(sup);
        return;
    }
    if (!validUserAddr(addr) || !validUserAddr(addr + len - 1)) { /* buffer outside kuseg */
        programTrap(sup);
        return;
    }

    char *s = (char *)addr;
    SYSCALL(PASSEREN, (int)&termWrSem, 0, 0); /* P(termWrSem) */

    int written = 0;
    int retval = 0;
    for (int i = 0; i < len; i++) { /* push one char at a time through TRANSMITCHAR */
        unsigned int cmd = TRANSMITCHAR | (((unsigned int)(unsigned char)s[i]) << 8);
        int status = SYSCALL(DOIO, (int)&TERM0_ADDR->transm_command, (int)cmd, 0);
        if ((status & 0xFF) != OKCHARTRANS) { /* device error: stop and report */
            retval = -(status & 0xFF);
            break;
        }
        written++;
    }
    if (retval == 0) retval = written;

    SYSCALL(VERHOGEN, (int)&termWrSem, 0, 0); /* V(termWrSem) */

    st->reg_a0 = (unsigned int)retval;
    st->pc_epc += WORDLEN; /* skip past the ECALL */
    LDST(st);
}

/*
 * SYS5 - ReadTerminal
 * Reads a line from terminal 0 into the buffer at virtual address `a1`
 * under mutual exclusion on termRdSem. Stops at '\n' or MAXSTRLENG.
 * Returns the count in a0 on success or the negated low byte of the
 * device status on error. Address errors terminate via programTrap.
 */
static void doSys5(support_t *sup, state_t *st) {
    memaddr addr = (memaddr)st->reg_a1;
    if (!validUserAddr(addr)) { /* buffer outside kuseg */
        programTrap(sup);
        return;
    }

    char *buf = (char *)addr;
    SYSCALL(PASSEREN, (int)&termRdSem, 0, 0); /* P(termRdSem) */

    int read = 0;
    int retval = 0;
    int done = 0;
    while (!done && read < MAXSTRLENG) {
        if (!validUserAddr(addr + read)) { /* buffer slid out of kuseg mid-read */
            retval = -1;
            break;
        }
        int status = SYSCALL(DOIO, (int)&TERM0_ADDR->recv_command, RECEIVECHAR, 0);
        if ((status & 0xFF) != CHARRECV) { /* device error: stop and report */
            retval = -(status & 0xFF);
            break;
        }
        char c = (char)((status >> 8) & 0xFF); /* received byte sits in bits 15:8 */
        buf[read++] = c;
        if (c == '\n') done = 1;
    }
    if (retval == 0) retval = read;

    SYSCALL(VERHOGEN, (int)&termRdSem, 0, 0); /* V(termRdSem) */

    st->reg_a0 = (unsigned int)retval;
    st->pc_epc += WORDLEN; /* skip past the ECALL */
    LDST(st);
}

/*
 * SYS6 - Execute
 * Shell-only spawn of a U-proc by ASID. Validates the caller is the
 * shell, scrubs any stale swap-pool/page-table state for the target
 * ASID, builds a fresh initial state, and asks the Nucleus to create
 * the process via NSYS1. Blocks the shell on shellSem until the child
 * SYS2-terminates.
 */
static void doSys6(support_t *sup, state_t *st) {
    if (sup->sup_asid != SHELL_ASID) { /* only the shell may EXECUTE */
        programTrap(sup);
        return;
    }

    int asid = (int)st->reg_a1;
    if (asid < 2 || asid > UPROCMAX) { /* must address a non-shell U-proc */
        programTrap(sup);
        return;
    }

    resetUprocResources(asid); /* drop stale state from any previous incarnation */

    /* Refresh the initial state in case a previous incarnation left
     * stale GPRs. Mirrors the setup InstantiatorProcess does at boot. */
    state_t *us = &uprocStates[asid - 1];
    us->entry_hi = (unsigned int)asid << ASIDSHIFT;
    us->cause    = 0;
    us->status   = MSTATUS_MPP_U | MSTATUS_MPIE_MASK; /* drop to U-mode with MIE on after MRET */
    us->pc_epc   = UPROCSTARTADDR;
    us->mie      = MIE_ALL;
    for (int i = 0; i < STATE_GPR_LEN; i++) us->gpr[i] = 0;
    us->reg_s9 = UPROCSTARTADDR;
    us->reg_sp = USERSTACKTOP;

    SYSCALL(CREATEPROCESS, (int)us, PROCESS_PRIO_LOW, (int)&supports[asid - 1]);

    SYSCALL(PASSEREN, (int)&shellSem, 0, 0); /* block until the child V's shellSem on exit */

    st->reg_a0 = 0;
    st->pc_epc += WORDLEN; /* skip past the ECALL */
    LDST(st);
}

/*
 * programTrap
 * Orderly termination path used on every fatal U-proc error (program
 * trap, unknown SYSCALL, address/length validation failure, flash error
 * from the Pager). V's masterSem if the victim is the shell so that
 * InstantiatorProcess wakes; V's shellSem otherwise so that the shell
 * resumes from its SYS6 block. Then asks the Nucleus to terminate the
 * U-proc via NSYS2.
 *
 * The Pager releases swapMutex itself before delegating here on flash
 * error (see vmSupport.c), so this routine doesn't track that ownership.
 */
void programTrap(support_t *sup) {
    if (sup->sup_asid == SHELL_ASID) {
        SYSCALL(VERHOGEN, (int)&masterSem, 0, 0);
    } else {
        SYSCALL(VERHOGEN, (int)&shellSem, 0, 0);
    }
    SYSCALL(TERMPROCESS, 0, 0, 0);
}


/* ------------------------ HELPERS ------------------------ */

/*
 * Range-check a virtual address against the U-proc's kuseg window
 * [KUSEGSTART, KUSEGEND). Returns 1 if valid, 0 otherwise.
 */
static int validUserAddr(memaddr a) {
    return a >= (memaddr)KUSEGSTART && a < (memaddr)KUSEGEND;
}

/*
 * Erase any swap-pool entries owned by `asid` and reset that ASID's
 * page table back to V=0. Run before re-spawning a U-proc on the same
 * ASID so the Pager doesn't write fresh-process data over the flash
 * backing store thinking those frames still belong to the previous
 * incarnation. Holds swapMutex around the swap-pool walk; TLBCLR drops
 * any stale TLB entries that might still match the old PTEs.
 */
static void resetUprocResources(int asid) {
    SYSCALL(PASSEREN, (int)&swapPoolSem, 0, 0); /* P(swapPoolSem) */
    for (int i = 0; i < POOLSIZE; i++) {
        if (swapPool[i].sw_asid == asid) {
            swapPool[i].sw_asid = NOPROC;
            swapPool[i].sw_pte  = NULL;
        }
    }
    TLBCLR(); /* coarse but cheap at EXECUTE granularity */
    SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0); /* V(swapMutex) */

    pteEntry_t *pt = supports[asid - 1].sup_privatePgTbl;
    for (int i = 0; i < MAXPAGES; i++) {
        pt[i].pte_entryLO = DIRTYON; /* V=0, D=1, G=0 */
    }
}