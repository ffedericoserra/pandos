/* Phase 3 - InstantiatorProcess.
 *
 * Replaces the phase 2 `test` placeholder. In later milestones this file
 * initialises Support structures, semaphores, the swap pool, and launches
 * the shell U-proc; the M1 incarnation just terminates cleanly so the
 * kernel halts after we swap out p2test.c.
 */

#include <uriscv/liburiscv.h>

#include "../headers/const.h"

void test(void) {
    SYSCALL(TERMPROCESS, 0, 0, 0);
}
