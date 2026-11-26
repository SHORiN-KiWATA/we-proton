/* A layered popup window that is made a child of another window with
 * SetParent(), over a parent that presents frames with Direct3D: where does
 * the popup's content end up, and what is behind its transparent pixels?
 *
 * The host is a borderless top-level window (WS_POPUP | WS_VISIBLE |
 * WS_CLIPSIBLINGS) with a Direct3D 11 swap chain that keeps presenting a solid
 * colour, blue at first. The popup is created the way toolkit overlay windows
 * are: a hidden top-level WS_POPUP window that gets WS_EX_LAYERED |
 * WS_EX_TOOLWINDOW, is updated once with UpdateLayeredWindow, is then given to
 * the host with SetParent (its WS_POPUP style stays), resized to the host's
 * client area, shown without activation and updated again. Its content, a
 * premultiplied 32 bpp DIB, in thirds from left to right:
 *
 *   opaque green | red at 50 % alpha | fully transparent
 *
 * At each step the probe prints "SAMPLE <step> <x> <y>" (the screen position of
 * the host's client area) and keeps presenting for two seconds, so that a
 * grabber outside the program can read the pixels; layered_popup_child_grab.py
 * does that on X11. With "selfgrab" the probe reads the screen itself through
 * GDI and prints the colours, which is what to use on Windows (in an
 * interactive session). Six spots are read, at 1/12, 3/12 ... 11/12 of the
 * width, half way down.
 *
 * Steps: host-only, popup-shown, frames-changed (the host now presents dark
 * cyan), popup-moved (the popup covers the right half only), popup-hidden,
 * popup-shown-again, popup-destroyed. With "fade" the popup fades in the way
 * toolkits animate a window's opacity: its image is set with a constant alpha
 * of 0, then UpdateLayeredWindow is called without an image for 128 (step
 * popup-fading) and 255.
 *
 *   layered_popup_child_probe.exe [gdi] [flip] [clip] [caption] [full] [indirect] [fade] [selfgrab]
 *
 *   gdi       the host paints itself with GDI instead of Direct3D
 *   flip      flip model swap chain (default: bitblt model)
 *   clip      the host has WS_CLIPCHILDREN
 *   caption   the host is a WS_OVERLAPPEDWINDOW
 *   full      the host covers the primary monitor (default: 600 x 400 at 100,100)
 *   indirect  UpdateLayeredWindowIndirect with a dirty rectangle
 *   fade      fade the popup in: image at opacity 0, then opacity alone
 *   selfgrab  read the screen at each step and print the colours
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>

static int W = 600, H = 400;

static HWND host, popup;
static BOOL use_gdi, use_flip, use_indirect, use_fade, selfgrab;
static COLORREF gdi_color = RGB(0, 0, 255);
static float d3d_color[4] = {0.0f, 0.0f, 1.0f, 1.0f};

static IDXGISwapChain *swapchain;
static ID3D11Device *device;
static ID3D11DeviceContext *context;
static ID3D11RenderTargetView *rtv;

static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_ERASEBKGND && use_gdi)
    {
        RECT rc;
        HBRUSH brush = CreateSolidBrush(gdi_color);
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, brush);
        DeleteObject(brush);
        return 1;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static BOOL init_d3d(void)
{
    static const D3D_DRIVER_TYPE types[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
    DXGI_SWAP_CHAIN_DESC desc = {0};
    ID3D11Texture2D *back;
    HRESULT hr = E_FAIL;
    unsigned int i;

    desc.BufferDesc.Width = W;
    desc.BufferDesc.Height = H;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = use_flip ? 2 : 1;
    desc.OutputWindow = host;
    desc.Windowed = TRUE;
    desc.SwapEffect = use_flip ? 4 /* DXGI_SWAP_EFFECT_FLIP_DISCARD */ : DXGI_SWAP_EFFECT_DISCARD;

    for (i = 0; i < ARRAYSIZE(types) && FAILED(hr); i++)
    {
        hr = D3D11CreateDeviceAndSwapChain(NULL, types[i], NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                                           &desc, &swapchain, &device, NULL, &context);
        if (SUCCEEDED(hr)) printf("d3d11 device: %s, %s model\n", i ? "WARP" : "hardware", use_flip ? "flip" : "bitblt");
    }
    if (FAILED(hr))
    {
        printf("D3D11CreateDeviceAndSwapChain failed, hr %#lx\n", hr);
        return FALSE;
    }
    IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D, (void **)&back);
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)back, NULL, &rtv);
    ID3D11Texture2D_Release(back);
    return SUCCEEDED(hr);
}

static void render(void)
{
    if (!swapchain)
    {
        Sleep(10);
        return;
    }
    ID3D11DeviceContext_OMSetRenderTargets(context, 1, &rtv, NULL);
    ID3D11DeviceContext_ClearRenderTargetView(context, rtv, d3d_color);
    IDXGISwapChain_Present(swapchain, 1, 0);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        render();
    }
}

/* change the opacity of the popup as a whole, keeping its image */
static void set_popup_opacity(BYTE alpha)
{
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};

    if (!UpdateLayeredWindow(popup, 0, NULL, NULL, 0, NULL, 0, &blend, ULW_ALPHA))
        printf("UpdateLayeredWindow without an image, opacity %u, failed, error %lu\n", alpha, GetLastError());
}

/* fill the popup: thirds of opaque green, half transparent red, transparent */
static void update_popup(int width, int height, BYTE alpha)
{
    BITMAPINFO bmi = {{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB}};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    POINT src = {0, 0};
    SIZE size = {width, height};
    DWORD *bits;
    HDC hdc = CreateCompatibleDC(0);
    HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&bits, 0, 0);
    HGDIOBJ old = SelectObject(hdc, bmp);
    int x, y;

    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            bits[y * width + x] = x < width / 3 ? 0xff00ff00          /* opaque green */
                                : x < 2 * width / 3 ? 0x80800000      /* red, alpha 128, premultiplied */
                                : 0x00000000;                         /* transparent */

    if (use_indirect)
    {
        /* with a dirty rectangle, the way toolkits flush their backing store */
        RECT dirty = {0, 0, width, height};
        UPDATELAYEREDWINDOWINFO info = {sizeof(info), 0, NULL, &size, hdc, &src, 0, &blend, ULW_ALPHA, &dirty};
        if (!UpdateLayeredWindowIndirect(popup, &info))
            printf("UpdateLayeredWindowIndirect %dx%d failed, error %lu\n", width, height, GetLastError());
    }
    else if (!UpdateLayeredWindow(popup, 0, NULL, &size, hdc, &src, 0, &blend, ULW_ALPHA))
        printf("UpdateLayeredWindow %dx%d failed, error %lu\n", width, height, GetLastError());
    SelectObject(hdc, old);
    DeleteObject(bmp);
    DeleteDC(hdc);
}

static void sample(const char *step)
{
    POINT org = {0, 0};
    int i;

    ClientToScreen(host, &org);
    printf("SAMPLE %s %ld %ld\n", step, org.x, org.y);
    fflush(stdout);
    pump(selfgrab ? 1000 : 2000);
    if (selfgrab)
    {
        HDC screen = GetDC(0), mem = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, W, H);
        HGDIOBJ old = SelectObject(mem, bmp);

        BitBlt(mem, 0, 0, W, H, screen, org.x, org.y, SRCCOPY | CAPTUREBLT);
        for (i = 1; i < 12; i += 2)
        {
            COLORREF c = GetPixel(mem, i * W / 12, H / 2);
            printf("  %2d/12: r=%3d g=%3d b=%3d\n", i, GetRValue(c), GetGValue(c), GetBValue(c));
        }
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(0, screen);
    }
    if (popup)
    {
        /* which window is at the middle of each third of the host? */
        printf("  WindowFromPoint:");
        for (i = 1; i < 6; i += 2)
        {
            POINT pt = {org.x + i * W / 6, org.y + H / 2};
            HWND hit = WindowFromPoint(pt);
            printf(" %s", hit == popup ? "popup" : hit == host ? "host" : "other");
        }
        printf("\n");
    }
    fflush(stdout);
}

static const char *name(HWND hwnd)
{
    static char buf[32];
    if (!hwnd) return "0";
    if (hwnd == host) return "host";
    if (hwnd == popup) return "popup";
    if (hwnd == GetDesktopWindow()) return "desktop";
    sprintf(buf, "%p", hwnd);
    return buf;
}

static void describe(const char *when)
{
    RECT rc;

    GetWindowRect(popup, &rc);
    printf("%s: style %08lx exstyle %08lx GetParent %s", when, GetWindowLongA(popup, GWL_STYLE),
           GetWindowLongA(popup, GWL_EXSTYLE), name(GetParent(popup)));
    printf(" GA_PARENT %s", name(GetAncestor(popup, GA_PARENT)));
    printf(" GW_OWNER %s", name(GetWindow(popup, GW_OWNER)));
    printf(" GA_ROOT %s", name(GetAncestor(popup, GA_ROOT)));
    printf(" visible %d screen rect %ld,%ld-%ld,%ld", IsWindowVisible(popup), rc.left, rc.top, rc.right, rc.bottom);
    MapWindowPoints(0, host, (POINT *)&rc, 2);
    printf(" in host client %ld,%ld-%ld,%ld\n", rc.left, rc.top, rc.right, rc.bottom);
    printf("  host first child %s, own X window %#lx\n", name(GetWindow(host, GW_CHILD)),
           (unsigned long)(ULONG_PTR)GetPropA(popup, "__wine_x11_whole_window"));
}

int main(int argc, char **argv)
{
    DWORD style = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS;
    WNDCLASSA wc = {0};
    HWND prev;
    int i, x = 100, y = 100;
    RECT rc;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "gdi")) use_gdi = TRUE;
        else if (!strcmp(argv[i], "flip")) use_flip = TRUE;
        else if (!strcmp(argv[i], "clip")) style |= WS_CLIPCHILDREN;
        else if (!strcmp(argv[i], "caption")) style = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;
        else if (!strcmp(argv[i], "selfgrab")) selfgrab = TRUE;
        else if (!strcmp(argv[i], "indirect")) use_indirect = TRUE;
        else if (!strcmp(argv[i], "fade")) use_fade = TRUE;
        else if (!strcmp(argv[i], "full"))
        {
            /* cover the primary monitor, like a borderless fullscreen game */
            W = GetSystemMetrics(SM_CXSCREEN);
            H = GetSystemMetrics(SM_CYSCREEN);
            x = y = 0;
        }
    }
    SetRect(&rc, 0, 0, W, H);

    wc.lpfnWndProc = host_proc;
    wc.hInstance = GetModuleHandleA(0);
    wc.lpszClassName = "layered_popup_child_host";
    wc.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.lpszClassName = "layered_popup_child_popup";
    RegisterClassA(&wc);

    AdjustWindowRect(&rc, style, FALSE);
    host = CreateWindowExA(0, "layered_popup_child_host", "layered_popup_child_probe", style,
                           x, y, rc.right - rc.left, rc.bottom - rc.top, 0, 0, 0, 0);
    printf("host style %08lx, %s, client area %d x %d\n", GetWindowLongA(host, GWL_STYLE),
           use_gdi ? "GDI" : "Direct3D 11", W, H);
    if (!use_gdi && !init_d3d()) return 1;
    pump(1500);
    sample("host-only");

    /* a hidden top-level popup that becomes layered, like a toolkit's translucent window */
    popup = CreateWindowExA(0, "layered_popup_child_popup", "popup", WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                            x + W / 2 - 160, y + H / 2 - 120, 320, 240, 0, 0, 0, 0);
    SetWindowLongA(popup, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TOOLWINDOW);
    update_popup(320, 240, 255);
    describe("top-level");

    SetLastError(0xdeadbeef);
    prev = SetParent(popup, host);
    printf("SetParent returned %s, error %#lx\n", name(prev), GetLastError());
    describe("after SetParent");

    SetWindowPos(popup, 0, 0, 0, W, H, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(popup, SW_SHOWNOACTIVATE);
    if (use_fade)
    {
        /* fade in: the image comes with opacity 0, then only the opacity changes */
        update_popup(W, H, 0);
        pump(300);
        set_popup_opacity(128);
        pump(1000);
        sample("popup-fading");
        set_popup_opacity(255);
    }
    else update_popup(W, H, 255);
    describe("shown");
    pump(1000);
    sample("popup-shown");

    gdi_color = RGB(0, 128, 128);
    d3d_color[1] = d3d_color[2] = 128 / 255.0f;
    if (use_gdi) RedrawWindow(host, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    pump(1000);
    sample("frames-changed");

    SetWindowPos(popup, 0, W / 2, 0, W / 2, H, SWP_NOZORDER | SWP_NOACTIVATE);
    update_popup(W / 2, H, 255);
    describe("moved");
    pump(1000);
    sample("popup-moved");

    ShowWindow(popup, SW_HIDE);
    pump(1000);
    sample("popup-hidden");

    ShowWindow(popup, SW_SHOWNOACTIVATE);
    update_popup(W / 2, H, 255);
    pump(1000);
    sample("popup-shown-again");

    DestroyWindow(popup);
    popup = 0;
    pump(1000);
    sample("popup-destroyed");

    DestroyWindow(host);
    return 0;
}
