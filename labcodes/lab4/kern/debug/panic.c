#include <assert.h>
#include <stdio.h>
#include <intr.h>
#include <sbi.h>
static bool panicking;
void __panic(const char *file, int line, const char *fmt, ...) {
    intr_disable(); panicking = 1;
    va_list ap; va_start(ap, fmt);
    cprintf("PANIC %s:%d: ", file, line); vcprintf(fmt, ap); cprintf("\n");
    va_end(ap); sbi_shutdown(); for (;;) asm volatile("wfi");
}
void __warn(const char *file, int line, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); cprintf("WARN %s:%d: ", file, line);
    vcprintf(fmt, ap); cprintf("\n"); va_end(ap);
}
bool is_kernel_panic(void) { return panicking; }
