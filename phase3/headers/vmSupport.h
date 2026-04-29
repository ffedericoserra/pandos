#ifndef PANDOS_PHASE3_VMSUPPORT_H
#define PANDOS_PHASE3_VMSUPPORT_H

#include "../../headers/types.h"

/* Swap-Pool mutex semaphore (init to 1). */
extern int swapMutex;

void initSwapStructs(void);
void pager(void);
void uTLB_RefillHandler(void);

/* Move one frame between RAM and an ASID's flash backing store.
 * isWrite==0: read flash block -> frameAddr; isWrite!=0: write frame -> flash.
 * Returns the device status reported by SYS5 (DEV_READY on success). */
int flashOp(int asid, int blockNo, memaddr frameAddr, int isWrite);

#endif
