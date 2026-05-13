/* Phase 3 - InstantiatorProcess (replaces phase 2 `test`).
 *
 * Initialises the swap pool, the support-level mutex and synchronisation
 * semaphores, builds a Support structure for every U-proc (ASID 1..UPROCMAX),
 * launches the shell U-proc (ASID 1) via NSYS1, blocks on masterSem until
 * the shell calls SYS2, and finally NSYS2-terminates so the kernel halts.
 *
 * Other U-procs (ASID 2..UPROCMAX) are launched on demand by the shell via
 * SYS6 EXECUTE — handled in sysSupport.c.
 */

#include <uriscv/liburiscv.h>

#include "../headers/types.h"
#include "../headers/const.h"

#include "headers/initProc.h"
#include "headers/vmSupport.h"
#include "headers/sysSupport.h"

support_t supports[UPROCMAX];
state_t   uprocStates[UPROCMAX];

int masterSem;
int shellSem;
int termRdSem;
int termWrSem;
int flashSem[UPROCMAX];

/* Status word for support-level handler contexts: kernel mode, MIE on. */
#define HANDLER_STATUS (MSTATUS_MPP_M | MSTATUS_MIE_MASK)

/* Status word for U-proc initial state (LDST'd by createProcess via the
 * scheduler): MPP=U so the post-MRET privilege drops to user, MPIE so MIE
 * becomes 1, mie=MIE_ALL so all interrupts unmask. */
#define UPROC_STATUS (MSTATUS_MPP_U | MSTATUS_MPIE_MASK)

static void initPageTable(support_t *sup, int asid) {
    unsigned int asidBits = (unsigned int)asid << ASIDSHIFT;

    /* Text/data pages 0..30 → VPN 0x80000..0x8001E. */
    for (int i = 0; i < MAXPAGES - 1; i++) {
        unsigned int vpn = 0x80000 + i;
        sup->sup_privatePgTbl[i].pte_entryHI = (vpn << VPNSHIFT) | asidBits;
        sup->sup_privatePgTbl[i].pte_entryLO = DIRTYON; /* V=0, D=1, G=0 */
    }
    /* Stack page 31 → VPN 0xBFFFF. */
    sup->sup_privatePgTbl[MAXPAGES - 1].pte_entryHI =
        (0xBFFFF << VPNSHIFT) | asidBits;
    sup->sup_privatePgTbl[MAXPAGES - 1].pte_entryLO = DIRTYON;
}

static void initSupport(support_t *sup, int asid) {
    sup->sup_asid = asid;
    initPageTable(sup, asid);

    sup->sup_exceptContext[PGFAULTEXCEPT].pc       = (memaddr)pager;
    sup->sup_exceptContext[PGFAULTEXCEPT].stackPtr = (memaddr)&sup->sup_stackTLB[499];
    sup->sup_exceptContext[PGFAULTEXCEPT].status   = HANDLER_STATUS;

    sup->sup_exceptContext[GENERALEXCEPT].pc       = (memaddr)supportGeneralHandler;
    sup->sup_exceptContext[GENERALEXCEPT].stackPtr = (memaddr)&sup->sup_stackGen[499];
    sup->sup_exceptContext[GENERALEXCEPT].status   = HANDLER_STATUS;
}

static void initUprocState(state_t *st, int asid) {
    /* zero everything explicitly, then fill the load-bearing fields */
    st->entry_hi = (unsigned int)asid << ASIDSHIFT;
    st->cause    = 0;
    st->status   = UPROC_STATUS;
    st->pc_epc   = UPROCSTARTADDR;
    st->mie      = MIE_ALL;
    for (int i = 0; i < STATE_GPR_LEN; i++) st->gpr[i] = 0;
    /* Per the µRISCV state mapping, both PC and s9 hold the entry point. */
    st->reg_s9   = UPROCSTARTADDR;
    st->reg_sp   = USERSTACKTOP;
}

void test(void) {
    initSwapPool();

    masterSem = 0;
    shellSem  = 0;
    termRdSem = 1;
    termWrSem = 1;
    for (int i = 0; i < UPROCMAX; i++) flashSem[i] = 1;

    for (int i = 1; i <= UPROCMAX; i++) {
        initSupport(&supports[i - 1], i);
        initUprocState(&uprocStates[i - 1], i);
    }

    /* Launch the shell only (ASID 1). The shell spawns children via SYS6. */
    SYSCALL(CREATEPROCESS, (int)&uprocStates[0], PROCESS_PRIO_LOW, (int)&supports[0]);

    /* Wait for the shell to terminate. */
    SYSCALL(PASSEREN, (int)&masterSem, 0, 0);

    SYSCALL(TERMPROCESS, 0, 0, 0);
}
