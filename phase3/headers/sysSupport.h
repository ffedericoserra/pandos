#ifndef PANDOS_PHASE3_SYSSUPPORT_H
#define PANDOS_PHASE3_SYSSUPPORT_H

#include "../../headers/types.h"

/* Entry point referenced by sup_exceptContext[GENERALEXCEPT].pc */
void supportGeneralHandler(void);

/* Orderly U-proc termination. Releases support-level sync semaphores
 * (masterSem if shell, shellSem if child) before SYS2. The Pager
 * releases swapMutex itself before delegating here on flash error. */
void programTrap(support_t *sup);

#endif
