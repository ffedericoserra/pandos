/* Phase 3 - Virtual memory support: Pager, swap pool, TLB-Refill, flash I/O.
 *
 * uTLB_RefillHandler runs from the BIOS pass-up vector (no Support struct
 * indirection) and just copies the resident PTE into the TLB.
 *
 * The Pager runs at the Support level via sup_exceptContext[PGFAULTEXCEPT].
 * It implements the 14-step flow from the spec (§4.2): pick a frame using
 * a §10 free-frame fast-path with FIFO fall-back, evict its current
 * occupant if necessary (write-back to flash), read the missing page in,
 * update the swap-pool table, and re-validate the PTE atomically with the
 * TLB. swapMutex serialises the whole sequence across all U-procs.
 */

#include <uriscv/liburiscv.h>
#include <uriscv/types.h>
#include <uriscv/cpu.h>

#include "../headers/types.h"
#include "../headers/const.h"

#include "headers/vmSupport.h"
#include "headers/initProc.h"
#include "headers/sysSupport.h"

extern pcb_t *currentProcess;

/* Swap-pool table: one slot per RAM frame in [SWAPPOOLSTART, SWAPPOOLSTART + POOLSIZE*PAGESIZE). */
swap_t swapPool[POOLSIZE];
int    swapMutex;
static int    fifoNext;

/* (VPN - 0x80000) mod 32 maps text/data 0x80000..0x8001E to slots 0..30
 * and stack 0xBFFFF to slot 31 with no special case. */
static int pageIndex(unsigned int vpn) {
    return (vpn - 0x80000) & (MAXPAGES - 1);
}

/* Address of a flash device's register block. ASIDs are 1..UPROCMAX so
 * device index = asid-1. (IL_FLASH - IL_DISK) * 8 devices * 16 bytes = 0x80. */
static dtpreg_t *flashDevReg(int asid) {
    return (dtpreg_t *)(START_DEVREG + 0x80 + (asid - 1) * 0x10);
}

void initSwapStructs(void) {
    for (int i = 0; i < POOLSIZE; i++) {
        swapPool[i].sw_asid = NOPROC;
        swapPool[i].sw_pageNo = 0;
        swapPool[i].sw_pte = NULL;
    }
    swapMutex = 1;
    fifoNext = 0;
}

void uTLB_RefillHandler(void) {
    state_t *st = (state_t *)BIOSDATAPAGE;

    unsigned int vpn = (st->entry_hi & GETPAGENO) >> VPNSHIFT;
    int idx = pageIndex(vpn);

    pteEntry_t *pte = &currentProcess->p_supportStruct->sup_privatePgTbl[idx];

    setENTRYHI(pte->pte_entryHI);
    setENTRYLO(pte->pte_entryLO);
    TLBWR();

    LDST(st);
}

int flashOp(int asid, int blockNo, memaddr frameAddr, int isWrite) {
    dtpreg_t *flash = flashDevReg(asid);

    SYSCALL(PASSEREN, (int)&flashSem[asid - 1], 0, 0);

    flash->data0 = frameAddr;
    int command = (blockNo << 8) | (isWrite ? FLASHWRITE : FLASHREAD);
    int status = SYSCALL(DOIO, (int)&flash->command, command, 0);

    SYSCALL(VERHOGEN, (int)&flashSem[asid - 1], 0, 0);
    return status;
}

static int pickFrame(void) {
    /* §10 optimisation: prefer empty slots before evicting. */
    for (int i = 0; i < POOLSIZE; i++) {
        if (swapPool[i].sw_asid == NOPROC) return i;
    }
    int frame = fifoNext;
    fifoNext = (fifoNext + 1) % POOLSIZE;
    return frame;
}

void pager(void) {
    support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);
    state_t   *st  = &sup->sup_exceptState[PGFAULTEXCEPT];

    unsigned int excCode = st->cause & CAUSE_EXCCODE_MASK;
    if (excCode == EXC_MOD) {
        /* TLB-Modification on a writable page: treated as program trap. */
        programTrap(sup);
        return;
    }

    SYSCALL(PASSEREN, (int)&swapMutex, 0, 0);

    unsigned int vpn = (st->entry_hi & GETPAGENO) >> VPNSHIFT;
    int p = pageIndex(vpn);
    /* For the U-proc layout (text/data in pages 0..30, stack in slot 31)
     * the flash-block index matches the page-table slot. */
    int blockNo = p;

    int frame = pickFrame();
    memaddr frameAddr = SWAPPOOLSTART + frame * PAGESIZE;

    if (swapPool[frame].sw_asid != NOPROC) {
        int oldAsid          = swapPool[frame].sw_asid;
        int oldBlockNo       = swapPool[frame].sw_pageNo;
        pteEntry_t *oldPte   = swapPool[frame].sw_pte;

        unsigned int prev = getSTATUS();
        setSTATUS(prev & ~MSTATUS_MIE_MASK);
        oldPte->pte_entryLO &= ~VALIDON;
        TLBCLR();
        setSTATUS(prev);

        int wstatus = flashOp(oldAsid, oldBlockNo, frameAddr, 1);
        if ((wstatus & 0xFF) != DEV_READY) {
            SYSCALL(VERHOGEN, (int)&swapMutex, 0, 0);
            programTrap(sup);
            return;
        }
    }

    int rstatus = flashOp(sup->sup_asid, blockNo, frameAddr, 0);
    if ((rstatus & 0xFF) != DEV_READY) {
        SYSCALL(VERHOGEN, (int)&swapMutex, 0, 0);
        programTrap(sup);
        return;
    }

    swapPool[frame].sw_asid   = sup->sup_asid;
    swapPool[frame].sw_pageNo = p;
    swapPool[frame].sw_pte    = &sup->sup_privatePgTbl[p];

    unsigned int prev = getSTATUS();
    setSTATUS(prev & ~MSTATUS_MIE_MASK);
    sup->sup_privatePgTbl[p].pte_entryLO = frameAddr | DIRTYON | VALIDON;
    TLBCLR();
    setSTATUS(prev);

    SYSCALL(VERHOGEN, (int)&swapMutex, 0, 0);
    LDST(st);
}
