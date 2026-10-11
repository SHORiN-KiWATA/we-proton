/* How does the type library marshaler treat parameters that have neither
 * PARAMFLAG_FIN nor PARAMFLAG_FOUT set?
 *
 * Type libraries made with ICreateTypeInfo (or old ODL compilers) can leave
 * wParamFlags at 0. The probe builds such a type library, registers it for
 * the current user, marshals an object implementing the interface to another
 * apartment and calls it through the type library proxy. For every case it
 * prints what the server saw on entry and what the client got back.
 *
 *   typelib_noflags_probe.exe
 *
 * The type library is written to %TEMP% and unregistered and deleted again at
 * the end.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <stdio.h>

/* not declared by the mingw headers */
HRESULT WINAPI RegisterTypeLibForUser(ITypeLib *, OLECHAR *, OLECHAR *);
HRESULT WINAPI UnRegisterTypeLibForUser(REFGUID, WORD, WORD, LCID, SYSKIND);

static const GUID LIBID_probe = {0x3c1f5a52,0x8d0e,0x4b7a,{0x9e,0x61,0x2f,0x14,0xa8,0x33,0x70,0xc5}};
static const GUID IID_probe   = {0x3c1f5a53,0x8d0e,0x4b7a,{0x9e,0x61,0x2f,0x14,0xa8,0x33,0x70,0xc5}};

#define SYSKIND_NATIVE (sizeof(void *) == 8 ? SYS_WIN64 : SYS_WIN32)

/* what the server saw on entry */
static char seen[256];

typedef struct probe probe;
typedef struct
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(probe *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(probe *);
    ULONG (STDMETHODCALLTYPE *Release)(probe *);
    HRESULT (STDMETHODCALLTYPE *Long)(probe *, LONG, LONG *);       /* flags 0, 0 */
    HRESULT (STDMETHODCALLTYPE *LongIn)(probe *, LONG *);           /* FIN */
    HRESULT (STDMETHODCALLTYPE *LongOut)(probe *, LONG *);          /* FOUT */
    HRESULT (STDMETHODCALLTYPE *Bstr)(probe *, BSTR *);             /* flags 0 */
    HRESULT (STDMETHODCALLTYPE *Variant)(probe *, VARIANT *);       /* flags 0 */
    HRESULT (STDMETHODCALLTYPE *TwoBstr)(probe *, BSTR *, BSTR *);  /* flags 0, 0 */
    HRESULT (STDMETHODCALLTYPE *Unk)(probe *, IUnknown *);          /* flags 0, VT_UNKNOWN */
    HRESULT (STDMETHODCALLTYPE *IfacePtr)(probe *, IUnknown *);     /* flags 0, VT_PTR to the interface */
    HRESULT (STDMETHODCALLTYPE *UnkPtr)(probe *, IUnknown **);      /* flags 0, VT_PTR to VT_UNKNOWN */
} probe_vtbl;
struct probe { const probe_vtbl *lpVtbl; LONG ref; };

static HRESULT STDMETHODCALLTYPE p_QueryInterface(probe *p, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_probe))
    {
        *out = p;
        p->lpVtbl->AddRef(p);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE p_AddRef(probe *p) { return InterlockedIncrement(&p->ref); }
static ULONG STDMETHODCALLTYPE p_Release(probe *p) { return InterlockedDecrement(&p->ref); }

static void seen_long(LONG *v)
{
    if (v) sprintf(seen, "*p=%ld", *v);
    else strcpy(seen, "p=NULL");
}

static HRESULT STDMETHODCALLTYPE p_Long(probe *p, LONG a, LONG *v)
{
    sprintf(seen, "a=%ld ", a);
    if (v) sprintf(seen + strlen(seen), "*p=%ld", *v), *v = a + 100;
    else strcat(seen, "p=NULL");
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE p_LongIn(probe *p, LONG *v) { seen_long(v); if (v) *v = 7; return S_OK; }
static HRESULT STDMETHODCALLTYPE p_LongOut(probe *p, LONG *v) { seen_long(v); if (v) *v = 7; return S_OK; }

static void seen_bstr(char *buf, BSTR *s)
{
    if (!s) strcpy(buf, "p=NULL");
    else if (!*s) strcpy(buf, "*p=NULL");
    else sprintf(buf, "*p=\"%ls\"", *s);
}

static HRESULT STDMETHODCALLTYPE p_Bstr(probe *p, BSTR *s)
{
    seen_bstr(seen, s);
    if (s)
    {
        SysFreeString(*s);
        *s = SysAllocString(L"server");
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_TwoBstr(probe *p, BSTR *s1, BSTR *s2)
{
    seen_bstr(seen, s1);
    strcat(seen, " ");
    seen_bstr(seen + strlen(seen), s2);
    if (s1) { SysFreeString(*s1); *s1 = SysAllocString(L"one"); }
    if (s2) { SysFreeString(*s2); *s2 = SysAllocString(L"two"); }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_Variant(probe *p, VARIANT *v)
{
    if (!v) strcpy(seen, "p=NULL");
    else sprintf(seen, "vt=%d lVal=%ld", V_VT(v), V_VT(v) == VT_I4 ? V_I4(v) : 0);
    if (v)
    {
        VariantClear(v);
        V_VT(v) = VT_I4;
        V_I4(v) = 42;
    }
    return S_OK;
}

/* a plain IUnknown object, one for each side */
static HRESULT STDMETHODCALLTYPE u_QueryInterface(IUnknown *u, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = u;
        IUnknown_AddRef(u);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE u_AddRef(IUnknown *u) { return 2; }
static ULONG STDMETHODCALLTYPE u_Release(IUnknown *u) { return 1; }
static IUnknownVtbl unk_vtbl = { u_QueryInterface, u_AddRef, u_Release };
static IUnknown client_unk = { &unk_vtbl }, server_unk = { &unk_vtbl };

static HRESULT STDMETHODCALLTYPE p_Unk(probe *p, IUnknown *u)
{
    strcpy(seen, u ? "p=object" : "p=NULL");
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_UnkPtr(probe *p, IUnknown **u)
{
    if (!u) strcpy(seen, "p=NULL");
    else
    {
        strcpy(seen, *u ? "*p=object" : "*p=NULL");
        if (*u) IUnknown_Release(*u);
        *u = &server_unk;
        IUnknown_AddRef(*u);
    }
    return S_OK;
}

static const probe_vtbl vtbl =
{
    p_QueryInterface, p_AddRef, p_Release,
    p_Long, p_LongIn, p_LongOut, p_Bstr, p_Variant, p_TwoBstr,
    p_Unk, p_Unk, p_UnkPtr,
};
static probe object = { &vtbl, 1 };

static TYPEDESC td_unk_byval;

static HRESULT build_typelib(const WCHAR *path)
{
    static TYPEDESC td_long = { {0}, VT_I4 }, td_bstr = { {0}, VT_BSTR }, td_var = { {0}, VT_VARIANT };
    static TYPEDESC td_unk = { {0}, VT_UNKNOWN }, td_iface = { {0}, VT_USERDEFINED };
    static const struct
    {
        const WCHAR *names[3];
        int count;
        TYPEDESC *types[2];   /* NULL: VT_I4 by value, &td_unk_byval: VT_UNKNOWN, else pointer to the type */
        USHORT flags[2];
    } funcs[] =
    {
        { {L"Long", L"a", L"p"}, 2, {NULL, &td_long}, {0, 0} },
        { {L"LongIn", L"p"}, 1, {&td_long}, {PARAMFLAG_FIN} },
        { {L"LongOut", L"p"}, 1, {&td_long}, {PARAMFLAG_FOUT} },
        { {L"Bstr", L"p"}, 1, {&td_bstr}, {0} },
        { {L"Variant", L"p"}, 1, {&td_var}, {0} },
        { {L"TwoBstr", L"p1", L"p2"}, 2, {&td_bstr, &td_bstr}, {0, 0} },
        { {L"Unk", L"p"}, 1, {&td_unk_byval}, {0} },
        { {L"IfacePtr", L"p"}, 1, {&td_iface}, {0} },
        { {L"UnkPtr", L"p"}, 1, {&td_unk}, {0} },
    };
    ICreateTypeLib2 *ctl;
    ICreateTypeInfo *cti;
    ITypeLib *stdole;
    ITypeInfo *unk;
    HREFTYPE href;
    HRESULT hr;
    int i, j;

    if (FAILED(hr = CreateTypeLib2(SYSKIND_NATIVE, path, &ctl))) return hr;
    ICreateTypeLib2_SetGuid(ctl, &LIBID_probe);
    ICreateTypeLib2_SetVersion(ctl, 1, 0);
    ICreateTypeLib2_SetLcid(ctl, LOCALE_NEUTRAL);
    ICreateTypeLib2_SetName(ctl, (WCHAR *)L"NoFlagsProbe");
    if (FAILED(hr = ICreateTypeLib2_CreateTypeInfo(ctl, (WCHAR *)L"INoFlagsProbe", TKIND_INTERFACE, &cti))) return hr;
    ICreateTypeInfo_SetGuid(cti, &IID_probe);
    ICreateTypeInfo_SetTypeFlags(cti, TYPEFLAG_FOLEAUTOMATION);
    if (FAILED(hr = LoadRegTypeLib(&IID_StdOle, 2, 0, LOCALE_NEUTRAL, &stdole))) return hr;
    ITypeLib_GetTypeInfoOfGuid(stdole, &IID_IUnknown, &unk);
    ICreateTypeInfo_AddRefTypeInfo(cti, unk, &href);
    ICreateTypeInfo_AddImplType(cti, 0, href);
    td_iface.hreftype = href;
    for (i = 0; i < ARRAYSIZE(funcs); i++)
    {
        FUNCDESC fd;
        ELEMDESC params[2];
        memset(&fd, 0, sizeof(fd));
        memset(params, 0, sizeof(params));
        for (j = 0; j < funcs[i].count; j++)
        {
            if (funcs[i].types[j] == &td_unk_byval)
                params[j].tdesc.vt = VT_UNKNOWN;
            else if (funcs[i].types[j])
            {
                params[j].tdesc.vt = VT_PTR;
                params[j].tdesc.lptdesc = funcs[i].types[j];
            }
            else params[j].tdesc.vt = VT_I4;
            params[j].paramdesc.wParamFlags = funcs[i].flags[j];
        }
        fd.memid = i + 1;
        fd.funckind = FUNC_PUREVIRTUAL;
        fd.invkind = INVOKE_FUNC;
        fd.callconv = CC_STDCALL;
        fd.cParams = funcs[i].count;
        fd.lprgelemdescParam = params;
        fd.elemdescFunc.tdesc.vt = VT_HRESULT;
        if (FAILED(hr = ICreateTypeInfo_AddFuncDesc(cti, i, &fd))) return hr;
        if (FAILED(hr = ICreateTypeInfo_SetFuncAndParamNames(cti, i, (WCHAR **)funcs[i].names, funcs[i].count + 1))) return hr;
    }
    if (FAILED(hr = ICreateTypeInfo_LayOut(cti))) return hr;
    hr = ICreateTypeLib2_SaveAllChanges(ctl);
    ICreateTypeInfo_Release(cti);
    ICreateTypeLib2_Release(ctl);
    return hr;
}

static IStream *stream;
static HANDLE ready;

static DWORD WINAPI server_thread(void *arg)
{
    MSG msg;

    CoInitialize(NULL);
    CoMarshalInterThreadInterfaceInStream(&IID_probe, (IUnknown *)&object, &stream);
    SetEvent(ready);
    while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    CoUninitialize();
    return 0;
}

static void show_bstr(char *buf, BSTR s)
{
    if (s) sprintf(buf, "\"%ls\"", s);
    else strcpy(buf, "NULL");
}

int main(void)
{
    WCHAR path[MAX_PATH];
    ITypeLib *tl;
    probe *proxy;
    HANDLE thread;
    DWORD tid;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetTempPathW(MAX_PATH, path);
    lstrcatW(path, L"typelib_noflags_probe.tlb");
    CoInitialize(NULL);

    hr = build_typelib(path);
    printf("%d-bit, build type library %#lx\n", (int)sizeof(void *) * 8, hr);
    if (FAILED(hr)) return 1;
    hr = LoadTypeLibEx(path, REGKIND_NONE, &tl);
    if (SUCCEEDED(hr)) hr = RegisterTypeLibForUser(tl, path, NULL);
    printf("register %#lx\n", hr);
    if (FAILED(hr)) return 1;

    ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    thread = CreateThread(NULL, 0, server_thread, NULL, 0, &tid);
    WaitForSingleObject(ready, INFINITE);
    hr = CoGetInterfaceAndReleaseStream(stream, &IID_probe, (void **)&proxy);
    printf("unmarshal %#lx\n\n", hr);
    if (FAILED(hr)) goto done;

    {
        LONG v = -1;
        seen[0] = 0;
        hr = proxy->lpVtbl->Long(proxy, 5, &v);
        printf("Long(5, &v=-1)    flags 0,0  hr %#lx  server saw: %-22s client v=%ld\n", hr, seen, v);
        seen[0] = 0;
        hr = proxy->lpVtbl->Long(proxy, 5, NULL);
        printf("Long(5, NULL)     flags 0,0  hr %#lx  server saw: %s\n", hr, seen);
        v = -1; seen[0] = 0;
        hr = proxy->lpVtbl->LongIn(proxy, &v);
        printf("LongIn(&v=-1)     FIN        hr %#lx  server saw: %-22s client v=%ld\n", hr, seen, v);
        v = -1; seen[0] = 0;
        hr = proxy->lpVtbl->LongOut(proxy, &v);
        printf("LongOut(&v=-1)    FOUT       hr %#lx  server saw: %-22s client v=%ld\n", hr, seen, v);
    }
    {
        BSTR s = SysAllocString(L"client");
        char buf[64];
        seen[0] = 0;
        hr = proxy->lpVtbl->Bstr(proxy, &s);
        show_bstr(buf, s);
        printf("Bstr(&\"client\")   flags 0    hr %#lx  server saw: %-22s client got %s\n", hr, seen, buf);
        SysFreeString(s);
        s = NULL; seen[0] = 0;
        hr = proxy->lpVtbl->Bstr(proxy, &s);
        show_bstr(buf, s);
        printf("Bstr(&NULL)       flags 0    hr %#lx  server saw: %-22s client got %s\n", hr, seen, buf);
        SysFreeString(s);
    }
    {
        BSTR s1 = NULL, s2 = NULL;
        char b1[64], b2[64];
        seen[0] = 0;
        hr = proxy->lpVtbl->TwoBstr(proxy, &s1, &s2);
        show_bstr(b1, s1);
        show_bstr(b2, s2);
        printf("TwoBstr(&NULL,&NULL) flags 0,0 hr %#lx  server saw: %-22s client got %s %s\n", hr, seen, b1, b2);
        SysFreeString(s1);
        SysFreeString(s2);
    }
    {
        VARIANT v;
        V_VT(&v) = VT_I4;
        V_I4(&v) = -1;
        seen[0] = 0;
        hr = proxy->lpVtbl->Variant(proxy, &v);
        printf("Variant(&I4 -1)   flags 0    hr %#lx  server saw: %-22s client vt=%d lVal=%ld\n", hr, seen,
               V_VT(&v), V_VT(&v) == VT_I4 ? V_I4(&v) : 0);
        VariantClear(&v);
    }
    {
        IUnknown *u;
        seen[0] = 0;
        hr = proxy->lpVtbl->Unk(proxy, &client_unk);
        printf("Unk(object)       flags 0    hr %#lx  server saw: %s\n", hr, seen);
        seen[0] = 0;
        hr = proxy->lpVtbl->IfacePtr(proxy, &client_unk);
        printf("IfacePtr(object)  flags 0    hr %#lx  server saw: %s\n", hr, seen);
        u = NULL; seen[0] = 0;
        hr = proxy->lpVtbl->UnkPtr(proxy, &u);
        printf("UnkPtr(&NULL)     flags 0    hr %#lx  server saw: %-22s client got %s\n", hr, seen, u ? "object" : "NULL");
        if (u) IUnknown_Release(u);
        u = &client_unk; seen[0] = 0;
        hr = proxy->lpVtbl->UnkPtr(proxy, &u);
        printf("UnkPtr(&object)   flags 0    hr %#lx  server saw: %-22s client got %s\n", hr, seen,
               u == &client_unk ? "its own object" : u ? "a new object" : "NULL");
        if (u && u != &client_unk) IUnknown_Release(u);
    }
    proxy->lpVtbl->Release(proxy);

done:
    PostThreadMessageW(tid, WM_QUIT, 0, 0);
    WaitForSingleObject(thread, INFINITE);
    hr = UnRegisterTypeLibForUser(&LIBID_probe, 1, 0, LOCALE_NEUTRAL, SYSKIND_NATIVE);
    ITypeLib_Release(tl);
    printf("\nunregister %#lx, delete %d\n", hr, DeleteFileW(path));
    CoUninitialize();
    return 0;
}
