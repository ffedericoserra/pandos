/* Phase 3 - Support-level general exception handler.
 *
 * Receives every non-TLB exception that the Nucleus passes up via
 * passUpOrDie(GENERALEXCEPT). For SYSCALLs (ECALL from U-mode) it
 * dispatches on a0 to the four support-level services we provide:
 *
 *     SYS2 TERMINATE     - orderly U-proc termination
 *     SYS4 WRITETERMINAL - blocking write of a string to terminal 0
 *     SYS5 READTERMINAL  - blocking line read from terminal 0
 *     SYS6 EXECUTE       - shell-only spawn of a U-proc by ASID (M4)
 *
 * Anything else (program trap, unknown syscall, address/length error)
 * goes through programTrap, which V's the appropriate sync semaphore
 * and SYS2-terminates the U-proc.
 *
 * The Pager releases swapMutex itself before delegating here on flash
 * error (see vmSupport.c), so programTrap doesn't need to track it.
 */

#include <uriscv/liburiscv.h>
#include <uriscv/types.h>
#include <uriscv/cpu.h>

#include "../headers/types.h"
#include "../headers/const.h"
#include "../headers/utils.h"

#include "headers/sysSupport.h"
#include "headers/initProc.h"
#include "headers/vmSupport.h"

/* Terminal 0 device register block (line IL_TERMINAL=21, devNo=0). */
#define TERM0_ADDR ((termreg_t *)(START_DEVREG + (21 - 17) * 0x80))

#define SHELL_ASID 1

static void doSys2(support_t *sup);
static void doSys4(support_t *sup, state_t *st);
static void doSys5(support_t *sup, state_t *st);
static void doSys6(support_t *sup, state_t *st);
static int  validUserAddr(memaddr a);
static void resetUprocResources(int asid);

void programTrap(support_t *sup) {
    if (sup->sup_asid == 1) {
        SYSCALL(VERHOGEN, (int)&masterSem, 0, 0);
    } else {
        SYSCALL(VERHOGEN, (int)&shellSem, 0, 0);
    }
    SYSCALL(TERMPROCESS, 0, 0, 0);
}

void supportGeneralHandler(void) {
    support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);
    state_t   *st  = &sup->sup_exceptState[GENERALEXCEPT];

    unsigned int excCode = st->cause & CAUSE_EXCCODE_MASK;
    if (excCode != EXC_ECU && excCode != EXC_ECM) {
        programTrap(sup);
        return;
    }

    /* SYSCALL: dispatch on a0. PC is bumped before resuming. */
    int sysNo = (int)st->reg_a0;
    switch (sysNo) {
    case TERMINATE:
        doSys2(sup);
        return;
    case WRITETERMINAL:
        doSys4(sup, st);
        return;
    case READTERMINAL:
        doSys5(sup, st);
        return;
    case EXECUTE:
        doSys6(sup, st);
        return;
    default:
        programTrap(sup);
        return;
    }
}

static int validUserAddr(memaddr a) {
    return a >= (memaddr)KUSEGSTART && a < (memaddr)KUSEGEND;
}

static void doSys2(support_t *sup) {
    programTrap(sup); /* same termination sequence */
}

static void doSys4(support_t *sup, state_t *st) {
    memaddr addr = (memaddr)st->reg_a1;
    int     len  = (int)st->reg_a2;

    if (len < 0 || len > MAXSTRLENG) { programTrap(sup); return; }
    if (!validUserAddr(addr) || !validUserAddr(addr + len - 1)) {
        programTrap(sup); return;
    }

    char *s = (char *)addr;
    SYSCALL(PASSEREN, (int)&termWrSem, 0, 0);

    int written = 0;
    int retval  = 0;
    for (int i = 0; i < len; i++) {
        unsigned int cmd = TRANSMITCHAR | (((unsigned int)(unsigned char)s[i]) << 8);
        int status = SYSCALL(DOIO, (int)&TERM0_ADDR->transm_command, (int)cmd, 0);
        if ((status & 0xFF) != OKCHARTRANS) {
            retval = -(status & 0xFF);
            break;
        }
        written++;
    }
    if (retval == 0) retval = written;

    SYSCALL(VERHOGEN, (int)&termWrSem, 0, 0);

    st->reg_a0 = (unsigned int)retval;
    st->pc_epc += WORDLEN;
    LDST(st);
}

/* Erase any swap-pool entries owned by `asid` and reset that ASID's page
 * table back to V=0. Run before re-spawning a U-proc on the same ASID
 * so the Pager doesn't write fresh-process data over the flash backing
 * store thinking those frames still belong to the previous incarnation. */
static void resetUprocResources(int asid) {
    SYSCALL(PASSEREN, (int)&swapMutex, 0, 0);
    for (int i = 0; i < POOLSIZE; i++) {
        if (swapPool[i].sw_asid == asid) {
            swapPool[i].sw_asid = NOPROC;
            swapPool[i].sw_pte  = NULL;
        }
    }
    /* Best-effort TLB invalidation: TLBCLR drops every entry, which is
     * cheap-enough at EXECUTE granularity and avoids stale ASID hits. */
    TLBCLR();
    SYSCALL(VERHOGEN, (int)&swapMutex, 0, 0);

    pteEntry_t *pt = supports[asid - 1].sup_privatePgTbl;
    for (int i = 0; i < MAXPAGES; i++) {
        pt[i].pte_entryLO = DIRTYON; /* V=0, D=1, G=0 */
    }
}

static void doSys6(support_t *sup, state_t *st) {
    if (sup->sup_asid != SHELL_ASID) { programTrap(sup); return; }

    int asid = (int)st->reg_a1;
    if (asid < 2 || asid > UPROCMAX) { programTrap(sup); return; }

    resetUprocResources(asid);

    /* Refresh the initial state in case a previous incarnation left
     * stale GPRs (initUprocState is the same routine InstantiatorProcess
     * uses; we re-import the bits we need here without the helper). */
    state_t *us = &uprocStates[asid - 1];
    us->entry_hi = (unsigned int)asid << ASIDSHIFT;
    us->cause    = 0;
    us->status   = MSTATUS_MPP_U | MSTATUS_MPIE_MASK;
    us->pc_epc   = UPROCSTARTADDR;
    us->mie      = MIE_ALL;
    for (int i = 0; i < STATE_GPR_LEN; i++) us->gpr[i] = 0;
    us->reg_s9 = UPROCSTARTADDR;
    us->reg_sp = USERSTACKTOP;

    SYSCALL(CREATEPROCESS, (int)us, PROCESS_PRIO_LOW, (int)&supports[asid - 1]);

    /* Block the shell until the spawned U-proc terminates. */
    SYSCALL(PASSEREN, (int)&shellSem, 0, 0);

    st->reg_a0 = 0;
    st->pc_epc += WORDLEN;
    LDST(st);
}

static void doSys5(support_t *sup, state_t *st) {
    memaddr addr = (memaddr)st->reg_a1;
    if (!validUserAddr(addr)) { programTrap(sup); return; }

    char *buf = (char *)addr;
    SYSCALL(PASSEREN, (int)&termRdSem, 0, 0);

    int read   = 0;
    int retval = 0;
    int done   = 0;
    while (!done && read < MAXSTRLENG) {
        if (!validUserAddr(addr + read)) {
            retval = -1; /* generic address error mid-read */
            break;
        }
        int status = SYSCALL(DOIO, (int)&TERM0_ADDR->recv_command, RECEIVECHAR, 0);
        if ((status & 0xFF) != CHARRECV) {
            retval = -(status & 0xFF);
            break;
        }
        char c = (char)((status >> 8) & 0xFF);
        buf[read++] = c;
        if (c == '\n') done = 1;
    }
    if (retval == 0) retval = read;

    SYSCALL(VERHOGEN, (int)&termRdSem, 0, 0);

    st->reg_a0 = (unsigned int)retval;
    st->pc_epc += WORDLEN;
    LDST(st);
}
