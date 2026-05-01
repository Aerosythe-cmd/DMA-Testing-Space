#include "esp.h"
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────
//  Window creation: transparent borderless click-through topmost
// ─────────────────────────────────────────────────────────────
LRESULT CALLBACK ESP::WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, msg, w, l);
}

HWND ESP::CreateOverlayWindow(int w, int h) {
    const wchar_t* className = L"BF6_ESP_Overlay";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = ESP::WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    // WS_EX_LAYERED + colorkey black for transparency (simple & reliable)
    // WS_EX_TRANSPARENT for click-through
    // WS_EX_TOPMOST always-on-top
    // WS_EX_TOOLWINDOW hides from Alt-Tab and taskbar
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        className, L"",
        WS_POPUP,
        0, 0, w, h,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!hwnd) return nullptr;

    // Black (0,0,0) becomes transparent. We always clear to black before drawing.
    SetLayeredWindowAttributes(hwnd, RGB(0,0,0), 0, LWA_COLORKEY);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

// ─────────────────────────────────────────────────────────────
//  Init — D3D11 device + DXGI swap chain + Direct2D + DirectWrite
// ─────────────────────────────────────────────────────────────
bool ESP::Init(int width, int height) {
    m_width  = width;
    m_height = height;

    m_hwnd = CreateOverlayWindow(width, height);
    if (!m_hwnd) {
        std::cerr << "[ESP] CreateOverlayWindow failed\n";
        return false;
    }

    // ── D3D11 device (no swap chain yet — created via DXGI for flip-model) ──
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;  // required for Direct2D interop
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL featLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION,
        m_d3dDevice.GetAddressOf(), &featLevel, m_d3dCtx.GetAddressOf());
    if (FAILED(hr)) { std::cerr << "[ESP] D3D11CreateDevice failed 0x" << std::hex << hr << "\n"; return false; }

    // ── DXGI swap chain (1-buffer, BGRA, FLIP_SEQUENTIAL) ──
    ComPtr<IDXGIDevice> dxgiDev;
    m_d3dDevice.As(&dxgiDev);
    ComPtr<IDXGIAdapter> dxgiAdapter;
    dxgiDev->GetAdapter(dxgiAdapter.GetAddressOf());
    ComPtr<IDXGIFactory2> dxgiFactory;
    dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width       = width;
    scd.Height      = height;
    scd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;

    hr = dxgiFactory->CreateSwapChainForHwnd(
        m_d3dDevice.Get(), m_hwnd, &scd, nullptr, nullptr,
        m_swapChain.GetAddressOf());
    if (FAILED(hr)) { std::cerr << "[ESP] CreateSwapChainForHwnd failed 0x" << std::hex << hr << "\n"; return false; }

    // ── Direct2D factory + device + context ──
    D2D1_FACTORY_OPTIONS d2dOpts{};
#ifdef _DEBUG
    d2dOpts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1), &d2dOpts,
        reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));
    if (FAILED(hr)) { std::cerr << "[ESP] D2D1CreateFactory failed\n"; return false; }

    hr = m_d2dFactory->CreateDevice(dxgiDev.Get(), m_d2dDevice.GetAddressOf());
    if (FAILED(hr)) { std::cerr << "[ESP] D2D CreateDevice failed\n"; return false; }

    hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        m_d2dCtx.GetAddressOf());
    if (FAILED(hr)) { std::cerr << "[ESP] D2D CreateDeviceContext failed\n"; return false; }

    // ── Bind D2D bitmap target to back buffer ──
    ComPtr<IDXGISurface> dxgiBackBuf;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(dxgiBackBuf.GetAddressOf()));
    D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.f, 96.f);
    hr = m_d2dCtx->CreateBitmapFromDxgiSurface(
        dxgiBackBuf.Get(), &bmpProps, m_d2dTarget.GetAddressOf());
    if (FAILED(hr)) { std::cerr << "[ESP] CreateBitmapFromDxgiSurface failed\n"; return false; }
    m_d2dCtx->SetTarget(m_d2dTarget.Get());

    // ── DirectWrite text format ──
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) { std::cerr << "[ESP] DWriteCreateFactory failed\n"; return false; }

    hr = m_dwriteFactory->CreateTextFormat(
        L"Consolas", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.f, L"en-us",
        m_textFormat.GetAddressOf());
    if (FAILED(hr)) { std::cerr << "[ESP] CreateTextFormat failed\n"; return false; }

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // ── Pre-create cached brushes ──
    auto mkBrush = [&](const Color& c, ComPtr<ID2D1SolidColorBrush>& out) {
        m_d2dCtx->CreateSolidColorBrush(
            D2D1::ColorF(c.r, c.g, c.b, c.a), out.GetAddressOf());
    };
    mkBrush(Colors::Enemy,    m_brushEnemy);
    mkBrush(Colors::Skeleton, m_brushSkeleton);
    mkBrush(Colors::White,    m_brushWhite);
    mkBrush(Colors::Black,    m_brushBlack);
    mkBrush(Colors::White,    m_brushDynamic);

    std::cout << "[ESP] Initialized — overlay window " << width << "x" << height << "\n";
    return true;
}

void ESP::Shutdown() {
    m_textFormat.Reset();
    m_dwriteFactory.Reset();
    m_brushEnemy.Reset();
    m_brushSkeleton.Reset();
    m_brushWhite.Reset();
    m_brushBlack.Reset();
    m_brushDynamic.Reset();
    m_d2dTarget.Reset();
    m_d2dCtx.Reset();
    m_d2dDevice.Reset();
    m_d2dFactory.Reset();
    m_swapChain.Reset();
    m_d3dCtx.Reset();
    m_d3dDevice.Reset();
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

// ─────────────────────────────────────────────────────────────
//  World → Screen
// ─────────────────────────────────────────────────────────────
ScreenPos ESP::WorldToScreen(const Vec3& world, const Matrix4x4& vm) const {
    float clipX = world.x * vm.m[0][0] + world.y * vm.m[0][1]
                + world.z * vm.m[0][2] + vm.m[0][3];
    float clipY = world.x * vm.m[1][0] + world.y * vm.m[1][1]
                + world.z * vm.m[1][2] + vm.m[1][3];
    float clipW = world.x * vm.m[3][0] + world.y * vm.m[3][1]
                + world.z * vm.m[3][2] + vm.m[3][3];

    if (clipW < 0.001f) return { 0, 0, false };

    float ndcX =  clipX / clipW;
    float ndcY = -clipY / clipW;

    return {
        (ndcX + 1.f) * 0.5f * m_width,
        (ndcY + 1.f) * 0.5f * m_height,
        true
    };
}

// ─────────────────────────────────────────────────────────────
//  Render — top-level call once per frame
// ─────────────────────────────────────────────────────────────
void ESP::Render(const std::vector<Entity>& entities, const Matrix4x4& viewMatrix) {
    if (!m_d2dCtx || !m_swapChain) return;
    auto& cfg = Config::Get();

    // Pump the message queue so the overlay window stays responsive
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    m_d2dCtx->BeginDraw();
    // Clear to BLACK (which becomes transparent via LWA_COLORKEY)
    m_d2dCtx->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 1.f));

    for (const auto& e : entities) {
        if (!e.cachedAlive) continue;

        ScreenPos headSP = WorldToScreen(e.cachedBones[Offsets::Bones::Head],   viewMatrix);
        ScreenPos feetSP = WorldToScreen(e.cachedBones[Offsets::Bones::RAnkle], viewMatrix);
        if (!headSP.valid || !feetSP.valid) continue;

        // Push head up a bit and feet down a bit so the box wraps the model
        headSP.y -= 8.f;
        feetSP.y += 4.f;

        if (cfg.espBox)       DrawBox      (headSP, feetSP, Colors::Enemy);
        if (cfg.espSkeleton)  DrawSkeleton (e, viewMatrix);
        if (cfg.espName)      DrawName     (headSP, e.cachedName, Colors::White);
        if (cfg.espWeapon &&
            !e.cachedWeapon.empty())
                              DrawWeapon   (feetSP, e.cachedWeapon);
        if (cfg.espHealth)    DrawHealthBar(headSP, feetSP, e.cachedHealth, 100.f);
        if (cfg.espSnaplines) DrawSnapline (feetSP);
    }

    HRESULT hr = m_d2dCtx->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        // Device lost — caller should re-init
        std::cerr << "[ESP] Direct2D target lost — re-init needed\n";
    }
    m_swapChain->Present(1, 0);
}

// ─────────────────────────────────────────────────────────────
//  Composite shapes
// ─────────────────────────────────────────────────────────────
void ESP::DrawBox(const ScreenPos& head, const ScreenPos& foot, const Color& c) {
    float h  = foot.y - head.y;
    float w  = h * 0.42f;
    float cx = (head.x + foot.x) * 0.5f;
    float x  = cx - w * 0.5f;

    // 1px black outline + 1px colored line on top
    DrawRect(x - 1.f, head.y - 1.f, w + 2.f, h + 2.f, Colors::Black, 1.f);
    DrawRect(x,       head.y,       w,       h,       c,             1.f);
}

void ESP::DrawSkeleton(const Entity& e, const Matrix4x4& vm) {
    for (int i = 0; i < SKELETON_PAIR_COUNT; i++) {
        int b0 = SKELETON_PAIRS[i][0];
        int b1 = SKELETON_PAIRS[i][1];
        ScreenPos s0 = WorldToScreen(e.cachedBones[b0], vm);
        ScreenPos s1 = WorldToScreen(e.cachedBones[b1], vm);
        if (!s0.valid || !s1.valid) continue;
        DrawLine(s0.x, s0.y, s1.x, s1.y, Colors::Skeleton, 1.5f);
    }
}

void ESP::DrawName(const ScreenPos& pos, const std::string& name, const Color& c) {
    DrawText2D(pos.x, pos.y - 18.f, name, c);
}

void ESP::DrawWeapon(const ScreenPos& pos, const std::string& weapon) {
    DrawText2D(pos.x, pos.y + 4.f, weapon, Colors::White);
}

void ESP::DrawHealthBar(const ScreenPos& head, const ScreenPos& foot,
                        float hp, float maxHp) {
    if (maxHp <= 0.f) return;
    float h    = foot.y - head.y;
    float pct  = std::max(0.f, std::min(1.f, hp / maxHp));
    float barH = h * pct;
    float barW = 4.f;
    float x    = head.x - (h * 0.42f * 0.5f) - barW - 3.f;

    DrawFilledRect(x - 1.f, head.y - 1.f, barW + 2.f, h + 2.f, Colors::Black);
    Color hc = { 1.f - pct, pct, 0.f, 1.f };  // red→green
    DrawFilledRect(x, head.y + (h - barH), barW, barH, hc);
}

void ESP::DrawSnapline(const ScreenPos& pos) {
    float cx = m_width  * 0.5f;
    float cy = (float)m_height;
    DrawLine(cx, cy, pos.x, pos.y, Colors::Enemy, 1.f);
}

// ─────────────────────────────────────────────────────────────
//  Direct2D primitives
// ─────────────────────────────────────────────────────────────
ID2D1SolidColorBrush* ESP::GetBrush(const Color& c) {
    // Fast-path the cached color set
    if (c.r == Colors::Enemy.r    && c.g == Colors::Enemy.g    && c.b == Colors::Enemy.b)
        return m_brushEnemy.Get();
    if (c.r == Colors::Skeleton.r && c.g == Colors::Skeleton.g && c.b == Colors::Skeleton.b)
        return m_brushSkeleton.Get();
    if (c.r == Colors::White.r    && c.g == Colors::White.g    && c.b == Colors::White.b)
        return m_brushWhite.Get();
    if (c.r == Colors::Black.r    && c.g == Colors::Black.g    && c.b == Colors::Black.b)
        return m_brushBlack.Get();

    // Dynamic color (e.g. health bar gradient): repaint the dynamic brush
    if (m_brushDynamic)
        m_brushDynamic->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
    return m_brushDynamic.Get();
}

void ESP::DrawFilledRect(float x, float y, float w, float h, const Color& c) {
    if (auto* b = GetBrush(c))
        m_d2dCtx->FillRectangle(D2D1::RectF(x, y, x + w, y + h), b);
}

void ESP::DrawRect(float x, float y, float w, float h, const Color& c, float thickness) {
    if (auto* b = GetBrush(c))
        m_d2dCtx->DrawRectangle(D2D1::RectF(x, y, x + w, y + h), b, thickness);
}

void ESP::DrawLine(float x1, float y1, float x2, float y2, const Color& c, float thickness) {
    if (auto* b = GetBrush(c))
        m_d2dCtx->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2),
                           b, thickness);
}

void ESP::DrawText2D(float x, float y, const std::string& text, const Color& c) {
    if (text.empty() || !m_textFormat) return;
    std::wstring w(text.begin(), text.end());

    // Render in a small box centered on (x, y)
    constexpr float TEXT_W = 200.f;
    constexpr float TEXT_H = 18.f;
    D2D1_RECT_F box = D2D1::RectF(x - TEXT_W * 0.5f, y, x + TEXT_W * 0.5f, y + TEXT_H);

    if (auto* b = GetBrush(c)) {
        m_d2dCtx->DrawText(w.c_str(), (UINT32)w.size(),
            m_textFormat.Get(), box, b,
            D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
}
