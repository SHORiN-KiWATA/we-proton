/* A raw syscall must not touch the stack below rsp (Windows sysret doesn't).
 * Code may keep data there across a syscall, e.g. a stub saving rbp at rsp-8. */
#include <stdio.h>

int main(void)
{
    unsigned long long status, rbp_after, m8, m16;
    __asm__ volatile(
        "movq %%rbp, %%r12\n\t"
        "movabsq $0x1111111111111111, %%rax\n\t"
        "movq %%rax, -8(%%rsp)\n\t"
        "movabsq $0x2222222222222222, %%rax\n\t"
        "movq %%rax, -16(%%rsp)\n\t"
        "movabsq $0x5a5a5a5a5a5a5a5a, %%rbp\n\t"
        "movl $0x46, %%eax\n\t"        /* NtYieldExecution */
        "xorl %%r10d, %%r10d\n\t"
        "syscall\n\t"
        "movq %%rbp, %%r13\n\t"
        "movq %%r12, %%rbp\n\t"
        "movq -8(%%rsp), %%r14\n\t"
        "movq -16(%%rsp), %%r15\n\t"
        "movq %%r13, %1\n\t"
        "movq %%r14, %2\n\t"
        "movq %%r15, %3\n\t"
        : "=a"(status), "=m"(rbp_after), "=m"(m8), "=m"(m16)
        :
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "memory");
    printf("status %#llx rbp_after %#llx [rsp-8] %#llx [rsp-16] %#llx\n", status, rbp_after, m8, m16);
    int ok = rbp_after == 0x5a5a5a5a5a5a5a5aULL && m8 == 0x1111111111111111ULL && m16 == 0x2222222222222222ULL;
    puts(ok ? "PASS: stack below rsp untouched" : "FAIL: syscall return clobbered the stack below rsp");
    return !ok;
}
