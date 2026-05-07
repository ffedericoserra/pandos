#ifndef PANDOS_PHASE3_SYSSUPPORT_H
#define PANDOS_PHASE3_SYSSUPPORT_H

#include "../../headers/types.h"

/* Support-level general exception handler */
void supportGeneralHandler();
void programTrap(support_t *sup);

#endif