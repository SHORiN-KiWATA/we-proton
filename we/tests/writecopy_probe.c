/* Chromium's renderer CHECKs that VirtualProtect(PAGE_READONLY) on a global
 * in libcef.dll's data section reports the old protection PAGE_READWRITE.
 * What do Windows and Wine report for image data pages, written or not? */
#include <windows.h>
#include <stdio.h>

__attribute__((aligned(4096))) static char written[4096] = { 1 };
__attribute__((aligned(4096))) static char untouched[4096] = { 1 };
__attribute__((aligned(4096))) static char bss_page[4096];

static void probe(const char *name, void *p, int write_first)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (write_first) ((volatile char *)p)[0] = 2;
    VirtualQuery(p, &mbi, sizeof(mbi));
    DWORD before = mbi.Protect, old = 0;
    BOOL ok = VirtualProtect(p, 4096, PAGE_READONLY, &old);
    DWORD old2 = 0;
    VirtualProtect(p, 4096, PAGE_READWRITE, &old2);
    VirtualQuery(p, &mbi, sizeof(mbi));
    printf("%-22s query %#04lx type %#lx | VirtualProtect(RO) ok %d old %#04lx | back to RW old %#04lx, now %#04lx\n",
           name, before, mbi.Type, ok, old, old2, mbi.Protect);
}

int main(void)
{
    printf("writecopy_probe, wine %s\n", GetProcAddress(GetModuleHandleA("ntdll"), "wine_get_version") ? "yes" : "no");
    probe(".data written", written, 1);
    probe(".data not written", untouched, 0);
    probe(".bss written", bss_page, 1);
    return 0;
}
