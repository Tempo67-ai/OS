#include <defs.h>
#include <sbi.h>
static unsigned long sbi_call(unsigned long id, unsigned long x, unsigned long y, unsigned long z) {
    register unsigned long a0 asm("a0") = x;
    register unsigned long a1 asm("a1") = y;
    register unsigned long a2 asm("a2") = z;
    register unsigned long a7 asm("a7") = id;
    asm volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a2), "r"(a7) : "memory");
    return a0;
}
void sbi_console_putchar(unsigned char ch) { sbi_call(1, ch, 0, 0); }
int sbi_console_getchar(void) { return (int)sbi_call(2, 0, 0, 0); }
void sbi_set_timer(unsigned long long when) { sbi_call(0, when, 0, 0); }
void sbi_shutdown(void) { sbi_call(8, 0, 0, 0); }
