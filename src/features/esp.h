#pragma once
#include "../game/entity.h"
#include "../game/offsets.h"
#include "../config/config.h"
#include <d3d11.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

// ─────────────────────────────────────────────────────
//  ESP — DirectX 11 + Direct2D + DirectWrite overlay
//  Transparent always-on-top click-through window.
//  Draws: box, full skeleton, name, weapon, HP bar, snaplines.
//
//  Direct2D handles all 2D primitives (line, rect, fill).
//  DirectWrite handles text.
//  D3D11 is the surface backing — required for Direct2D 1.1
//  acceleration on top of a DXGI swap chain.
// ─────────────────────────────────────────────────────

struct Color { float r, g, b, a; };

namespace Colors {
    constexpr Color Enemy    = {1.f, 0.1f, 0.1f, 1.f};
    constexpr Color Skeleton = {1.f, 1.f,  0.f,  1.f};
    constexpr Color Friendly = {0.1f, 1.f, 0.1f, 1.f};
    constexpr Color White    = {1.f, 1.f,  1.f,  1.f};
    constexpr Color Black    = {0.f, 0.f,  0.f,  1.f};
    constexpr Color Health   = {0.1f, 0.9f, 0.1f, 1.f};
}

struct ScreenPos { float x, y; bool valid; };

class ESP {
public:
    static ESP& Get() {
        static ESP inst;
        return inst;
    }

    // Creates a transparent fullscreen overlay window and initializes
    // D3D11 + Direct2D + DirectWrite. Returns false on any init failure.
    bool Init(int width, int height);
    void Shutdown();

    bool IsReady() const { return m_d2dCtx != nullptr; }

    // Call once per frame from main loop
    void Render(const std::vector<Entity>& entities,
                const Matrix4x4& viewMatrix);

private:
    ESP() = default;

    // ── Window ───────────────────────────────────────────
    HWND CreateOverlayWindow(int w, int h);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    // ── World→Screen projection ──────────────────────────
    ScreenPos WorldToScreen(const Vec3& world,
                             const Matrix4x4& viewMatrix) const;

    // ── Drawing primitives (Direct2D) ────────────────────
    void DrawBox      (const ScreenPos& head, const ScreenPos& foot, const Color& c);
    void DrawSkeleton (const Entity& e, const Matrix4x4& vm);
    void DrawName     (const ScreenPos& pos, const std::string& name, const Color& c);
    void DrawWeapon   (const ScreenPos& pos, const std::string& weapon);
    void DrawHealthBar(const ScreenPos& head, const ScreenPos& foot,
                       float hp, float maxHp);
    void DrawSnapline (const ScreenPos& pos);

    void DrawFilledRect(float x, float y, float w, float h, const Color& c);
    void DrawRect      (float x, float y, float w, float h, const Color& c, float thickness = 1.f);
    void DrawLine      (float x1, float y1, float x2, float y2, const Color& c, float thickness = 1.f);
    void DrawText2D    (float x, float y, const std::string& text, const Color& c);

    ID2D1SolidColorBrush* GetBrush(const Color& c);

    // ── State ────────────────────────────────────────────
    HWND m_hwnd = nullptr;
    int  m_width  = 0;
    int  m_height = 0;

    Microsoft::WRL::ComPtr<ID3D11Device>           m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_d3dCtx;
    Microsoft::WRL::ComPtr<IDXGISwapChain1>        m_swapChain;
    Microsoft::WRL::ComPtr<ID2D1Factory1>          m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1Device>            m_d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext>     m_d2dCtx;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1>           m_d2dTarget;
    Microsoft::WRL::ComPtr<IDWriteFactory>         m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>      m_textFormat;

    // Brush cache — small set of colors so we don't realloc per call
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>   m_brushEnemy;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>   m_brushSkeleton;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>   m_brushWhite;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>   m_brushBlack;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>   m_brushDynamic;  // for arbitrary colors
};
