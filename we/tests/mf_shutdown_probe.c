/* Deterministic repro: a work item running on a system work queue calls
 * MFUnlockWorkQueue (which takes the rtworkq queue list lock) while MFShutdown
 * is draining the system queues.
 *
 *   mf_shutdown_probe.exe [--queue standard|mt]
 *
 * Exit code 0 when MFShutdown returns and the callback finishes, 3 on a
 * watchdog timeout (deadlock), 1 on other failures.
 */
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <stdio.h>
#include <string.h>

static HANDLE entered, go, done;
static DWORD user_queue, cb_queue = MFASYNC_CALLBACK_QUEUE_STANDARD;
static volatile HRESULT unlock_hr = 0xdeadbeef;
static volatile const char *step = "start";

static DWORD WINAPI watchdog(void *arg)
{
    Sleep(10000);
    printf("TIMEOUT during: %s\n", step);
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 3);
    return 0;
}

static HRESULT WINAPI cb_QueryInterface(IMFAsyncCallback *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IMFAsyncCallback) || IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cb_AddRef(IMFAsyncCallback *iface) { return 2; }
static ULONG WINAPI cb_Release(IMFAsyncCallback *iface) { return 1; }
static HRESULT WINAPI cb_GetParameters(IMFAsyncCallback *iface, DWORD *flags, DWORD *queue)
{
    *flags = 0;
    *queue = cb_queue;
    return S_OK;
}
static HRESULT WINAPI cb_Invoke(IMFAsyncCallback *iface, IMFAsyncResult *result)
{
    SetEvent(entered);
    WaitForSingleObject(go, INFINITE);
    /* give MFShutdown time to start waiting for this callback */
    Sleep(300);
    unlock_hr = MFUnlockWorkQueue(user_queue);
    SetEvent(done);
    return S_OK;
}
static IMFAsyncCallbackVtbl cb_vtbl = {cb_QueryInterface, cb_AddRef, cb_Release, cb_GetParameters, cb_Invoke};
static IMFAsyncCallback cb = {&cb_vtbl};

int main(int argc, char **argv)
{
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 2 && !strcmp(argv[1], "--queue") && !strcmp(argv[2], "mt"))
        cb_queue = MFASYNC_CALLBACK_QUEUE_MULTITHREADED;
    entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    go = CreateEventW(NULL, TRUE, FALSE, NULL);
    done = CreateEventW(NULL, TRUE, FALSE, NULL);
    CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));

    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    printf("MFStartup: hr %#lx\n", hr);
    hr = MFAllocateWorkQueue(&user_queue);
    printf("MFAllocateWorkQueue: hr %#lx, queue %#lx\n", hr, user_queue);
    hr = MFPutWorkItem(cb_queue, &cb, NULL);
    printf("MFPutWorkItem(%s queue): hr %#lx\n", cb_queue == MFASYNC_CALLBACK_QUEUE_STANDARD ? "standard" : "multithreaded", hr);
    step = "waiting for the callback to start";
    if (WaitForSingleObject(entered, 5000))
    {
        printf("FAIL callback did not run\n");
        return 1;
    }
    SetEvent(go);
    step = "MFShutdown";
    hr = MFShutdown();
    printf("MFShutdown: hr %#lx\n", hr);
    step = "waiting for the callback to finish";
    if (WaitForSingleObject(done, 5000))
    {
        printf("FAIL callback did not finish\n");
        return 1;
    }
    printf("MFUnlockWorkQueue in callback: hr %#lx\n", unlock_hr);
    printf("RESULT ok\n");
    return 0;
}
