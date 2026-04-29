#include <uriscv/liburiscv.h>

#include "h/tconst.h"
#include "h/print.h"

/* Static name -> ASID map matching config_machine_phase3.json flash layout:
 * flash0 shell (us, ASID 1), flash1 fibEight (2), flash2 echo (3),
 * flash3 fibEleven (4), flash4 uname (5), flash5 date (6), flash6 sl (7),
 * flash7 calc (8). */
#define NCMDS 7

static const char *names[NCMDS] = {
    "fibEight", "echo", "fibEleven", "uname", "date", "sl", "calc"
};
static const int asids[NCMDS] = { 2, 3, 4, 5, 6, 7, 8 };

#define BUFLEN 32

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

void main() {
    char buf[BUFLEN];

    print(WRITETERMINAL, "PandOSsh shell\n");

    while (1) {
        print(WRITETERMINAL, "$ ");

        int n = SYSCALL(READTERMINAL, (int)&buf[0], 0, 0);
        if (n <= 0) continue;

        /* Strip trailing newline, then null-terminate. */
        if (buf[n - 1] == '\n') n--;
        if (n >= BUFLEN) n = BUFLEN - 1;
        buf[n] = '\0';

        if (n == 0) continue;
        if (streq(buf, "exit")) break;

        int asid = -1;
        for (int i = 0; i < NCMDS; i++) {
            if (streq(buf, names[i])) { asid = asids[i]; break; }
        }
        if (asid < 0) {
            print(WRITETERMINAL, "command not found\n");
            continue;
        }

        SYSCALL(EXECUTE, asid, 0, 0);
    }

    print(WRITETERMINAL, "bye\n");
    SYSCALL(TERMINATE, 0, 0, 0);
}
