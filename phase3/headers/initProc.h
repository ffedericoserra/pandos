#ifndef PANDOS_PHASE3_INITPROC_H
#define PANDOS_PHASE3_INITPROC_H

#include "../../headers/types.h"
#include "../../headers/const.h"

/* InstantiatorProcess entry point — replaces the phase 2 `test`. */
void test(void);

/* Per-U-proc Support structures and initial processor states.
 * supports[i-1] / uprocStates[i-1] correspond to ASID i, i in 1..UPROCMAX. */
extern support_t supports[UPROCMAX];
extern state_t   uprocStates[UPROCMAX];

/* Synchronisation semaphores */
extern int masterSem;            /* 0; V'd by shell on exit */
extern int shellSem;             /* 0; V'd by child U-proc on exit */

/* Mutex semaphores (init to 1) for support-level device access */
extern int termRdSem;
extern int termWrSem;
extern int flashSem[UPROCMAX];

#endif