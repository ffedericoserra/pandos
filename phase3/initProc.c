/* Phase 3 - InstantiatorProcess (replaces phase 2 `test`).
 *
 * Initialises the swap pool, semaphores, and Support Structures for all
 * U-procs, launches the shell (ASID 1), waits for it to terminate, then
 * issues NSYS2 so the kernel halts.
 *
 * Other U-procs (ASID 2..UPROCMAX) are spawned on demand by the shell
 * via SYS6 EXECUTE — handled in sysSupport.c.
 */

#include <uriscv/liburiscv.h>

#include "../headers/types.h"
#include "../headers/const.h"

#include "headers/initProc.h"
#include "headers/vmSupport.h"
#include "headers/sysSupport.h"

/* One Support Structure and one initial processor state per U-proc.
 * Indexed by (ASID - 1). Shared with sysSupport.c via initProc.h. */
support_t supports[UPROCMAX];
state_t   uprocStates[UPROCMAX];

/* masterSem: test() blocks here until the shell calls SYS2 (starts at 0).
 * shellSem:  shell blocks here after each SYS6 until the child exits (starts at 0).
 * termRdSem: mutex for terminal 0 receive sub-device (starts at 1).
 * termWrSem: mutex for terminal 0 transmit sub-device (starts at 1).
 * flashSem:  one mutex per flash device, used by flashIO() in vmSupport.c (starts at 1). */
int masterSem;
int shellSem;
int termRdSem;
int termWrSem;
int flashSem[UPROCMAX];

/* Handler contexts run kernel-mode with interrupts enabled. */
#define HANDLER_STATUS (MSTATUS_MPP_M | MSTATUS_MIE_MASK)

/* U-proc initial state: drop to user-mode with interrupts enabled after MRET. */
#define UPROC_STATUS (MSTATUS_MPP_U | MSTATUS_MPIE_MASK)

/* Initialise the 32-entry private page table for U-proc `asid`.
 * Entries 0..30: text/data pages, VPN 0x80000..0x8001E.
 * Entry 31:      stack page,      VPN 0xBFFFF (SP starts at 0xC0000000).
 * All entries: D=1 (writable), V=0 (not in RAM yet -> will page-fault). */
static void initPageTable(support_t *sup, int asid) {
    unsigned int asidBits = (unsigned int)asid << ASIDSHIFT;

    for (int i = 0; i < MAXPAGES - 1; i++) {
        unsigned int vpn = 0x80000 + i;
        sup->sup_privatePgTbl[i].pte_entryHI = (vpn << VPNSHIFT) | asidBits;
        sup->sup_privatePgTbl[i].pte_entryLO = DIRTYON; /* V=0, D=1, G=0 */
    }
    /* Stack page: VPN 0xBFFFF, one page below USERSTACKTOP. */
    sup->sup_privatePgTbl[MAXPAGES - 1].pte_entryHI =
        (0xBFFFF << VPNSHIFT) | asidBits;
    sup->sup_privatePgTbl[MAXPAGES - 1].pte_entryLO = DIRTYON;
}

/* Initialise the Support Structure for U-proc `asid`:
 * set the ASID, build its page table, and configure both exception contexts
 * (TLB faults -> pager, everything else -> supportGeneralHandler).
 * Stack pointers point to the END of each array (stacks grow downward). */
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

/* Build the initial processor state for U-proc `asid`.
 * PC and s9 both hold UPROCSTARTADDR (µRISCV convention).
 * SP starts at USERSTACKTOP (0xC0000000); all GPRs zeroed. */
static void initUprocState(state_t *st, int asid) {
    st->entry_hi = (unsigned int)asid << ASIDSHIFT;
    st->cause    = 0;
    st->status   = UPROC_STATUS;
    st->pc_epc   = UPROCSTARTADDR;
    st->mie      = MIE_ALL;
    for (int i = 0; i < STATE_GPR_LEN; i++) st->gpr[i] = 0;
    st->reg_s9   = UPROCSTARTADDR;
    st->reg_sp   = USERSTACKTOP;
}

/* InstantiatorProcess — entry point for Phase 3. */
void test(void) {

    /* Initialise Swap Pool table and swapPoolSem (defined in vmSupport.c). */
    initSwapPool();

    /* masterSem/shellSem = 0: callers will block until the child V's them.
     * I/O semaphores = 1: standard mutex initialisation. */
    masterSem = 0;
    shellSem  = 0;
    termRdSem = 1;
    termWrSem = 1;
    for (int i = 0; i < UPROCMAX; i++) flashSem[i] = 1;

    /* Pre-build Support Structures and initial states for all ASIDs so that
     * SYS6 only needs to reset the V bits before re-launching a U-proc. */
    for (int i = 1; i <= UPROCMAX; i++) {
        initSupport(&supports[i - 1], i);
        initUprocState(&uprocStates[i - 1], i);
    }

    /* Launch the shell (ASID 1). All other U-procs are spawned by the shell
     * via SYS6. PROCESS_PRIO_LOW is used for all U-procs. */
    SYSCALL(CREATEPROCESS, (int)&uprocStates[0], PROCESS_PRIO_LOW, (int)&supports[0]);

    /* Block until the shell exits. The shell's SYS2 handler V's masterSem
     * just before issuing NSYS2, waking test() exactly once. */
    SYSCALL(PASSEREN, (int)&masterSem, 0, 0);

    /* Shell has terminated: process count drops to zero -> kernel HALT. */
    SYSCALL(TERMPROCESS, 0, 0, 0);
}
