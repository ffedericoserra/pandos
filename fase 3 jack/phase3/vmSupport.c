#include <uriscv/liburiscv.h>
#include <uriscv/cpu.h>

#include "../headers/types.h"
#include "../headers/const.h"
#include "../headers/utils.h"

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"

#include "./headers/exceptions.h"
#include "./headers/interrupts.h"
#include "./headers/scheduler.h"
#include "./headers/syscalls.h"

// Numero di frame disponibili nello swap pool
// (2 per ogni processo utente massimo)
#define POOLSIZE (2 * UPROCMAX) 


//Struttura swap pool:
//Ogni entry rappresenta un frame fisico nello swap pool
typedef struct {
    int asid;              //ASID del processo che possiede la pagina (-1 = libero)
    int vpn;               //Virtual Page Number della pagina caricata nel frame
    pteEntry_t *pte;       //Puntatore alla entry della page table associata
} swap_entry_t;


//Array globale che rappresenta tutti i frame disponibili
swap_entry_t swap_pool[POOLSIZE];


//Serve per proteggere lo swap pool (accesso concorrente).
int swap_pool_sem = 1;


//Tiene traccia del prossimo frame da rimpiazzare
static int fifo_index = 0;


// Viene chiamato quando la pagina è valida (V=1 nella page table), ma non è presente nel TLB.
// Quindi basta ricaricare il TLB
void uTLB_RefillHandler() {

    state_t *state = GET_EXCEPTION_STATE_PTR(0);

    unsigned int entryHi = state->entry_hi;

    int vpn = entryHi >> VPNSHIFT;

    //Recupero support struct del processo corrente
    support_t *sup = currentProcess->p_supportStruct;

    //Traduzione VPN -> indice page table

    int index;

    if (vpn >= 0x80000 && vpn <= 0x8001E) {
        //pagine normali
        index = vpn - 0x80000;
    }
    else if (vpn == 0xBFFFF) {
        //ultima pagina
        index = 31;
    }
    else {
        index = 0;
    }

    //Recupero entry dalla page table
    pteEntry_t entry = sup->sup_privatePgTbl[index];

    //Copia entryHI ed entryLO(VPN + ASID)
    setENTRYHI(entry.pte_entryHI);
    setENTRYLO(entry.pte_entryLO);

    //Scrittura nel TLB
    TLBWR();
    LDST(state);
}



//Inizializzazione page table
//Crea la page table privata di un processo
void initPageTable(support_t *sup, int asid) {

    for (int i = 0; i < USERPGTBLSIZE; i++) {

        unsigned int vpn;
        unsigned int entryHI;
        unsigned int entryLO;

        //calcolo vpn
        if (i < USERPGTBLSIZE - 1) {
            //Pagine normali
            vpn = 0x80000 + i;
        } else {
            //Ultima pagina = stack
            vpn = 0xBFFFF;
        }

        entryHI = (vpn << VPNSHIFT) | (asid << ASIDSHIFT);

        //PFN = 0 -> pagina non ancora caricata
        //DIRTY = 1 -> scrivibile
        //VALID = 0 -> non valida e causerà page fault
        entryLO = DIRTYON;

        //Scrittura nella page table
        sup->sup_privatePgTbl[i].pte_entryHI = entryHI;
        sup->sup_privatePgTbl[i].pte_entryLO = entryLO;
    }
}

//Inizializzazione swap pool
void initSwapPool() {

    for (int i = 0; i < POOLSIZE; i++) {

        // Frame libero
        swap_pool[i].asid = -1;

        // Valori di default (pulizia)
        swap_pool[i].vpn = -1;
        swap_pool[i].pte = NULL;
    }

    // Semaforo già inizializzato a 1
}


//Seleziona il prossimo frame da usare/sostituire
int getFrameFIFO() {

    //Prende il frame corrente
    int frame = fifo_index;

    //Aggiorna indice
    fifo_index = (fifo_index + 1) % POOLSIZE;

    return frame;
}


//Disabilita interrupt
//Necessario per operazioni atomiche su TLB / page table
void disableInterrupts() {
    setSTATUS(getSTATUS() & ~MSTATUS_MIE_MASK);
}

//Abilita interrupt
void enableInterrupts() {
    setSTATUS(getSTATUS() | MSTATUS_MIE_MASK);
}

//Cancella tutte le entry del TLB
void updateTLB() {
    TLBCLR();
}