#include "./headers/asl.h"
#include "./headers/pcb.h"

static semd_t semd_table[MAXPROC];          // Array for semaphores descriptors
static struct list_head semdFree_h;         // List of unused (free) semaphore descriptors
static struct list_head semd_h;             // Represents the ASL: sorted list of semaphores with one or more processes blocked on them

static semd_t* getSemd(int* key);


/* Initialize the semdFree list to contain all the elements of the array static semd_t semdTable[MAXPROC].
 * This method will be only called once during data structure initialization. */
void initASL() {
    INIT_LIST_HEAD(&semdFree_h);
    INIT_LIST_HEAD(&semd_h);

    /* Add all static sem descriptors to the free list */
    for (int i = 0; i < MAXPROC; i++) {
        list_add(&semd_table[i].s_link, &semdFree_h);
    }
}

/* Insert the PCB pointed to by p at the tail of the process queue associated with the semaphore
 * whose key is semAdd and set the semaphore address of p to semaphore with semAdd. If the
 * semaphore is currently not active (i.e. there is no descriptor for it in the ASL), allocate a new
 * descriptor from the semdFree list, insert it in the ASL (at the appropriate position), initialize
 * all of the fields (i.e. set s_key to semAdd, and s_procq to mkEmptyProcQ()), and proceed as
 * above. If a new semaphore descriptor needs to be allocated and the semdFree list is empty,
 * return TRUE. In all other cases return FALSE.
 */
int insertBlocked(int* semAdd, pcb_t* p) {
    semd_t *sem = getSemd(semAdd);

    /* If semaphore is not active, allocate a new one */
    if (sem == NULL) {
        if (list_empty(&semdFree_h))    return TRUE;
        
        /* Remove from free list */
        struct list_head *new_node = semdFree_h.next;
        list_del(new_node);
        sem = container_of(new_node, semd_t, s_link);
        
        /* Initialize new semaphore */
        sem->s_key = semAdd;
        mkEmptyProcQ(&sem->s_procq);
        
        /* Insert into ASL in sorted order (ascending by key) */
        struct list_head *pos;
        int inserted = 0;
        list_for_each(pos, &semd_h) {
            semd_t *entry = container_of(pos, semd_t, s_link);
            if (entry->s_key > semAdd) {
                /* Insert before the current larger element */
                __list_add(&sem->s_link, pos->prev, pos);
                inserted = 1;
                break;
            }
        }
        /* If not inserted yet (list empty or largest key), add to tail */
        if (!inserted) {
            list_add_tail(&sem->s_link, &semd_h);
        }
    }
    
    /* Add the PCB to the tail of the semaphore's process queue */
    list_add_tail(&p->p_list, &sem->s_procq);
    p->p_semAdd = semAdd;

    return FALSE;
}

/* Search the ASL for a descriptor of this semaphore. If none is found, return NULL; otherwise,
 * remove the first (i.e. head) PCB from the process queue of the found semaphore descriptor and 
 * return a pointer to it. If the process queue for this semaphore becomes empty (emptyProcQ(s_procq) 
 * is TRUE), remove the semaphore descriptor from the ASL and return it to the semdFree list.
 */
pcb_t* removeBlocked(int* semAdd) {
    semd_t *sem = getSemd(semAdd);

    /* Semaphore not found; also handles safety check to ensure s_procq is not empty */
    if (sem == NULL || emptyProcQ(&sem->s_procq)) {
        return NULL;
    }

    /* Remove the first PCB from the queue */
    pcb_t *p = removeProcQ(&sem->s_procq);
    
    p->p_semAdd = NULL;

    /* If the queue is now empty, return the semaphore to the free list */
    if (emptyProcQ(&sem->s_procq)) {
        list_del(&sem->s_link);
        list_add(&sem->s_link, &semdFree_h);
    }

    return p;
}

/* Remove the PCB pointed to by p from the process queue associated with p’s semaphore (p->p_semAdd)
 * on the ASL. If PCB pointed to by p does not appear in the process queue associated with p’s
 * semaphore, which is an error condition, return NULL; otherwise, return p.
 */
pcb_t* outBlocked(pcb_t* p) {
    /* Error condition: p is not blocked on any semaphore */
    if (p->p_semAdd == NULL) {
        return NULL;
    }

    semd_t *sem = getSemd(p->p_semAdd);

    /* Error condition: semaphore descriptor not found */
    if (sem == NULL) {
        return NULL;
    }

    /* Remove p from the semaphore's queue */
    /* Note: list_del unlinks the entry regardless of its position */
    list_del(&p->p_list);
    p->p_semAdd = NULL;

    /* If the queue is now empty, return the semaphore to the free list */
    if (emptyProcQ(&sem->s_procq)) {
        list_del(&sem->s_link);
        list_add(&sem->s_link, &semdFree_h);
    }

    return p;
}

/* Return a pointer to the PCB that is at the head of the process queue associated with the
 * semaphore semAdd. Return NULL if semAdd is not found on the ASL or if the process queue
 * associated with semAdd is empty.
 */
pcb_t* headBlocked(int* semAdd) {
    semd_t *sem = getSemd(semAdd);

    if (sem == NULL || emptyProcQ(&sem->s_procq)) {
        return NULL;
    }

    /* Return the first PCB without removing it */
    return headProcQ(&sem->s_procq);
}


/* ------------------------ HELPERS ------------------------ */

/* Search for a semaphore descriptor in the ASL by its key.
 * If found return a pointer to the semaphore, else return NULL.
 */
static inline semd_t* getSemd(int* key) {
    struct list_head *pos;
    list_for_each(pos, &semd_h) {
        semd_t *entry = container_of(pos, semd_t, s_link);
        if (entry->s_key == key) {
            return entry;
        }
        /* Since ASL is sorted, passing the key one time means it's not in the list */
        if (entry->s_key > key) {
            return NULL;
        }
    }

    return NULL;
}
