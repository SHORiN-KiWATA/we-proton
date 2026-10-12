/* A game-style window: one thread runs the message loop and presents a D3D11
 * flip-model swap chain with vsync, so it can only see a restore while Present
 * returns. After argv[1] seconds (default 3) the window minimizes itself and keeps
 * presenting; something outside (the compositor) restores it. A watchdog thread
 * prints the frame count every half second, so a main loop stuck in Present shows
 * up as a count that stops moving. Exits after argv[2] seconds (default 12).
 *
 *   minimize_restore_probe.exe [minimize after] [exit after] [popup]
 *
 * "popup" uses a WS_POPUP window covering the monitor instead of a normal window. */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DWORD start;
static volatile LONG frames;
static volatile LONG done;

static void say(const char *fmt, ...)
{
    va_list ap;
    printf("%6.2f ", (GetTickCount() - start) / 1000.0);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n"); fflush(stdout);
}

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
        say("WM_SIZE %s %ux%u", wp == SIZE_MINIMIZED ? "minimized" : wp == SIZE_MAXIMIZED ? "maximized" : "restored",
            LOWORD(lp), HIWORD(lp));
        break;
    case WM_ACTIVATE:
        say("WM_ACTIVATE %u minimized=%u", LOWORD(wp), HIWORD(wp));
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD WINAPI watchdog(void *arg)
{
    LONG last = -1;
    while (!done)
    {
        LONG now = frames;
        say("frames %ld%s", now, now == last ? "  (main loop not presenting)" : "");
        last = now;
        Sleep(500);
    }
    return 0;
}

int main(int argc, char **argv)
{
    DWORD minimize_after = argc > 1 ? atoi(argv[1]) * 1000 : 3000;
    DWORD exit_after = argc > 2 ? atoi(argv[2]) * 1000 : 12000;
    BOOL popup = argc > 3 && !strcmp(argv[3], "popup");
    WNDCLASSW wc = {0};
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    IDXGISwapChain1 *swapchain;
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    ID3D11Texture2D *backbuffer;
    ID3D11RenderTargetView *rtv;
    IDXGIDevice *dxgi_device;
    IDXGIAdapter *adapter;
    IDXGIFactory2 *factory;
    BOOL minimized = FALSE;
    HWND hwnd;
    HRESULT hr;
    MSG msg;

    start = GetTickCount();
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"minimize_restore_probe";
    RegisterClassW(&wc);
    if (popup)
        hwnd = CreateWindowW(wc.lpszClassName, L"minimize_restore_probe", WS_POPUP | WS_VISIBLE, 0, 0,
                             GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, NULL, NULL);
    else
        hwnd = CreateWindowW(wc.lpszClassName, L"minimize_restore_probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             100, 100, 800, 600, NULL, NULL, NULL, NULL);

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device, NULL, &context);
    if (FAILED(hr)) { say("D3D11CreateDevice failed %#lx", hr); return 1; }
    ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    IDXGIDevice_GetAdapter(dxgi_device, &adapter);
    IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&factory);
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)device, hwnd, &desc, NULL, NULL, &swapchain);
    if (FAILED(hr)) { say("CreateSwapChainForHwnd failed %#lx", hr); return 1; }
    IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D, (void **)&backbuffer);
    ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)backbuffer, NULL, &rtv);
    say("presenting, %s window", popup ? "popup" : "normal");
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);

    for (;;)
    {
        float color[4] = {0.0f, 0.3f + 0.3f * ((frames / 30) & 1), 0.6f, 1.0f};

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) goto out;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!minimized && GetTickCount() - start >= minimize_after)
        {
            minimized = TRUE;
            say("ShowWindow(SW_MINIMIZE)");
            ShowWindow(hwnd, SW_MINIMIZE);
        }
        if (GetTickCount() - start >= exit_after) break;
        ID3D11DeviceContext_OMSetRenderTargets(context, 1, &rtv, NULL);
        ID3D11DeviceContext_ClearRenderTargetView(context, rtv, color);
        hr = IDXGISwapChain1_Present(swapchain, 1, 0);
        if (FAILED(hr)) say("Present failed %#lx", hr);
        InterlockedIncrement(&frames);
    }
out:
    done = 1;
    say("exit, %ld frames, IsIconic=%d", frames, IsIconic(hwnd));
    return 0;
}
