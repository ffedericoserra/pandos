#include <uriscv/liburiscv.h>

#include "h/tconst.h"
#include "h/print.h"

/* One-shot single-digit calculator: <digit><op><digit>. */

#define BUFLEN 16

static void printSigned(int n) {
    /* No libc; print at most a couple digits with optional minus. */
    char out[8];
    int i = 0;
    int neg = 0;
    if (n < 0) { neg = 1; n = -n; }

    char digits[6];
    int  d = 0;
    if (n == 0) {
        digits[d++] = '0';
    } else {
        while (n > 0) { digits[d++] = '0' + (n % 10); n /= 10; }
    }

    if (neg) out[i++] = '-';
    while (d > 0) out[i++] = digits[--d];
    out[i++] = '\n';
    out[i] = '\0';

    print(WRITETERMINAL, out);
}

void main() {
    char buf[BUFLEN];

    print(WRITETERMINAL, "calc> ");
    int n = SYSCALL(READTERMINAL, (int)&buf[0], 0, 0);

    if (n < 3) {
        print(WRITETERMINAL, "format: <digit><op><digit>\n");
        SYSCALL(TERMINATE, 0, 0, 0);
    }

    int  a  = buf[0] - '0';
    char op = buf[1];
    int  b  = buf[2] - '0';

    if (a < 0 || a > 9 || b < 0 || b > 9) {
        print(WRITETERMINAL, "operands must be single digits\n");
        SYSCALL(TERMINATE, 0, 0, 0);
    }

    int result = 0;
    switch (op) {
    case '+': result = a + b; break;
    case '-': result = a - b; break;
    case '*': result = a * b; break;
    case '/':
        if (b == 0) {
            print(WRITETERMINAL, "division by zero\n");
            SYSCALL(TERMINATE, 0, 0, 0);
        }
        result = a / b;
        break;
    default:
        print(WRITETERMINAL, "operator must be + - * /\n");
        SYSCALL(TERMINATE, 0, 0, 0);
    }

    printSigned(result);
    SYSCALL(TERMINATE, 0, 0, 0);
}
