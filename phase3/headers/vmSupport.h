#ifndef VMSUPPORT_H
#define VMSUPPORT_H

#include "../../headers/types.h"
#include "../../headers/const.h"

/* SWAPPOOLSTART is defined in headers/const.h (included above). */

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

/* Initialize Swap Pool table and Swap Pool semaphore.
 * Called by test/InstantiatorProcess in initProc.c. */
void initSwapPool(void);

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
extern int flashSem[UPROCMAX];    /* Per-flash-device mutual exclusion (8) */
extern int termRdSem;               /* Terminal-0 read mutual exclusion      */
extern int termWrSem;              /* Terminal-0 write mutual exclusion     */

#endif /* VMSUPPORT_H */