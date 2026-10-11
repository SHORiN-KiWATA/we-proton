/* Print what programs see when they check for the Visual C++ 2015-2022 runtime:
 * the HKLM\Software\Microsoft\VisualStudio\14.0\VC\Runtimes\<arch> values in both
 * registry views and the file versions of the runtime DLLs in the system
 * directories. Then run the check a minimum-version bootstrapper does: the x64
 * key and the file versions of msvcp140_2.dll and vcruntime140_1.dll in system32
 * must all be at least the minimum (default 14.44.35211, or argv[1]), and both
 * DLLs must load.
 *
 *   vcrt_version_probe.exe [major.minor.build]
 */
#include <windows.h>
#include <stdio.h>

static DWORD min_ver[3] = { 14, 44, 35211 };

static int at_least( const DWORD *v )
{
    for (int i = 0; i < 3; i++) if (v[i] != min_ver[i]) return v[i] > min_ver[i];
    return 1;
}

static int print_key( const char *arch, REGSAM view, const char *view_name, DWORD *out )
{
    static const char *names[] = { "Major", "Minor", "Bld", "Rbld" };
    char path[128], version[64];
    DWORD v[4] = {0}, installed = 0, size;
    HKEY key;

    snprintf( path, sizeof(path), "Software\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\%s", arch );
    if (RegOpenKeyExA( HKEY_LOCAL_MACHINE, path, 0, KEY_READ | view, &key ))
    {
        printf( "reg %-5s %s: missing\n", arch, view_name );
        return 0;
    }
    for (int i = 0; i < 4; i++)
    {
        size = sizeof(v[i]);
        RegQueryValueExA( key, names[i], NULL, NULL, (BYTE *)&v[i], &size );
    }
    size = sizeof(installed);
    RegQueryValueExA( key, "Installed", NULL, NULL, (BYTE *)&installed, &size );
    size = sizeof(version);
    if (RegQueryValueExA( key, "Version", NULL, NULL, (BYTE *)version, &size )) strcpy( version, "(none)" );
    RegCloseKey( key );
    printf( "reg %-5s %s: Installed=%lu Major=%lu Minor=%lu Bld=%lu Rbld=%lu Version=%s\n",
            arch, view_name, installed, v[0], v[1], v[2], v[3], version );
    if (out) memcpy( out, v, sizeof(v) );
    return 1;
}

static int file_version( const char *path, DWORD *v )
{
    VS_FIXEDFILEINFO *info;
    DWORD size = GetFileVersionInfoSizeA( path, NULL );
    UINT len;
    void *data;
    int ret = 0;

    if (!size || !(data = malloc( size ))) return 0;
    if (GetFileVersionInfoA( path, 0, size, data ) && VerQueryValueA( data, "\\", (void **)&info, &len ))
    {
        v[0] = HIWORD(info->dwFileVersionMS);
        v[1] = LOWORD(info->dwFileVersionMS);
        v[2] = HIWORD(info->dwFileVersionLS);
        v[3] = LOWORD(info->dwFileVersionLS);
        ret = 1;
    }
    free( data );
    return ret;
}

static int print_file( const char *dir, const char *name, int try_load )
{
    char path[MAX_PATH];
    DWORD v[4];
    int ok = 0;

    snprintf( path, sizeof(path), "%s\\%s", dir, name );
    if (GetFileAttributesA( path ) == INVALID_FILE_ATTRIBUTES)
    {
        printf( "file %s: missing\n", path );
        return 0;
    }
    if (file_version( path, v ))
    {
        printf( "file %s: %lu.%lu.%lu.%lu", path, v[0], v[1], v[2], v[3] );
        ok = at_least( v );
    }
    else printf( "file %s: no version resource", path );
    if (try_load)
    {
        HMODULE mod = LoadLibraryA( path );
        printf( ", LoadLibrary %s", mod ? "ok" : "failed" );
        if (mod) FreeLibrary( mod );
        else ok = 0;
    }
    printf( "\n" );
    return ok;
}

int main( int argc, char **argv )
{
    char sys32[MAX_PATH], wow64[MAX_PATH];
    DWORD reg[4];
    int reg_ok, files_ok;

    if (argc > 1) sscanf( argv[1], "%lu.%lu.%lu", &min_ver[0], &min_ver[1], &min_ver[2] );
    printf( "minimum %lu.%lu.%lu\n", min_ver[0], min_ver[1], min_ver[2] );

    reg_ok = print_key( "x64", KEY_WOW64_64KEY, "64-bit view", reg ) && at_least( reg );
    print_key( "x64", KEY_WOW64_32KEY, "32-bit view", NULL );
    print_key( "x86", KEY_WOW64_64KEY, "64-bit view", NULL );
    print_key( "x86", KEY_WOW64_32KEY, "32-bit view", NULL );

    GetSystemDirectoryA( sys32, sizeof(sys32) );
    files_ok = print_file( sys32, "msvcp140_2.dll", 1 );
    files_ok &= print_file( sys32, "vcruntime140_1.dll", 1 );
    print_file( sys32, "msvcp140.dll", 0 );
    print_file( sys32, "vcruntime140.dll", 0 );
    if (GetSystemWow64DirectoryA( wow64, sizeof(wow64) )) print_file( wow64, "msvcp140.dll", 0 );

    printf( "check: registry %s, files %s -> %s\n", reg_ok ? "ok" : "too old",
            files_ok ? "ok" : "too old", reg_ok && files_ok ? "pass" : "runtime reported missing" );
    return !(reg_ok && files_ok);
}
