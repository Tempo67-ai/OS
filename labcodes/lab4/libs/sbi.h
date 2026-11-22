#ifndef LAB4_SBI_H
#define LAB4_SBI_H
void sbi_console_putchar(unsigned char ch);
int sbi_console_getchar(void);
void sbi_set_timer(unsigned long long when);
void sbi_shutdown(void);
#endif
