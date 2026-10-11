/* Which pUnk do the media types returned by a video capture filter carry?
 *
 * Programs that list camera formats do what this probe does: enumerate
 * CLSID_VideoInputDeviceCategory, bind each device to its capture filter, take
 * IAMStreamConfig from the output pin and call GetStreamCaps for every index,
 * freeing each media type with DeleteMediaType(), which releases a non-NULL
 * pUnk. The probe also checks GetFormat and IEnumMediaTypes::Next.
 *
 * Before every call it fills and frees a batch of CoTaskMemAlloc blocks of
 * sizeof(AM_MEDIA_TYPE), so a field the callee leaves unwritten shows up as
 * 0xcccccccc instead of whatever the heap happened to hold.
 *
 *   capture_mt_probe.exe            print pUnk, free the media types by hand
 *   capture_mt_probe.exe --delete   free them with DeleteMediaType() like the
 *                                   programs do (crashes on a garbage pUnk)
 *
 * Needs a video capture device; prints "no devices" otherwise.
 */
#define COBJMACROS
#include <windows.h>
#include <dshow.h>
#include <stdio.h>
#include <string.h>

static BOOL use_delete;
static int bad;

static void dirty_heap(void)
{
    void *blocks[64];
    int i;

    for (i = 0; i < 64; i++)
        if ((blocks[i] = CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE))))
            memset(blocks[i], 0xcc, sizeof(AM_MEDIA_TYPE));
    for (i = 63; i >= 0; i--)
        CoTaskMemFree(blocks[i]);
}

static void free_mt(AM_MEDIA_TYPE *mt)
{
    if (use_delete)
    {
        /* what strmbase/strmiids DeleteMediaType does */
        if (mt->cbFormat) CoTaskMemFree(mt->pbFormat);
        if (mt->pUnk) IUnknown_Release(mt->pUnk);
        CoTaskMemFree(mt);
        return;
    }
    if (mt->cbFormat) CoTaskMemFree(mt->pbFormat);
    CoTaskMemFree(mt);
}

static void check(const char *what, int index, HRESULT hr, AM_MEDIA_TYPE *mt)
{
    if (hr != S_OK || !mt)
    {
        printf("  %-16s %3d  hr %#lx\n", what, index, hr);
        return;
    }
    printf("  %-16s %3d  hr 0  cbFormat %lu  pUnk %p%s\n", what, index, mt->cbFormat,
           mt->pUnk, mt->pUnk ? "  <-- not NULL" : "");
    if (mt->pUnk) bad++;
    fflush(stdout);
    free_mt(mt);
}

static IPin *output_pin(IBaseFilter *filter)
{
    IEnumPins *pins;
    IPin *pin;

    if (FAILED(IBaseFilter_EnumPins(filter, &pins))) return NULL;
    while (IEnumPins_Next(pins, 1, &pin, NULL) == S_OK)
    {
        PIN_DIRECTION dir;
        if (SUCCEEDED(IPin_QueryDirection(pin, &dir)) && dir == PINDIR_OUTPUT)
        {
            IEnumPins_Release(pins);
            return pin;
        }
        IPin_Release(pin);
    }
    IEnumPins_Release(pins);
    return NULL;
}

static void probe_device(IMoniker *moniker)
{
    IAMStreamConfig *config;
    IEnumMediaTypes *types;
    IBaseFilter *filter;
    IPropertyBag *bag;
    AM_MEDIA_TYPE *mt;
    int count, size, i;
    VARIANT name;
    HRESULT hr;
    IPin *pin;
    BYTE caps[256];

    VariantInit(&name);
    if (SUCCEEDED(IMoniker_BindToStorage(moniker, NULL, NULL, &IID_IPropertyBag, (void **)&bag)))
    {
        IPropertyBag_Read(bag, L"FriendlyName", &name, NULL);
        IPropertyBag_Release(bag);
    }
    printf("device %ls\n", V_VT(&name) == VT_BSTR ? V_BSTR(&name) : L"?");
    VariantClear(&name);

    hr = IMoniker_BindToObject(moniker, NULL, NULL, &IID_IBaseFilter, (void **)&filter);
    if (FAILED(hr))
    {
        printf("  BindToObject hr %#lx\n", hr);
        return;
    }
    if (!(pin = output_pin(filter)))
    {
        printf("  no output pin\n");
        IBaseFilter_Release(filter);
        return;
    }
    hr = IPin_QueryInterface(pin, &IID_IAMStreamConfig, (void **)&config);
    if (FAILED(hr))
    {
        printf("  no IAMStreamConfig, hr %#lx\n", hr);
        IPin_Release(pin);
        IBaseFilter_Release(filter);
        return;
    }

    hr = IAMStreamConfig_GetNumberOfCapabilities(config, &count, &size);
    printf("  GetNumberOfCapabilities hr %#lx count %d size %d\n", hr, count, size);
    for (i = 0; SUCCEEDED(hr) && i < count && i < 8; i++)
    {
        mt = NULL;
        dirty_heap();
        hr = IAMStreamConfig_GetStreamCaps(config, i, &mt, caps);
        check("GetStreamCaps", i, hr, mt);
    }

    mt = NULL;
    dirty_heap();
    hr = IAMStreamConfig_GetFormat(config, &mt);
    check("GetFormat", 0, hr, mt);

    if (SUCCEEDED(IPin_EnumMediaTypes(pin, &types)))
    {
        for (i = 0; i < 8; i++)
        {
            mt = NULL;
            dirty_heap();
            hr = IEnumMediaTypes_Next(types, 1, &mt, NULL);
            if (hr != S_OK) break;
            check("EnumMediaTypes", i, hr, mt);
        }
        IEnumMediaTypes_Release(types);
    }

    IAMStreamConfig_Release(config);
    IPin_Release(pin);
    IBaseFilter_Release(filter);
}

int main(int argc, char **argv)
{
    ICreateDevEnum *devenum;
    IEnumMoniker *monikers;
    IMoniker *moniker;
    HRESULT hr;
    int n = 0;

    use_delete = argc > 1 && !strcmp(argv[1], "--delete");
    printf("%d-bit, sizeof(AM_MEDIA_TYPE) %u, free with %s\n", (int)sizeof(void *) * 8,
           (unsigned)sizeof(AM_MEDIA_TYPE), use_delete ? "DeleteMediaType" : "CoTaskMemFree");

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ICreateDevEnum, (void **)&devenum);
    if (FAILED(hr))
    {
        printf("SystemDeviceEnum hr %#lx\n", hr);
        return 1;
    }
    hr = ICreateDevEnum_CreateClassEnumerator(devenum, &CLSID_VideoInputDeviceCategory, &monikers, 0);
    if (hr != S_OK)
    {
        printf("no devices (hr %#lx)\n", hr);
        return 0;
    }
    while (IEnumMoniker_Next(monikers, 1, &moniker, NULL) == S_OK)
    {
        probe_device(moniker);
        IMoniker_Release(moniker);
        n++;
    }
    IEnumMoniker_Release(monikers);
    ICreateDevEnum_Release(devenum);
    printf("%d device(s), %d media type(s) with a non-NULL pUnk\n", n, bad);
    CoUninitialize();
    return 0;
}
