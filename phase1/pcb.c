#include "./headers/pcb.h"

static struct list_head pcbFree_h;
static pcb_t pcbFree_table[MAXPROC];
static int next_pid = 1;

/*initialize the pcbFree list to contain all the elements of the
static array of MAXPROC PCBs. This method will be called only once during data
structure initialization.*/
void initPcbs() {

    //Inizializzo la lista pcbFree_h vuota.
    INIT_LIST_HEAD(&pcbFree_h);

    //Inserisco in coda alla lista gli elementi di pcbfree_table[].
    for(int i=0; i<MAXPROC; i++){
        list_add_tail(&pcbFree_table[i].p_list, &pcbFree_h);
    }
}

//insert the element pointed to by p onto the pcbFree list
void freePcb(pcb_t* p) {

    //Inserisco l'elemento in coda alla lista.
    list_add_tail(&p->p_list, &pcbFree_h);
}

/*return NULL if the pcbFree list is empty. Otherwise,
remove an element from the pcbFree list, provide initial values for ALL of the
PCBs fields and then return a pointer to the removed element. PCBs get reused,
so it is important that no previous value persist in a PCB when it gets reallocated. */
pcb_t* allocPcb() {

    //Controlla se la lista è vuota, se sì, ritorna NULL.
    if(list_empty(&pcbFree_h)){
        return NULL;
    }

    //Con un puntatore al primo nodo della lista, elimino il nodo usando list_del().
    struct list_head *headNext = pcbFree_h.next;
    list_del(headNext);

    //Container_of ottiene il puntatore al PCB che contiene il campo p_list.
    pcb_t *p = container_of(headNext, pcb_t, p_list);

    //Inizializzo i valori dei PCB, per farlo: 
    //Azzero il padre e il tempo, 
    //inizializzo la lista dei figli e dei fratelli,
    //tolgo il riferimento al semaforo e al supporto e azzero la priorità,
    //assegno un nuovo PID incrementando il contatore globale next_pid.
    //Azzero il nodo list_head.
    p->p_parent=NULL;
    INIT_LIST_HEAD(&p->p_child);
    INIT_LIST_HEAD(&p->p_sib);
    p->p_time=0;
    p->p_semAdd=NULL;
    p->p_supportStruct=NULL;
    p->p_prio=0;
    p->p_pid=next_pid++;
    INIT_LIST_HEAD(&p->p_list);

    return p;
}

void mkEmptyProcQ(struct list_head *head) {
    INIT_LIST_HEAD(head); // function of listx.h to initialize an empty list
}

int emptyProcQ(struct list_head *head) {
    return list_empty(head); // returns 1 if the list is empty and 0 otherwise
}

void insertProcQ(struct list_head *head, pcb_t *p) {
    struct list_head *pos = head->next;

    /* navigating the list until I find an element with less priority */
    while (pos != head) {
        pcb_t *curr = container_of(pos, pcb_t, p_list);

        /* insert p before the first element with less priority */
        if (p->p_prio > curr->p_prio) {
            __list_add(&p->p_list, pos->prev, pos);
            return;
        }

        pos = pos->next; // update pos with the next element of the list
    }

    /* if no element with less priority than p->prio was found, I add the element to the tail */
    list_add_tail(&p->p_list, head);
}

pcb_t *headProcQ(struct list_head *head) {
    if (emptyProcQ(head)) 
        return NULL; // returns NULL if the list is emptyProcQ

    return container_of(head->next, pcb_t, p_list); // returns the pcb_t of the first element of the queue
}

pcb_t *removeProcQ(struct list_head *head) {
    pcb_t *element = headProcQ(head); // element becomes a pointer to the first element of the list
                                      // if the list is empty, element will be NULL
    if (element != NULL) {
        list_del(head->next); // deletes the first element of the list
    }

    return element;
}

pcb_t *outProcQ(struct list_head *head, pcb_t *p) {
    struct list_head *pos = head->next;

    /* navigating the list until I find the pcb p */
    while (pos != head) {
        pcb_t *curr = container_of(pos, pcb_t, p_list);

        /* if the element p is found, I delete it from the list and return it */
        if (curr == p) {
            list_del(pos);
            return p;
        }

        pos = pos->next;
    }

    return NULL; // returns NULL if the element was not in the given list
}

/*
    Return TRUE if the PCB pointed to by p has no children. Return FALSE otherwise.
*/
int emptyChild(pcb_t* p) {
    return list_empty(&p->p_child); // returns 1 if the list is empty and 0 otherwise
}
/*
    Make the PCB pointed to by p a child of the PCB pointed to by prnt.
*/
void insertChild(pcb_t* prnt, pcb_t* p) {
    if(p != NULL && prnt != NULL){
        p->p_parent = prnt;        //sets the new parent
        list_add(&p->p_sib, &prnt->p_child);  //adds the new element &p->p_sib to the head of the list &prnt->p_child
    }
}
/*
    Make the first child of the PCB pointed to by p no longer a child of p. Return NULL if initially
    there were no children of p. Otherwise, return a pointer to this removed first child PCB.
*/
pcb_t* removeChild(pcb_t* p) {
    if(list_empty(&p->p_child)){
        return NULL;
    }
    pcb_t* tmp = container_of(p->p_child.next, pcb_t, p_sib);    //container_of returns the ptr to the struct (pcb_t) containing p->p_child.next
    list_del(p->p_child.next);         //deletes the references to &p->p_child.next inside the list of p_sib linking the prev and the next siblings together
    tmp->p_parent = NULL;
    return tmp;
}
/*
    Make the PCB pointed to by p no longer the child of its parent. If the PCB pointed to by p has
    no parent, return NULL; otherwise, return p. Note that the element pointed to by p could be
    in an arbitrary position (i.e. not be the first child of its parent).
*/
pcb_t* outChild(pcb_t* p) {
    if(p->p_parent == NULL){
        return NULL;
    }

    list_del(&p->p_sib);    //deletes the references to &p->p_sib inside the list of p_sib linking the prev and the next siblings together

    p->p_parent = NULL;

    return p;
}
