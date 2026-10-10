#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
extern char __tls_base[];
extern void _set_tls(void *);
extern void __libc_init_array(void);
extern int main(void);
static long sys3(long n, long a, long b, long c) { register long a0 asm("a0") = a, a1 asm("a1") = b, a2 asm("a2") = c, a7 asm("a7") = n; asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory"); return a0; }
void _exit(int c) { sys3(93, c, 0, 0); for (;;) {} }
static int out_putc(char c, FILE *f) { (void)f; sys3(64, 1, (long)&c, 1); return (unsigned char)c; }
static FILE out = FDEV_SETUP_STREAM(out_putc, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &out; FILE *const stderr = &out; FILE *const stdin = &out;
extern char __bss_start[]; extern char sim_stack[];
void start_c(void) { for (volatile char *b = __bss_start; b < sim_stack; ++b) *b = 0; _set_tls(__tls_base); __libc_init_array(); exit(main()); }
__attribute__((naked, section(".text.init.enter"))) void _start(void) {
  asm volatile(".option push\n.option norelax\nla gp, __global_pointer$\n.option pop\nla sp, sim_stack_top\ncall start_c");
}
#include <sys/time.h>
int gettimeofday(struct timeval *t, void *z) { (void)z; if (t) { t->tv_sec = 0; t->tv_usec = 0; } return 0; }
int open(const char *p, int f, ...) { (void)p; (void)f; return -1; }
int close(int fd) { (void)fd; return -1; }
long read(int fd, void *b, unsigned long n) { (void)fd; (void)b; (void)n; return 0; }
long write(int fd, const void *b, unsigned long n) { return sys3(64, fd, (long)b, (long)n); }
long lseek(int fd, long o, int w) { (void)fd; (void)o; (void)w; return -1; }
int unlink(const char *p) { (void)p; return -1; }

static char sim_heap[96 << 20] __attribute__((aligned(16)));
static unsigned long sim_heap_used;
void *sbrk(ptrdiff_t n) {
    if (n < 0 || sim_heap_used + (unsigned long)n > sizeof sim_heap) return (void *)-1;
    void *p = sim_heap + sim_heap_used; sim_heap_used += (unsigned long)n; return p;
}
extern char sim_stack[]; char sim_stack[1 << 20] __attribute__((aligned(16)));
asm(".globl sim_stack_top\n.set sim_stack_top, sim_stack + (1 << 20)");
