/* SYSCALL exception handling */

/* 
    Some system calls block the Current Process - the PCB is
    placed on the ASL and the Scheduler is called to dispatch the next job. If the system call is
    non-blocking, control is returned to the Current Process.

    Process termination is signalled via a system call => then call scheduler

    EXCEPTION CODES 8-11  

    Per ciascuna syscall ho una funzione? E l'indirizzamento ad esse è gestito dall'entrypoint
    `exceptionHandler()` in `exceptions.c`?

    */


/* if process who called called syscall (p_s in kernel mode && a0 register < 0) {
    
    CreateProcess (NSYS1)
    
    TerminateProcess (NSYS2)

    Passeren (P) (NSYS3)

    Verhogen (V) (NSYS4)

    DoIO (NSYS5)

    GetCPUTime (NSYS6)

    WaitForClock (NSYS7)

    GetSupportData (NSYS8)

    GetProcessID (NSYS9

    Yield (NSYS10)

    NSYS1-NSY10 in User-Mode
} */