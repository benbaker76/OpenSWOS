/*
 * sokol_impl_d3d11.c -- sokol_gfx's implementation for the Direct3D 11 backend (Windows), and the
 * GPU presenter's native interface for it (src/gpu_native.h; docs/RENDERING.md). OpenGL compiles
 * sokol in src/sokol_impl.c, Metal in src/sokol_impl_metal.m.
 *
 *   Init                a hardware device (feature level 11.1, else 11.0) and a flip-model swapchain on SDL's
 *                       window (RGBA8, two buffers); vsync is Present's interval
 *   Swapchain           the back buffer's render target view, the buffers resized first when the
 *                       window's client area changed
 *   FrameEnd            Present
 *   Read*               a capture: after sg_commit the back buffer copied into a staging texture
 *                       and mapped (before Present, which discards it)
 *   GenerateMipmaps     sokol's images cannot take GENERATE_MIPS: level 0 copied into a helper
 *                       texture that can, GenerateMips, the levels copied back
 *
 * Built by Cygwin's gcc against w32api's headers (d3d11.h, dxgi1_2.h); the HLSL is compiled at
 * run time by d3dcompiler_47.dll, part of Windows 10 (sokol loads it). Cygwin's SDL headers are
 * its Unix ones, so SDL_SysWMinfo names no Win32 member: the window is the first field of the
 * union, which is SDL's ABI.
 */
#include "sokol_config.h"

#if defined(SWOS_SDL_BUILD) && SWOS_GPU && defined(SOKOL_D3D11)

#define COBJMACROS
#define SOKOL_IMPL
#include "../third_party/sokol/sokol_gfx.h"
#include <dxgi1_2.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <SDL_syswm.h>

#define SOKOL_SHDC_IMPL
#include "shaders/present.glsl.h"

#include "gpu_native.h"

static struct {
    HWND                    hwnd;
    ID3D11Device           *dev;
    ID3D11DeviceContext    *ctx;
    IDXGISwapChain1        *sc;
    ID3D11RenderTargetView *rtv;
    UINT                    bw, bh;
    int32_t                 vsync;
    ID3D11Texture2D        *staging;
    UINT                    sw, sh;
    char                    name[128];
    struct { ID3D11Texture2D *tex; ID3D11ShaderResourceView *srv; UINT w, h, mips; DXGI_FORMAT fmt; } mip[8];
} D;

#define RELEASE(p) do { if (p) { IUnknown_Release((IUnknown *)(p)); (p) = NULL; } } while (0)

static int make_rtv(void)
{
    ID3D11Texture2D *bb = NULL;
    if (FAILED(IDXGISwapChain1_GetBuffer(D.sc, 0, &IID_ID3D11Texture2D, (void **)&bb)) || !bb)
        return 0;
    if (FAILED(ID3D11Device_CreateRenderTargetView(D.dev, (ID3D11Resource *)bb, NULL, &D.rtv)))
        D.rtv = NULL;
    RELEASE(bb);
    return D.rtv != NULL;
}

void GpuNative_DrawableSize(int *w, int *h)
{
    RECT r;
    if (D.hwnd && GetClientRect(D.hwnd, &r)) {
        *w = (int)(r.right - r.left);
        *h = (int)(r.bottom - r.top);
    } else {
        *w = *h = 0;
    }
}

int32_t GpuNative_Init(SDL_Window *window, int32_t vsync, int32_t *vsyncOut, sg_environment *env)
{
    SDL_SysWMinfo wm;
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    IDXGIDevice *dxdev = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory = NULL;
    DXGI_ADAPTER_DESC ad;
    DXGI_SWAP_CHAIN_DESC1 desc;
    int w, h;

    memset(&D, 0, sizeof D);
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(window, &wm) || wm.subsystem != SDL_SYSWM_WINDOWS) {
        fprintf(stderr, "[gpu] d3d11: not a Windows window (%s)\n", SDL_GetError());
        return -1;
    }
    memcpy(&D.hwnd, &wm.info, sizeof D.hwnd);       /* (info.win.window: see the head) */
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 levels, 2, D3D11_SDK_VERSION, &D.dev, NULL, &D.ctx)) &&
        FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 levels + 1, 1, D3D11_SDK_VERSION, &D.dev, NULL, &D.ctx))) {   /* (a runtime without 11.1) */
        fprintf(stderr, "[gpu] d3d11: no feature level 11.0 device\n");
        return -1;
    }
    if (FAILED(ID3D11Device_QueryInterface(D.dev, &IID_IDXGIDevice, (void **)&dxdev)) ||
        FAILED(IDXGIDevice_GetAdapter(dxdev, &adapter)) ||
        FAILED(IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&factory))) {
        fprintf(stderr, "[gpu] d3d11: no DXGI 1.2 factory\n");
        RELEASE(dxdev); RELEASE(adapter); RELEASE(factory);
        GpuNative_Shutdown();
        return -1;
    }
    if (SUCCEEDED(IDXGIAdapter_GetDesc(adapter, &ad)))
        WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, D.name, (int)sizeof D.name, NULL, NULL);
    GpuNative_DrawableSize(&w, &h);
    memset(&desc, 0, sizeof desc);
    desc.Width = (UINT)(w > 0 ? w : 1);
    desc.Height = (UINT)(h > 0 ? h : 1);
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    if (FAILED(IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)D.dev, D.hwnd, &desc, NULL, NULL, &D.sc))) {
        fprintf(stderr, "[gpu] d3d11: no swapchain on the window\n");
        RELEASE(dxdev); RELEASE(adapter); RELEASE(factory);
        GpuNative_Shutdown();
        return -1;
    }
    IDXGIFactory2_MakeWindowAssociation(factory, D.hwnd, DXGI_MWA_NO_ALT_ENTER);   /* (SDL does fullscreen) */
    RELEASE(dxdev); RELEASE(adapter); RELEASE(factory);
    D.bw = desc.Width;
    D.bh = desc.Height;
    if (!make_rtv()) {
        GpuNative_Shutdown();
        return -1;
    }
    D.vsync = vsync != 0;
    *vsyncOut = D.vsync;
    env->d3d11.device = D.dev;
    env->d3d11.device_context = D.ctx;
    return 0;
}

const char *GpuNative_Renderer(void) { return D.name; }

sg_pixel_format GpuNative_SwapFormat(void) { return SG_PIXELFORMAT_RGBA8; }

void GpuNative_Shutdown(void)
{
    for (int i = 0; i < 8; i++) {
        RELEASE(D.mip[i].srv);
        RELEASE(D.mip[i].tex);
    }
    RELEASE(D.staging);
    RELEASE(D.rtv);
    RELEASE(D.sc);
    if (D.ctx)
        ID3D11DeviceContext_ClearState(D.ctx);
    RELEASE(D.ctx);
    RELEASE(D.dev);
}

void GpuNative_FrameBegin(void) {}

void GpuNative_Swapchain(sg_swapchain *sc)
{
    if (sc->width > 0 && sc->height > 0 && ((UINT)sc->width != D.bw || (UINT)sc->height != D.bh)) {
        RELEASE(D.rtv);
        if (SUCCEEDED(IDXGISwapChain1_ResizeBuffers(D.sc, 0, (UINT)sc->width, (UINT)sc->height, DXGI_FORMAT_UNKNOWN, 0))) {
            D.bw = (UINT)sc->width;
            D.bh = (UINT)sc->height;
        }
        make_rtv();
    }
    sc->d3d11.render_view = D.rtv;
}

void GpuNative_FrameEnd(void)
{
    if (D.sc)
        IDXGISwapChain1_Present(D.sc, D.vsync ? 1 : 0, 0);
}

int32_t GpuNative_ReadBeforeEndPass(int w, int h)
{
    return D.sc && (UINT)w == D.bw && (UINT)h == D.bh;
}

void GpuNative_ReadAfterCommit(uint8_t *rgba)
{
    ID3D11Texture2D *bb = NULL;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(IDXGISwapChain1_GetBuffer(D.sc, 0, &IID_ID3D11Texture2D, (void **)&bb)) || !bb)
        return;
    if (!D.staging || D.sw != D.bw || D.sh != D.bh) {
        D3D11_TEXTURE2D_DESC td;
        RELEASE(D.staging);
        ID3D11Texture2D_GetDesc(bb, &td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        td.MiscFlags = 0;
        if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &td, NULL, &D.staging)))
            D.staging = NULL;
        D.sw = D.bw;
        D.sh = D.bh;
    }
    if (D.staging) {
        ID3D11DeviceContext_CopyResource(D.ctx, (ID3D11Resource *)D.staging, (ID3D11Resource *)bb);
        if (SUCCEEDED(ID3D11DeviceContext_Map(D.ctx, (ID3D11Resource *)D.staging, 0, D3D11_MAP_READ, 0, &m))) {
            for (UINT y = 0; y < D.sh; y++)
                memcpy(rgba + (size_t)y * D.sw * 4, (const uint8_t *)m.pData + (size_t)y * m.RowPitch, (size_t)D.sw * 4);
            ID3D11DeviceContext_Unmap(D.ctx, (ID3D11Resource *)D.staging, 0);
        }
    }
    RELEASE(bb);
}

void GpuNative_GenerateMipmaps(sg_image img)
{
    const sg_d3d11_image_info info = sg_d3d11_query_image_info(img);
    ID3D11Texture2D *tex = (ID3D11Texture2D *)info.tex2d;
    D3D11_TEXTURE2D_DESC td;
    int i, freeSlot = -1;
    if (!tex)
        return;
    ID3D11Texture2D_GetDesc(tex, &td);
    if (td.MipLevels < 2)
        return;
    for (i = 0; i < 8; i++) {
        if (D.mip[i].tex && D.mip[i].w == td.Width && D.mip[i].h == td.Height && D.mip[i].mips == td.MipLevels &&
            D.mip[i].fmt == td.Format)
            break;
        if (!D.mip[i].tex && freeSlot < 0)
            freeSlot = i;
    }
    if (i == 8) {                       /* a helper of this shape: one that can GenerateMips */
        D3D11_TEXTURE2D_DESC hd = td;
        if (freeSlot < 0) {
            freeSlot = 0;
            RELEASE(D.mip[0].srv);
            RELEASE(D.mip[0].tex);
        }
        i = freeSlot;
        hd.Usage = D3D11_USAGE_DEFAULT;
        hd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hd.CPUAccessFlags = 0;
        hd.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &hd, NULL, &D.mip[i].tex)) ||
            FAILED(ID3D11Device_CreateShaderResourceView(D.dev, (ID3D11Resource *)D.mip[i].tex, NULL, &D.mip[i].srv))) {
            RELEASE(D.mip[i].tex);
            return;
        }
        D.mip[i].w = td.Width;
        D.mip[i].h = td.Height;
        D.mip[i].mips = td.MipLevels;
        D.mip[i].fmt = td.Format;
    }
    ID3D11DeviceContext_CopySubresourceRegion(D.ctx, (ID3D11Resource *)D.mip[i].tex, 0, 0, 0, 0,
                                              (ID3D11Resource *)tex, 0, NULL);
    ID3D11DeviceContext_GenerateMips(D.ctx, D.mip[i].srv);
    for (UINT l = 1; l < td.MipLevels; l++)
        ID3D11DeviceContext_CopySubresourceRegion(D.ctx, (ID3D11Resource *)tex, l, 0, 0, 0,
                                                  (ID3D11Resource *)D.mip[i].tex, l, NULL);
}

#endif
