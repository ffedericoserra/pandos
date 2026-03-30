#ifndef UTILS_H
#define UTILS_H

#include <uriscv/types.h>

/* Word-by-word state copy to avoid compiler-generated memcpy (no libc) */
static inline void copyState(state_t *dest, const state_t *src) {
    dest->entry_hi = src->entry_hi;
    dest->cause = src->cause;
    dest->status = src->status;
    dest->pc_epc = src->pc_epc;
    dest->mie = src->mie;
    for (int i = 0; i < STATE_GPR_LEN; i++)
        dest->gpr[i] = src->gpr[i];
}

#endif
