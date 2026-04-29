/* Phase 3 - Virtual memory support.
 *
 * Owns:
 *   - uTLB_RefillHandler  : called from the BIOS pass-up vector on TLB miss
 *                           when the requested PTE has V=1. Just copies the
 *                           PTE into the TLB and resumes.
 *   - pager (TBD in M2)   : Support-level handler for TLB-Invalid faults.
 */

#include <uriscv/liburiscv.h>

#include "../headers/types.h"
#include "../headers/const.h"

extern pcb_t *currentProcess;

/* (VPN - 0x80000) mod 32. Maps text/data VPNs 0x80000..0x8001E to slots
 * 0..30 and the stack VPN 0xBFFFF to slot 31 with no special case. */
static inline int pageIndex(unsigned int vpn) {
    return (vpn - 0x80000) & (MAXPAGES - 1);
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
