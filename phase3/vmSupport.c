#include <uriscv/liburiscv.h>
#include <uriscv/cpu.h>
 
#include "../headers/types.h"
#include "../headers/const.h"
#include "../headers/utils.h"
 
#include "../phase1/headers/pcb.h"
#include "../phase2/headers/exceptions.h"
#include "../phase2/headers/syscalls.h"
 
#include "headers/vmSupport.h"



extern pcb_t *currentProcess;
extern int    deviceSemaphores[];

/* --------------------------------------------------------------------------
 * Swap Pool table – one entry per Swap Pool frame.
 * sw_asid == -1  =>  frame is free.
 * -------------------------------------------------------------------------- */
swap_t swapPool[POOLSIZE];
 
/* --------------------------------------------------------------------------
 * FIFO page-replacement pointer.
 * -------------------------------------------------------------------------- */
static int nextFrame = 0;
int swapPoolSem; /* mutex on Swap Pool table — initialized by initSwapStructs() */



// Called when the page is valid (V=1 in the page table), but not present in the TLB.
// Then just reload the TLB
void uTLB_RefillHandler() {

    state_t *state = GET_EXCEPTION_STATE_PTR(0);

    unsigned int entryHi = state->entry_hi;

    int vpn = entryHi >> VPNSHIFT;

    // Retrieve the support structure of the current process
    support_t *sup = currentProcess->p_supportStruct;

    // Translate VPN into page table index

    int index;

    if (vpn >= 0x80000 && vpn <= 0x8001E) {
        //pages
        index = vpn - 0x80000;
    }
    else if (vpn == 0xBFFFF) {
        //last page
        index = 31;
    }
    else {
        index = 0;
    }

    //Retrieve entry from page table
    pteEntry_t entry = sup->sup_privatePgTbl[index];

    //Copy entryHI and entryLO(VPN + ASID)
    setENTRYHI(entry.pte_entryHI);
    setENTRYLO(entry.pte_entryLO);

    //Writing in the TLB
    TLBWR();
    LDST(state);
}

//Initialize page table
//Create a process's private page table
void initPageTable(support_t *sup, int asid) {

    for (int i = 0; i < USERPGTBLSIZE; i++) {

        unsigned int vpn;
        unsigned int entryHI;
        unsigned int entryLO;

        //calculate vpn
        if (i < USERPGTBLSIZE - 1) {
            //pages
            vpn = 0x80000 + i;
        } else {
            //last page=stack
            vpn = 0xBFFFF;
        }

        entryHI = (vpn << VPNSHIFT) | (asid << ASIDSHIFT);

        //PFN = 0 -> page not yet loaded
        //DIRTY = 1 -> writable
        //VALID = 0 -> not valid, will cause page fault
        entryLO = DIRTYON;

        //writing to th page table
        sup->sup_privatePgTbl[i].pte_entryHI = entryHI;
        sup->sup_privatePgTbl[i].pte_entryLO = entryLO;
    }
}

//Initialize swap pool
void initSwapPool() {

    swapPoolSem = 1;  // Semaphore initialized to 1


    for (int i = 0; i < POOLSIZE; i++) {

        // free frame
        swapPool[i].sw_asid = -1;

        // default values
        swapPool[i].sw_pageNo = -1;
        swapPool[i].sw_pte = NULL;
    }

   }


//Select the next frame to use
int getFrameFIFO() {

    //get the current frame
    int frame = nextFrame;

    //update index
    nextFrame = (nextFrame + 1) % POOLSIZE;

    return frame;
}

//Disable interrupt
//Necessary for atomic operations on TLB / page table
void disableInterrupts() {
    setSTATUS(getSTATUS() & ~MSTATUS_MIE_MASK);
}

//enable interrupt
void enableInterrupts() {
    setSTATUS(getSTATUS() | MSTATUS_MIE_MASK);
}

// Invalidate all TLB entries
void updateTLB() {
    TLBCLR();
}



/* ==========================================================================
 * pager
 *
 * TLB exception handler (page-fault handler).  Handles TLB-Invalid exceptions
 * (TLBL / TLBS) passed up from the Nucleus.  Runs kernel-mode, interrupts ON.
 *
 * Algorithm (§4.2, steps 1-14):
 * ========================================================================== */
void pager(void)
{
    /* ------------------------------------------------------------------
     * Step 1: obtain the current process's Support Structure (NSYS8).
     * ------------------------------------------------------------------ */
    support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);
 
    /* ------------------------------------------------------------------
     * Step 2: read the exception cause from the saved exception state.
     * ------------------------------------------------------------------ */
    state_t *exceptState = &sup->sup_exceptState[PGFAULTEXCEPT];
    unsigned int cause   =  exceptState->cause & CAUSE_EXCCODE_MASK;
 
    /* ------------------------------------------------------------------
     * Step 3: TLB-Modification (write to read-only page) should never
     * occur since all PTEs have D=1.  Treat as a program trap (§8).
     * ------------------------------------------------------------------ */
    if (cause == EXC_MOD) {
        programTrapKill(0);   /* not holding swapPoolSem yet */
        return;
    }
 
    /* ------------------------------------------------------------------
     * Step 4: P(swapPoolSem) – serialise access to the Swap Pool.
     * ------------------------------------------------------------------ */
    SYSCALL(PASSEREN, (unsigned int)&swapPoolSem, 0, 0);
 
    /* ------------------------------------------------------------------
     * Step 5: determine the missing page index p.
     * ------------------------------------------------------------------ */
    int pageIndex = getPageIndex(exceptState->entry_hi);
    int asid      = sup->sup_asid;
 
    /* ------------------------------------------------------------------
     * Step 6: pick the next Swap Pool frame (FIFO round-robin).
     * ------------------------------------------------------------------ */
    int frameIndex = getFrameFIFO(); 
 
    /* ------------------------------------------------------------------
     * Steps 7-8: handle an occupied frame (evict the victim page).
     * ------------------------------------------------------------------ */
    swap_t *swapEntry = &swapPool[frameIndex];
 
    if (swapEntry->sw_asid != -1) {
        pteEntry_t *victimPte = swapEntry->sw_pte;
 
        /* 8a+8b (ATOMIC): invalidate victim's PTE and sync TLB.
         *         Must happen BEFORE writing to backing store (§5.3). */
        setSTATUS(getSTATUS() & ~MSTATUS_MIE_MASK);   /* disable ints */
        victimPte->pte_entryLO &= ~VALIDON;            /* V bit = 0    */
        updateTLB(victimPte);
        setSTATUS(getSTATUS() |  MSTATUS_MIE_MASK);   /* enable ints  */
 
        /* 8c: write the victim page to its flash backing store. */
        int wStatus = flashIO(swapEntry->sw_asid,
                              swapEntry->sw_pageNo,
                              FRAME_ADDR(frameIndex),
                              FLASHWRITE);
 
        if (wStatus != FLASH_STATUS_OK) {
            programTrapKill(1);   /* releases swapPoolSem first */
            return;
        }
    }
 
    /* ------------------------------------------------------------------
     * Step 9: read page p from the current process's flash device into
     * Swap Pool frame i.  Must happen BEFORE updating the PTE (§5.3).
     * ------------------------------------------------------------------ */
    int rStatus = flashIO(asid, pageIndex, FRAME_ADDR(frameIndex), FLASHREAD);
 
    if (rStatus != FLASH_STATUS_OK) {
        programTrapKill(1);
        return;
    }
 
    /* ------------------------------------------------------------------
     * Step 10: update the Swap Pool table entry.
     * ------------------------------------------------------------------ */
    swapEntry->sw_asid   = asid;
    swapEntry->sw_pageNo = pageIndex;
    swapEntry->sw_pte    = &sup->sup_privatePgTbl[pageIndex];
 
    /* ------------------------------------------------------------------
     * Steps 11-12 (ATOMIC): update the current PTE (V=1, PFN=frame i)
     * and flush the stale TLB entry.
     *
     * entryLO bit layout (const.h):
     *   bits [31:12] – PFN  (= FRAME_ADDR(i), page-aligned -> bits[11:0]=0)
     *   bit  10      – D (Dirty)  = 1
     *   bit   9      – V (Valid)  = 1
     * ------------------------------------------------------------------ */
    pteEntry_t *pte = &sup->sup_privatePgTbl[pageIndex];
 
    setSTATUS(getSTATUS() & ~MSTATUS_MIE_MASK);
    pte->pte_entryLO = FRAME_ADDR(frameIndex) | DIRTYON | VALIDON;
    updateTLB(pte);
    setSTATUS(getSTATUS() |  MSTATUS_MIE_MASK);
 
    /* ------------------------------------------------------------------
     * Step 13: V(swapPoolSem) – release Swap Pool mutual exclusion.
     * ------------------------------------------------------------------ */
    SYSCALL(VERHOGEN, (unsigned int)&swapPoolSem, 0, 0);
 
    /* ------------------------------------------------------------------
     * Step 14: LDST to retry the faulting instruction.
     * ------------------------------------------------------------------ */
    LDST(exceptState);
}


 
/* ==========================================================================
 * flashIO  (static helper)
 *
 * Read (op=FLASHREAD) or write (op=FLASHWRITE) one 4 KiB page between a
 * Swap Pool frame and a U-proc's flash backing-store device.
 *
 *   asid      – U-proc ASID (1..8); flash device number = asid - 1.
 *   pageNo    – Flash block number (0..31).
 *   frameAddr – Physical base address of the Swap Pool frame.
 *   op        – FLASHREAD (2) or FLASHWRITE (3).
 *
 * Returns the device status: FLASH_STATUS_OK (1) = success.
 *
 * Device register address (from interrupts.c convention):
 *   intLineNo  = IL_FLASH - 14 = 4
 *   devRegAddr = START_DEVREG + (intLineNo-3)*0x80 + devNo*0x10
 * ========================================================================== */
static int flashIO(int asid, int pageNo, memaddr frameAddr, int op)
{
    int devNo = asid - 1;
 
    memaddr devRegAddr = START_DEVREG
                       + ((FLASH_LINE - 3) * 0x80)
                       + (devNo * 0x10);
    dtpreg_t *flashReg = (dtpreg_t *)devRegAddr;
 
    /* Step 1: set DATA0 to the frame's physical base address. */
    flashReg->data0 = (unsigned int)frameAddr;
 
    /* Step 2: acquire per-device mutex then issue DOIO (NSYS5).
     *         DOIO writes COMMAND and blocks until the interrupt fires. */
    SYSCALL(PASSEREN, (unsigned int)&flashSem[devNo], 0, 0);
 
    unsigned int cmd = ((unsigned int)pageNo << 8) | (unsigned int)op;
    int status = SYSCALL(DOIO, (unsigned int)&flashReg->command, (int)cmd, 0);
 
    SYSCALL(VERHOGEN, (unsigned int)&flashSem[devNo], 0, 0);
 
    return status;
}
