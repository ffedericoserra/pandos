/* Phase 3 - Support-level general exception handler (M2 stub).
 *
 * In M2 every general exception (any SYS call, any program trap) is treated
 * as a fatal error for the offending U-proc: the per-shell-or-master sync
 * semaphore is V'd, then NSYS2 terminates the U-proc cleanly. M3 replaces
 * this stub with the real SYS2/SYS4/SYS5 dispatcher and a proper program-
 * trap handler.
 *
 * The Pager releases swapMutex itself before delegating here on flash error
 * (see vmSupport.c), so programTrap is free not to track that ownership.
 */

#include <uriscv/liburiscv.h>

#include "../headers/types.h"
#include "../headers/const.h"

#include "headers/sysSupport.h"
#include "headers/initProc.h"

void programTrap(support_t *sup) {
    if (sup->sup_asid == 1) {
        SYSCALL(VERHOGEN, (int)&masterSem, 0, 0);
    } else {
        SYSCALL(VERHOGEN, (int)&shellSem, 0, 0);
    }
    SYSCALL(TERMPROCESS, 0, 0, 0);
}

void supportGeneralHandler(void) {
    support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);
    programTrap(sup);
}
