/* What happens to the proxies of a single-threaded apartment whose thread
 * exits without calling CoUninitialize?
 *
 * A server thread hosts an object in its own STA and marshals it. A client
 * thread calls CoInitialize, unmarshals a proxy, calls the object once and
 * exits; depending on the case it releases the proxy and calls CoUninitialize
 * first, or not. The main thread then watches for a while whether the
 * object's reference count goes back down, that is, whether the client side
 * released its references.
 *
 *   sta_thread_exit_probe.exe
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

static LONG refs;
static DWORD released_at;
static IStream *stream;
static HANDLE ready;

static HRESULT STDMETHODCALLTYPE obj_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory))
    {
        *out = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE obj_AddRef(IUnknown *iface) { return InterlockedIncrement(&refs); }
static ULONG STDMETHODCALLTYPE obj_Release(IUnknown *iface)
{
    LONG r = InterlockedDecrement(&refs);
    if (r == 1) released_at = GetTickCount();
    return r;
}
static HRESULT STDMETHODCALLTYPE obj_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out)
{
    *out = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}
static HRESULT STDMETHODCALLTYPE obj_LockServer(IClassFactory *iface, BOOL lock) { return S_OK; }

/* IClassFactory, because it has a standard proxy */
static IClassFactoryVtbl vtbl =
{
    (void *)obj_QueryInterface, (void *)obj_AddRef, (void *)obj_Release, obj_CreateInstance, obj_LockServer,
};
static IClassFactory object = { &vtbl };

static DWORD WINAPI server_thread(void *arg)
{
    MSG msg;

    CoInitialize(NULL);
    refs = 1;   /* the server's own reference */
    CoMarshalInterThreadInterfaceInStream(&IID_IClassFactory, (IUnknown *)&object, &stream);
    SetEvent(ready);
    while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    CoUninitialize();
    return 0;
}

enum { EXIT_PLAIN, EXIT_RELEASE, EXIT_UNINIT, EXIT_BOTH };
static const char *names[] =
{
    "exit without Release or CoUninitialize",
    "Release, exit without CoUninitialize",
    "CoUninitialize, exit without Release",
    "Release and CoUninitialize, then exit",
};

static DWORD WINAPI client_thread(void *arg)
{
    int how = (INT_PTR)arg;
    IClassFactory *proxy;
    IUnknown *unk;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoGetInterfaceAndReleaseStream(stream, &IID_IClassFactory, (void **)&proxy);
    if (FAILED(hr))
    {
        printf("  unmarshal failed %#lx\n", hr);
        return 1;
    }
    hr = IClassFactory_CreateInstance(proxy, NULL, &IID_IUnknown, (void **)&unk);
    printf("  call through the proxy %#lx (the object returns CLASS_E_CLASSNOTAVAILABLE), object refs %ld\n", hr, refs);
    if (how == EXIT_RELEASE || how == EXIT_BOTH) IClassFactory_Release(proxy);
    if (how == EXIT_UNINIT || how == EXIT_BOTH) CoUninitialize();
    return 0;
}

int main(void)
{
    int how;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (how = EXIT_PLAIN; how <= EXIT_BOTH; how++)
    {
        HANDLE server, client;
        DWORD server_tid, exited, i;

        ready = CreateEventW(NULL, FALSE, FALSE, NULL);
        released_at = 0;
        server = CreateThread(NULL, 0, server_thread, NULL, 0, &server_tid);
        WaitForSingleObject(ready, INFINITE);
        printf("%s\n  marshalled, object refs %ld\n", names[how], refs);

        client = CreateThread(NULL, 0, client_thread, (void *)(INT_PTR)how, 0, NULL);
        WaitForSingleObject(client, INFINITE);
        exited = GetTickCount();
        CloseHandle(client);

        for (i = 0; i < 100 && !released_at; i++) Sleep(100);
        if (released_at) printf("  client references released %lu ms after the client thread exited\n", released_at - exited);
        else printf("  client references still held 10 s after the client thread exited, object refs %ld\n", refs);

        PostThreadMessageW(server_tid, WM_QUIT, 0, 0);
        WaitForSingleObject(server, 5000);
        CloseHandle(server);
        CloseHandle(ready);
    }
    return 0;
}
