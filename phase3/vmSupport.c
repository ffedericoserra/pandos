#include <uriscv/liburiscv.h>
#include <uriscv/cpu.h>
 
#include "../headers/types.h"
#include "../headers/const.h"
#include "../headers/utils.h"
 
#include "../phase1/headers/pcb.h"
#include "../phase2/headers/exceptions.h"
#include "../phase2/headers/syscalls.h"
 
#include "headers/vmSupport.h"
#include "headers/sysSupport.h"   /* programTrap */



extern pcb_t *currentProcess;
extern int    deviceSemaphores[];

/* Forward declarations for helpers defined later in this file. */
static int flashIO(int asid, int pageNo, memaddr frameAddr, int op);
int        getPageIndex(unsigned int entryHi);


//Swap Pool table – one entry per Swap Pool frame.
//sw_asid == -1  =>  frame is free.
swap_t swapPool[POOLSIZE];
 

//FIFO page-replacement pointer.
static int nextFrame = 0;
//mutex on Swap Pool table — initialized by initSwapStructs()
int swapPoolSem;



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

/* Translate entry_hi -> table page index (0..31).
 * Same mapping used by uTLB_RefillHandler:
 * VPN 0x80000..0x8001E -> slot 0..30 (text/data), VPN 0xBFFFF -> slot 31 (stack). */
int getPageIndex(unsigned int entryHi) {
    int vpn = entryHi >> VPNSHIFT;
    if (vpn >= 0x80000 && vpn <= 0x8001E) {
        return vpn - 0x80000;
    } else if (vpn == 0xBFFFF) {
        return 31;
    } else {
        return 0;
    }
}

/* Terminates the current U-proc neatly.
 * holdsMutex == 1: The caller holds swapPoolSem; we release it first
 * holdsMutex == 0: The caller does not hold it.
 * Retrieve the support struct and delegate to programTrap (sysSupport.c), which
* wakes up the appropriate sync sem (masterSem/shellSem) and calls SYS2 TERMPROCESS.*/
void programTrapKill(int holdsMutex) {
    if (holdsMutex) {
        SYSCALL(VERHOGEN, (unsigned int)&swapPoolSem, 0, 0);
    }
    support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);
    programTrap(sup);
}


//TLB exception handler (page-fault handler).  Handles TLB-Invalid exceptions
//(TLBL / TLBS) passed up from the Nucleus.  Runs kernel-mode, interrupts ON.
void pager(void)
{

    //Step 1: obtain the current process's Support Structure (NSYS8).
    support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);
 
    
    //Step 2: read the exception cause from the saved exception state.
    state_t *exceptState = &sup->sup_exceptState[PGFAULTEXCEPT];
    unsigned int cause   =  exceptState->cause & CAUSE_EXCCODE_MASK;
 
    //Step 3: TLB-Modification (write to read-only page) should never occur since all PTEs have D=1.
    if (cause == EXC_MOD) {
        programTrapKill(0);   // not holding swapPoolSem
        return;
    }
 
    //Step 4: P(swapPoolSem) – serialise access to the Swap Pool.
    SYSCALL(PASSEREN, (unsigned int)&swapPoolSem, 0, 0);
 
    //Step 5: determine the missing page index p.
    int pageIndex = getPageIndex(exceptState->entry_hi);
    int asid      = sup->sup_asid;
 
    //Step 6: pick the next Swap Pool frame (FIFO round-robin).
    int frameIndex = getFrameFIFO(); 
 
    //Steps 7-8: handle an occupied frame (evict the victim page).
    swap_t *swapEntry = &swapPool[frameIndex];
 
    if (swapEntry->sw_asid != -1) {
        pteEntry_t *victimPte = swapEntry->sw_pte;
 
        //Steps 8a+8b (ATOMIC): invalidate victim's PTE and sync TLB.
        //Must happen BEFORE writing to backing store.
        setSTATUS(getSTATUS() & ~MSTATUS_MIE_MASK);  //disable interrupts
        victimPte->pte_entryLO &= ~VALIDON;            // V bit = 0
        updateTLB();
        setSTATUS(getSTATUS() |  MSTATUS_MIE_MASK);   //enable interrupts
 
        //Steps 8c: write the victim page to its flash backing store.
        int wStatus = flashIO(swapEntry->sw_asid,
                              swapEntry->sw_pageNo,
                              FRAME_ADDR(frameIndex),
                              FLASHWRITE);
 
        if (wStatus != FLASH_STATUS_OK) {
            programTrapKill(1);   //releases swapPoolSem first
            return;
        }
    }
 
    //Step 9: read page p from the current process's flash device into Swap Pool frame i.
    int rStatus = flashIO(asid, pageIndex, FRAME_ADDR(frameIndex), FLASHREAD);

    //On flash error
    if (rStatus != FLASH_STATUS_OK) {
        programTrapKill(1);
        return;
    }
 
    //Step 10: update the Swap Pool table entry.
    swapEntry->sw_asid   = asid;
    swapEntry->sw_pageNo = pageIndex;
    swapEntry->sw_pte    = &sup->sup_privatePgTbl[pageIndex];
 

    //Steps 11-12 (ATOMIC): update the current PTE (V=1, PFN=frame i) and flush the stale TLB entry.
    pteEntry_t *pte = &sup->sup_privatePgTbl[pageIndex];
    setSTATUS(getSTATUS() & ~MSTATUS_MIE_MASK);
    pte->pte_entryLO = FRAME_ADDR(frameIndex) | DIRTYON | VALIDON;
    updateTLB();
    setSTATUS(getSTATUS() |  MSTATUS_MIE_MASK);
 
    //Step 13: V(swapPoolSem) – release Swap Pool mutual exclusion.
    SYSCALL(VERHOGEN, (unsigned int)&swapPoolSem, 0, 0);
 
    //Step 14: LDST to retry the faulting instruction.
    LDST(exceptState);
}


 
/*
Read (op=FLASHREAD) or write (op=FLASHWRITE) one 4 KiB page between a Swap Pool frame and a U-proc's flash backing-store device.
    asid      – U-proc ASID (1..8); flash device number = asid - 1.
    pageNo    – Flash block number (0..31).
    frameAddr – Physical base address of the Swap Pool frame.
    op        – FLASHREAD (2) or FLASHWRITE (3).
Returns the device status: FLASH_STATUS_OK (1) = success.
*/
static int flashIO(int asid, int pageNo, memaddr frameAddr, int op)
{
    int devNo = asid - 1;
    //Computes the flash device register address
    memaddr devRegAddr = START_DEVREG
                       + ((FLASH_LINE - 3) * 0x80)
                       + (devNo * 0x10);
    dtpreg_t *flashReg = (dtpreg_t *)devRegAddr;
 
    //Set DATA0 to the frame's physical base address.
    flashReg->data0 = (unsigned int)frameAddr;
 
    //Acquire per-device mutex then issue DOIO (NSYS5).
    //DOIO writes COMMAND and blocks until the interrupt fires.
    SYSCALL(PASSEREN, (unsigned int)&flashSem[devNo], 0, 0);
 
    unsigned int cmd = ((unsigned int)pageNo << 8) | (unsigned int)op;
    int status = SYSCALL(DOIO, (unsigned int)&flashReg->command, (int)cmd, 0);
    
    //Release mutex
    SYSCALL(VERHOGEN, (unsigned int)&flashSem[devNo], 0, 0);
 
    return status;
}
