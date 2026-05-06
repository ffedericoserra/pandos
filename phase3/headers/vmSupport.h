#ifndef VMSUPPORT_H
#define VMSUPPORT_H

#include "../../headers/types.h"
#include "../../headers/const.h"

/* -----------------------------------------------------------------------
 * Swap Pool starting physical address.
 * Placed after the first OSFRAMES (32) frames of RAM, i.e. after the OS.
 * RAMSTART = 0x20000000, OSFRAMES = 32, PAGESIZE = 4096
 * ----------------------------------------------------------------------- */
#define SWAPPOOLSTART (RAMSTART + (OSFRAMES * PAGESIZE))  /* 0x20020000 */

/* Physical address of Swap Pool frame i */
#define FRAME_ADDR(i) ((memaddr)(SWAPPOOLSTART + ((i) * PAGESIZE)))

/* Flash interrupt line number (converted from IL_FLASH=18 by subtracting 14) */
#define FLASH_LINE 4   /* intLineNo = IL_FLASH - 14 = 4 */

/* Semaphore index offset for flash devices in deviceSemaphores[] */
#define FLASH_SEM_BASE ((FLASH_LINE - 3) * DEVPERINT)  /* = 8 */

/* Flash device status code for a successful read/write */
#define FLASH_STATUS_OK 1

/* -----------------------------------------------------------------------
 * Public functions
 * ----------------------------------------------------------------------- */

/* Initialize Swap Pool table, Swap Pool semaphore, and device semaphores.
 * Called by test/InstantiatorProcess in initProc.c. */
void initSwapStructs(void);

/* TLB-Refill event handler (Phase 3 replacement for the Phase 2 stub).
 * Declared here so exceptions.c can still export it via exceptions.h. */
void uTLB_RefillHandler(void);

/* TLB exception handler – the Pager. Handles page-fault (TLB-Invalid)
 * exceptions passed up from the Nucleus. */
void pager(void);

/* -----------------------------------------------------------------------
 * Exported semaphores (shared with initProc.c / sysSupport.c)
 * ----------------------------------------------------------------------- */
extern int swapPoolSem;                /* Mutual exclusion on Swap Pool table */
extern int flashDevSem[DEVPERINT];    /* Per-flash-device mutual exclusion (8) */
extern int termReadSem;               /* Terminal-0 read mutual exclusion      */
extern int termWriteSem;              /* Terminal-0 write mutual exclusion     */

#endif /* VMSUPPORT_H */