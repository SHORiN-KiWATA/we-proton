/* Call InbvAcquireDisplayOwnership / InbvResetDisplay from ntoskrnl.exe.
 * A stub aborts the process, an implemented no-op lets it continue. */
#include <stdarg.h>
#include <stdio.h>
#include <windows.h>

typedef void (WINAPI *pfn_t)(void);

int main(void)
{
    HMODULE m = LoadLibraryW( L"ntoskrnl.exe" );
    pfn_t f;

    if (!m) { printf( "load ntoskrnl.exe failed err=%lu\n", GetLastError() ); return 1; }
    if (!(f = (pfn_t)GetProcAddress( m, "InbvAcquireDisplayOwnership" ))) { printf( "no export\n" ); return 1; }
    f();
    printf( "InbvAcquireDisplayOwnership: called ok\n" );
    if ((f = (pfn_t)GetProcAddress( m, "InbvResetDisplay" ))) { f(); printf( "InbvResetDisplay: called ok\n" ); }
    return 0;
}
