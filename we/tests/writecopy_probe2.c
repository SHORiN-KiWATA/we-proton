/* More write-copy cases: patching a code page, a code page made writable but
 * never written, and region boundaries between written and unwritten pages of
 * an image data section. Run on Windows and on Wine and compare. */
#include <windows.h>
#include <stdio.h>

__attribute__((aligned(4096), noinline)) static int patch_me(int x) { return x + 1; }
__attribute__((aligned(4096), noinline)) static int leave_me(int x) { return x + 2; }

/* its own section, so nothing else in the program writes these pages */
__attribute__((section(".probe"), aligned(4096))) static char pages[3][4096] = { { 1 }, { 1 }, { 1 } };

static void query(const char *what, void *p)
{
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(p, &mbi, sizeof(mbi));
    printf("  %-34s protect %#04lx size %#lx\n", what, mbi.Protect, (unsigned long)mbi.RegionSize);
}

static void protect(const char *what, void *p, SIZE_T size, DWORD prot)
{
    DWORD old = 0;
    BOOL ok = VirtualProtect(p, size, prot, &old);
    printf("  %-34s ok %d old %#04lx\n", what, ok, old);
}

int main(void)
{
    void *code = (void *)((ULONG_PTR)patch_me & ~(ULONG_PTR)0xfff);
    void *code2 = (void *)((ULONG_PTR)leave_me & ~(ULONG_PTR)0xfff);
    volatile char *byte = (volatile char *)patch_me + 64;

    printf("writecopy_probe2, wine %s\n", GetProcAddress(GetModuleHandleA("ntdll"), "wine_get_version") ? "yes" : "no");

    printf("code page, patched:\n");
    query("initial", code);
    protect("-> EXECUTE_READWRITE", code, 1, PAGE_EXECUTE_READWRITE);
    query("before write", code);
    *byte = *byte;
    query("after write", code);
    protect("-> EXECUTE_READ", code, 1, PAGE_EXECUTE_READ);
    query("restored", code);
    protect("-> EXECUTE_READWRITE again", code, 1, PAGE_EXECUTE_READWRITE);
    query("reopened", code);
    protect("-> EXECUTE_READ", code, 1, PAGE_EXECUTE_READ);
    printf("  patch_me(1) = %d\n", patch_me(1));

    printf("code page, opened but not written:\n");
    protect("-> EXECUTE_READWRITE", code2, 1, PAGE_EXECUTE_READWRITE);
    query("opened", code2);
    protect("-> EXECUTE_READ", code2, 1, PAGE_EXECUTE_READ);
    printf("  leave_me(1) = %d\n", leave_me(1));

    printf("data section, middle page written:\n");
    pages[1][0] = 2;
    query("page 0", pages[0]);
    query("page 1", pages[1]);
    query("page 2", pages[2]);
    protect("pages 0-2 -> READONLY", pages[0], sizeof(pages), PAGE_READONLY);
    protect("pages 0-2 -> READWRITE", pages[0], sizeof(pages), PAGE_READWRITE);
    query("page 0 after", pages[0]);
    query("page 1 after", pages[1]);
    protect("page 1 -> READONLY", pages[1], 1, PAGE_READONLY);
    protect("page 1 -> READWRITE", pages[1], 1, PAGE_READWRITE);
    return 0;
}
