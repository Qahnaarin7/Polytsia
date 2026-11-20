#pragma once
#include "library.hpp"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

using Microsoft::WRL::ComPtr;

struct Pixels {
    UINT width{}, height{};
    std::vector<BYTE> bytes;
    D2D1_COLOR_F tint{D2D1::ColorF(0x328DFF)};
    D2D1_RECT_F artwork{};
    D2D1_COLOR_F topEdge{}, bottomEdge{};
};

struct IconResult {
    std::wstring key;
    std::shared_ptr<Pixels> pixels;
};

std::shared_ptr<Pixels> DecodeIcon(const fs::path& file, UINT desired);
std::shared_ptr<Pixels> ReadStandardIcon(const fs::path& file, UINT desired);
bool WriteIconPng(const Pixels& pixels, const fs::path& file);
std::shared_ptr<Pixels> DecodeCover(const fs::path& file, UINT desired);
D2D1_RECT_F CropFrame(const Pixels& pixels, D2D1_SIZE_F viewport, Crop crop);
void QueueIcon(HWND window, UINT message, std::wstring key, fs::path shortcut, fs::path image, UINT desired, bool cover = false);
void PauseIconWorker(bool paused);
void StopIconWorker();

struct Canvas {
    struct GlowTexture {
        ComPtr<ID2D1Bitmap> image;
        ULONGLONG lastUsed;
    };
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> writer;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> body, semibold, heading, caption;
    std::map<unsigned, GlowTexture> glows;
    ULONGLONG glowSequence = 0;
    ComPtr<ID2D1RoundedRectangleGeometry> coverMask;
    ComPtr<ID2D1Layer> coverLayer, menuLayer;
    ComPtr<ID2D1BitmapBrush> grain;
    D2D1_SIZE_F coverSize{};
    bool Init(HWND window, float dpi);
    void Drop();
    void Size(HWND window, float dpi);
    void Fill(D2D1_RECT_F box, D2D1_COLOR_F color, float radius = 0);
    void Stroke(D2D1_RECT_F box, D2D1_COLOR_F color, float radius = 0, float weight = 1);
    void Line(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float width = 1.5f);
    void Circle(float x, float y, float radius, D2D1_COLOR_F color, float width = 1.5f);
    void Glow(D2D1_POINT_2F position, float rx, float ry, D2D1_COLOR_F color, D2D1_RECT_F bounds,
              bool ambient = false);
    void AmbientGrain(D2D1_RECT_F bounds, float dpi, float strength);
    void Shadow(D2D1_RECT_F box, float radius, float opacity);
    void Text(std::wstring_view text, D2D1_RECT_F box, D2D1_COLOR_F color,
              IDWriteTextFormat* style = nullptr, bool center = false);
    ComPtr<ID2D1Bitmap> Bitmap(const Pixels& pixels);
    ComPtr<ID2D1Bitmap> TileBackdrop(const Pixels& pixels);
    bool TileImage(ID2D1Bitmap* image, const Pixels& pixels, D2D1_RECT_F picture, D2D1_RECT_F tile,
                   D2D1_COLOR_F background, ID2D1Bitmap* backdrop = nullptr, std::optional<Crop> crop = std::nullopt);
};
