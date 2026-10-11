/* Private namespaces: boundary descriptor layout, CreatePrivateNamespace / OpenPrivateNamespace /
 * ClosePrivateNamespace, and how "<alias>\<name>" object names resolve, in one process and
 * across processes. Every API is looked up with GetProcAddress, so the probe also loads where
 * some of them are missing. Run without arguments; it starts itself again as the child. */
#include <windows.h>
#include <winternl.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

typedef HANDLE (WINAPI *pCreateBoundaryDescriptorW_t)(const WCHAR *, ULONG);
typedef HANDLE (WINAPI *pCreateBoundaryDescriptorA_t)(const char *, ULONG);
typedef BOOL (WINAPI *pAddSIDToBoundaryDescriptor_t)(HANDLE *, PSID);
typedef BOOL (WINAPI *pAddIntegrityLabelToBoundaryDescriptor_t)(HANDLE *, PSID);
typedef void (WINAPI *pDeleteBoundaryDescriptor_t)(HANDLE);
typedef HANDLE (WINAPI *pCreatePrivateNamespaceW_t)(SECURITY_ATTRIBUTES *, void *, const WCHAR *);
typedef HANDLE (WINAPI *pCreatePrivateNamespaceA_t)(SECURITY_ATTRIBUTES *, void *, const char *);
typedef HANDLE (WINAPI *pOpenPrivateNamespaceW_t)(void *, const WCHAR *);
typedef HANDLE (WINAPI *pOpenPrivateNamespaceA_t)(void *, const char *);
typedef BOOLEAN (WINAPI *pClosePrivateNamespace_t)(HANDLE, ULONG);
typedef NTSTATUS (WINAPI *pNtQueryObject_t)(HANDLE, OBJECT_INFORMATION_CLASS, void *, ULONG, ULONG *);

static pCreateBoundaryDescriptorW_t pCreateBoundaryDescriptorW;
static pCreateBoundaryDescriptorA_t pCreateBoundaryDescriptorA;
static pAddSIDToBoundaryDescriptor_t pAddSIDToBoundaryDescriptor;
static pAddIntegrityLabelToBoundaryDescriptor_t pAddIntegrityLabelToBoundaryDescriptor;
static pDeleteBoundaryDescriptor_t pDeleteBoundaryDescriptor;
static pCreatePrivateNamespaceW_t pCreatePrivateNamespaceW;
static pCreatePrivateNamespaceA_t pCreatePrivateNamespaceA;
static pOpenPrivateNamespaceW_t pOpenPrivateNamespaceW;
static pOpenPrivateNamespaceA_t pOpenPrivateNamespaceA;
static pClosePrivateNamespace_t pClosePrivateNamespace;
static pNtQueryObject_t pNtQueryObject;

static BYTE sid_ba[SECURITY_MAX_SID_SIZE], sid_world[SECURITY_MAX_SID_SIZE], sid_il[SECURITY_MAX_SID_SIZE];
static const char *who = "parent";

static void print_w(const WCHAR *s, int n)
{
    char buf[1024];
    int len = WideCharToMultiByte(CP_UTF8, 0, s, n, buf, sizeof(buf) - 1, NULL, NULL);
    buf[len > 0 ? len : 0] = 0;
    printf("%s", buf);
}

static void show_object(HANDLE h)
{
    union { OBJECT_NAME_INFORMATION name; BYTE buf[1024]; } u;
    ULONG len;
    NTSTATUS status;

    status = pNtQueryObject(h, 2 /* ObjectTypeInformation */, u.buf, sizeof(u.buf), &len);
    if (!status) { printf(" type="); print_w(u.name.Name.Buffer, u.name.Name.Length / 2); }
    else printf(" type=<%#lx>", status);
    status = pNtQueryObject(h, 1 /* ObjectNameInformation */, u.buf, sizeof(u.buf), &len);
    if (!status) { printf(" name=\""); print_w(u.name.Name.Buffer, u.name.Name.Length / 2); printf("\""); }
    else printf(" name=<%#lx>", status);
}

/* print "label: ok err=N type=... name=..." or "label: NULL err=N" for a handle result */
static void report(const char *label, HANDLE h, DWORD err)
{
    printf("[%s] %-46s %s err=%lu", who, label, h ? "ok  " : "NULL", err);
    if (h && h != INVALID_HANDLE_VALUE) show_object(h);
    printf("\n");
}

#define CALL(label, expr) do { HANDLE h_; DWORD e_; SetLastError(0xdeadbeef); h_ = (expr); e_ = GetLastError(); \
                               report(label, h_, e_); } while (0)

static void dump_bd(const char *label, HANDLE bd)
{
    const ULONG *p = bd;
    ULONG i, size;

    if (!bd) { printf("[%s] bd %-43s NULL\n", who, label); return; }
    size = p[2];
    printf("[%s] bd %-43s version=%lu items=%lu total=%lu flags=%#lx:", who, label, p[0], p[1], size, p[3]);
    for (i = 4; i < size / 4 && i < 64; i++) printf(" %08lx", p[i]);
    printf("\n");
}

static HANDLE make_bd(const WCHAR *name, ULONG flags, int ba, int world, int il)
{
    HANDLE bd = pCreateBoundaryDescriptorW(name, flags);
    if (!bd) return NULL;
    if (ba) pAddSIDToBoundaryDescriptor(&bd, sid_ba);
    if (world) pAddSIDToBoundaryDescriptor(&bd, sid_world);
    if (il) pAddIntegrityLabelToBoundaryDescriptor(&bd, sid_il);
    return bd;
}

static HANDLE mutex_w(const WCHAR *name, BOOL create)
{
    return create ? CreateMutexW(NULL, FALSE, name) : OpenMutexW(SYNCHRONIZE, FALSE, name);
}

static void child(void)
{
    HANDLE bd = make_bd(L"WeProbe-1", 0, 1, 0, 0), ns, h;

    who = "child";
    CALL("Create(B1, \"C\") from child", pCreatePrivateNamespaceW(NULL, bd, L"C"));
    CALL("Open(B1, \"C\") from child", ns = pOpenPrivateNamespaceW(bd, L"C"));
    CALL("OpenMutex \"C\\m\"", h = mutex_w(L"C\\m", FALSE));
    CALL("CreateMutex \"C\\m\"", mutex_w(L"C\\m", TRUE));
    CALL("CreateMutex \"C\\child\"", mutex_w(L"C\\child", TRUE));
    CALL("OpenMutex \"P\\m\" (alias of the parent)", mutex_w(L"P\\m", FALSE));
    (void)ns; (void)h;
    Sleep(1500); /* parent looks for C\child while we hold it */
}

static void run_child(void)
{
    WCHAR cmd[MAX_PATH + 16];
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;

    cmd[0] = '"';
    GetModuleFileNameW(NULL, cmd + 1, MAX_PATH);
    wcscat(cmd, L"\" child");
    fflush(stdout);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        printf("CreateProcess failed %lu\n", GetLastError());
        return;
    }
    Sleep(700);
    CALL("OpenMutex \"P\\child\" while the child runs", mutex_w(L"P\\child", FALSE));
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

int main(int argc, char **argv)
{
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    HANDLE bd1, bd, ns1, ns, h, m1;
    DWORD size;
    SECURITY_ATTRIBUTES sa = {sizeof(sa)};
    TOKEN_ELEVATION elev;

    setvbuf(stdout, NULL, _IONBF, 0);
#define GET(f) p##f = (void *)GetProcAddress(k32, #f); if (!p##f) printf("missing %s\n", #f)
    GET(CreateBoundaryDescriptorW); GET(CreateBoundaryDescriptorA); GET(AddSIDToBoundaryDescriptor);
    GET(AddIntegrityLabelToBoundaryDescriptor); GET(DeleteBoundaryDescriptor); GET(CreatePrivateNamespaceW);
    GET(CreatePrivateNamespaceA); GET(OpenPrivateNamespaceW); GET(OpenPrivateNamespaceA); GET(ClosePrivateNamespace);
#undef GET
    pNtQueryObject = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
    if (!pCreateBoundaryDescriptorW || !pAddSIDToBoundaryDescriptor || !pCreatePrivateNamespaceW
            || !pOpenPrivateNamespaceW || !pClosePrivateNamespace)
        return 1;

    size = sizeof(sid_ba); CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, sid_ba, &size);
    size = sizeof(sid_world); CreateWellKnownSid(WinWorldSid, NULL, sid_world, &size);
    size = sizeof(sid_il); CreateWellKnownSid(WinMediumLabelSid, NULL, sid_il, &size);

    if (argc > 1 && !strcmp(argv[1], "child"))
    {
        child();
        return 0;
    }

    if (GetTokenInformation(GetCurrentProcessToken(), TokenElevation, &elev, sizeof(elev), &size))
        printf("elevated=%lu\n", elev.TokenIsElevated);

    /* boundary descriptor layout */
    bd = pCreateBoundaryDescriptorW(L"WeProbe-1", 0);
    dump_bd("name only", bd);
    h = bd;
    printf("[parent] AddSID(BA) ret=%d", pAddSIDToBoundaryDescriptor(&bd, sid_ba));
    printf(" pointer %s\n", bd == h ? "same" : "changed");
    dump_bd("+ BA", bd);
    pAddSIDToBoundaryDescriptor(&bd, sid_world);
    dump_bd("+ BA + World", bd);
    if (pAddIntegrityLabelToBoundaryDescriptor)
    {
        printf("[parent] AddIntegrityLabel(medium) ret=%d\n", pAddIntegrityLabelToBoundaryDescriptor(&bd, sid_il));
        dump_bd("+ BA + World + IL", bd);
    }
    pDeleteBoundaryDescriptor(bd);
    dump_bd("empty name", pCreateBoundaryDescriptorW(L"", 0));
    dump_bd("flags 1", pCreateBoundaryDescriptorW(L"X", 1));
    if (pCreateBoundaryDescriptorA) dump_bd("A version \"abc\"", pCreateBoundaryDescriptorA("abc", 0));

    /* basics */
    bd1 = make_bd(L"WeProbe-1", 0, 1, 0, 0);
    CALL("Create(B1, \"P\")", ns1 = pCreatePrivateNamespaceW(NULL, bd1, L"P"));
    CALL("Create(B1, \"P\") again", pCreatePrivateNamespaceW(NULL, bd1, L"P"));
    CALL("Create(B1, \"Q\")", pCreatePrivateNamespaceW(NULL, bd1, L"Q"));
    CALL("Open(B1, \"Q\")", pOpenPrivateNamespaceW(bd1, L"Q"));
    CALL("CreateMutex \"P\\m\"", m1 = mutex_w(L"P\\m", TRUE));
    CALL("CreateMutex \"Q\\m\"", mutex_w(L"Q\\m", TRUE));
    CALL("OpenMutex \"p\\m\" (alias in lower case)", mutex_w(L"p\\m", FALSE));
    CALL("OpenMutex \"P\\M\"", mutex_w(L"P\\M", FALSE));
    CALL("CreateMutex \"Z\\m\" (no such alias)", mutex_w(L"Z\\m", TRUE));
    CALL("CreateMutex \"P\\sub\\m\"", mutex_w(L"P\\sub\\m", TRUE));
    CALL("CreateMutex \"P\\\"", mutex_w(L"P\\", TRUE));
    CALL("CreateMutex \"Global\\P\\m\"", mutex_w(L"Global\\P\\m", TRUE));
    CALL("CreateMutex \"Local\\P\\m\"", mutex_w(L"Local\\P\\m", TRUE));
    CALL("CreateMutexA \"P\\ma\"", CreateMutexA(NULL, FALSE, "P\\ma"));
    CALL("OpenMutexA \"P\\m\"", OpenMutexA(SYNCHRONIZE, FALSE, "P\\m"));
    CALL("CreateEvent \"P\\e\"", CreateEventW(NULL, FALSE, FALSE, L"P\\e"));
    CALL("OpenEvent \"P\\e\"", OpenEventW(SYNCHRONIZE, FALSE, L"P\\e"));
    CALL("CreateSemaphore \"P\\s\"", CreateSemaphoreW(NULL, 0, 1, L"P\\s"));
    CALL("CreateWaitableTimer \"P\\t\"", CreateWaitableTimerW(NULL, FALSE, L"P\\t"));
    CALL("CreateFileMapping \"P\\f\"", CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, L"P\\f"));
    CALL("OpenFileMapping \"P\\f\"", OpenFileMappingW(FILE_MAP_READ, FALSE, L"P\\f"));
    CALL("CreateJobObject \"P\\j\"", CreateJobObjectW(NULL, L"P\\j"));
    CALL("CreateMutex \"P\" (alias alone)", mutex_w(L"P", TRUE));

    /* cross-process */
    run_child();

    /* which boundaries are the same namespace */
    CALL("Create(B1 lower-case name, \"L\")", pCreatePrivateNamespaceW(NULL, make_bd(L"weprobe-1", 0, 1, 0, 0), L"L"));
    CALL("Create(name, World, \"R\")", pCreatePrivateNamespaceW(NULL, make_bd(L"WeProbe-1", 0, 0, 1, 0), L"R"));
    CALL("CreateMutex \"R\\m\"", mutex_w(L"R\\m", TRUE));
    CALL("Create(name, BA+World, \"S\")", pCreatePrivateNamespaceW(NULL, make_bd(L"WeProbe-1", 0, 1, 1, 0), L"S"));
    CALL("Create(name, no SID, \"T0\")", pCreatePrivateNamespaceW(NULL, make_bd(L"WeProbe-1", 0, 0, 0, 0), L"T0"));
    bd = pCreateBoundaryDescriptorW(L"WeProbe-1", 0);
    pAddSIDToBoundaryDescriptor(&bd, sid_world);
    pAddSIDToBoundaryDescriptor(&bd, sid_ba);
    CALL("Create(name, World+BA, \"T\")", pCreatePrivateNamespaceW(NULL, bd, L"T"));
    CALL("Create(name, BA, flags 1, \"U\")", pCreatePrivateNamespaceW(NULL, make_bd(L"WeProbe-1", 1, 1, 0, 0), L"U"));
    if (pAddIntegrityLabelToBoundaryDescriptor)
        CALL("Create(name, BA+IL, \"V\")", pCreatePrivateNamespaceW(NULL, make_bd(L"WeProbe-1", 0, 1, 0, 1), L"V"));
    CALL("Create(other name, BA, \"W\")", pCreatePrivateNamespaceW(NULL, make_bd(L"WeProbe-2", 0, 1, 0, 0), L"W"));
    CALL("Open(no such boundary, \"X\")", pOpenPrivateNamespaceW(make_bd(L"WeProbe-3", 0, 1, 0, 0), L"X"));

    /* the launcher's way: SD that only allows Administrators */
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;BA)", SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL);
    CALL("Create(B4 with SD, \"SD\")", pCreatePrivateNamespaceW(&sa, make_bd(L"WeProbe-4", 0, 1, 0, 0), L"SD"));
    CALL("CreateMutex \"SD\\m\"", mutex_w(L"SD\\m", TRUE));

    /* odd aliases */
    CALL("Create(B5, NULL alias)", pCreatePrivateNamespaceW(NULL, make_bd(L"WeProbe-5", 0, 1, 0, 0), NULL));
    CALL("Create(B6, \"\" alias)", pCreatePrivateNamespaceW(NULL, make_bd(L"WeProbe-6", 0, 1, 0, 0), L""));
    CALL("Create(B7, \"a\\b\" alias)", pCreatePrivateNamespaceW(NULL, make_bd(L"WeProbe-7", 0, 1, 0, 0), L"a\\b"));
    CALL("CreateMutex \"a\\b\\m\"", mutex_w(L"a\\b\\m", TRUE));
    CALL("Create(B8, no SID, \"N\")", pCreatePrivateNamespaceW(NULL, make_bd(L"WeProbe-8", 0, 0, 0, 0), L"N"));
    CALL("CreateMutex \"N\\m\"", mutex_w(L"N\\m", TRUE));
    if (pCreatePrivateNamespaceA)
        CALL("CreatePrivateNamespaceA(B9, \"PA\")", pCreatePrivateNamespaceA(NULL, make_bd(L"WeProbe-9", 0, 1, 0, 0), "PA"));
    CALL("CreateMutex \"PA\\m\"", mutex_w(L"PA\\m", TRUE));

    /* the same alias for a second namespace */
    CALL("Create(other name, BA, \"P\")", ns = pCreatePrivateNamespaceW(NULL, make_bd(L"WeProbe-10", 0, 1, 0, 0), L"P"));
    CALL("OpenMutex \"P\\m\" (two namespaces named P)", mutex_w(L"P\\m", FALSE));
    CALL("CreateMutex \"P\\new\"", h = mutex_w(L"P\\new", TRUE));
    printf("[parent] Close(second P, 0) ret=%d\n", pClosePrivateNamespace(ns, 0));
    CALL("OpenMutex \"P\\m\" after closing the second P", mutex_w(L"P\\m", FALSE));

    /* closing */
    ns = pOpenPrivateNamespaceW(bd1, L"K");
    printf("[parent] Close(K, 0) ret=%d\n", pClosePrivateNamespace(ns, 0));
    CALL("CreateMutex \"K\\m\" after closing K", mutex_w(L"K\\m", TRUE));
    printf("[parent] Close(P, 0) ret=%d\n", pClosePrivateNamespace(ns1, 0));
    CALL("OpenMutex \"Q\\m\" (Q still open)", mutex_w(L"Q\\m", FALSE));
    CALL("Create(B1, \"P2\") after closing P", pCreatePrivateNamespaceW(NULL, bd1, L"P2"));
    CALL("Open(B1, \"P3\")", ns = pOpenPrivateNamespaceW(bd1, L"P3"));
    printf("[parent] Close(P3, DESTROY) ret=%d err=%lu\n", pClosePrivateNamespace(ns, PRIVATE_NAMESPACE_FLAG_DESTROY), GetLastError());
    CALL("Open(B1, \"P4\") after DESTROY", pOpenPrivateNamespaceW(bd1, L"P4"));
    CALL("OpenMutex \"Q\\m\" after DESTROY", mutex_w(L"Q\\m", FALSE));
    CALL("Create(B1, \"P5\") after DESTROY", pCreatePrivateNamespaceW(NULL, bd1, L"P5"));
    CALL("OpenMutex \"P5\\m\" (m1 still open)", mutex_w(L"P5\\m", FALSE));
    CloseHandle(m1);
    (void)h;
    return 0;
}
