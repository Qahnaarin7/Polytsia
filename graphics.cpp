#include "graphics.hpp"
#include "localization.hpp"
#include <wincodec.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace {
D2D1_COLOR_F ArtworkEdgeColor(const Pixels& pixels, UINT left, UINT top, UINT right, UINT bottom, bool fromBottom) {
    std::array<std::pair<float, D2D1_COLOR_F>, 24> samples{};
    size_t count = 0;
    const UINT width = right - left, height = bottom - top;
    // Sample the upper and lower contours away from the center of the logo.
    for (UINT i = 1; i <= 12; ++i) {
        const UINT inset = (width - 1) * i / 36;
        for (const UINT x : {left + inset, right - 1 - inset}) {
            for (UINT offset = 0; offset < std::max(1u, height / 3); ++offset) {
                const UINT y = fromBottom ? bottom - 1 - offset : top + offset;
                const auto* p = &pixels.bytes[(static_cast<size_t>(y) * pixels.width + x) * 4];
                if (p[3] < 240) continue;
                const float alpha = p[3];
                const auto color = D2D1::ColorF(p[2] / alpha, p[1] / alpha, p[0] / alpha);
                samples[count++] = {.2126f * color.r + .7152f * color.g + .0722f * color.b, color};
                break;
            }
        }
    }
    if (!count) return {};
    const auto median = samples.begin() + count / 2;
    std::nth_element(samples.begin(), median, samples.begin() + count,
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    return median->second;
}

std::shared_ptr<Pixels> ReadPixels(IWICImagingFactory* factory, IWICBitmapSource* source, UINT desired) {
    UINT width = 0, height = 0;
    if (FAILED(source->GetSize(&width, &height)) || !width || !height ||
        width > 8192 || height > 8192 || static_cast<uint64_t>(width) * height > 32000000) return {};
    ComPtr<IWICBitmapScaler> scaler;
    const UINT extent = std::max(width, height);
    if (extent > desired) {
        width = std::max(1u, width * desired / extent);
        height = std::max(1u, height * desired / extent);
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(source, width, height, WICBitmapInterpolationModeFant))) return {};
        source = scaler.Get();
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                     nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    auto result = std::make_shared<Pixels>();
    result->width = width;
    result->height = height;
    result->bytes.resize(static_cast<size_t>(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(result->bytes.size()),
                                    result->bytes.data()))) return {};
    float red = 0, green = 0, blue = 0, weight = 0;
    UINT left = width, top = height, right = 0, bottom = 0, opaque = 0;
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const auto* pixel = result->bytes.data() + (static_cast<size_t>(y) * width + x) * 4;
            if (pixel[3] >= 64) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
            if (pixel[3] >= 220) ++opaque;
            if (pixel[3] < 102) continue;
            const float alpha = pixel[3] / 255.f;
            const float r = static_cast<float>(pixel[2]) / pixel[3];
            const float g = static_cast<float>(pixel[1]) / pixel[3];
            const float b = static_cast<float>(pixel[0]) / pixel[3];
            const float max = std::max({r, g, b});
            const float w = (max - std::min({r, g, b})) * max * alpha;
            red += r * w;
            green += g * w;
            blue += b * w;
            weight += w;
        }
    }
    if (left < right && top < bottom) {
        result->artwork = D2D1::RectF(static_cast<float>(left), static_cast<float>(top),
                                    static_cast<float>(right), static_cast<float>(bottom));
        // Rounded artwork with an opaque plate can bleed its edge color into its transparent corners.
        if (opaque >= (right - left) * (bottom - top) * .76f) {
            result->topEdge = ArtworkEdgeColor(*result, left, top, right, bottom, false);
            result->bottomEdge = ArtworkEdgeColor(*result, left, top, right, bottom, true);
            if (!result->topEdge.a) result->topEdge = result->bottomEdge;
            if (!result->bottomEdge.a) result->bottomEdge = result->topEdge;
        }
    }
    if (weight > width * height * .025f) {
        const float max = std::max({red, green, blue});
        result->tint = D2D1::ColorF(.14f + .86f * red / max, .14f + .86f * green / max,
                                  .14f + .86f * blue / max);
    }
    return result;
}

ComPtr<IWICImagingFactory> Wic() {
    ComPtr<IWICImagingFactory> factory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    return factory;
}

std::shared_ptr<Pixels> ReadIcon(HICON icon, UINT desired) {
    const auto factory = Wic();
    ComPtr<IWICBitmap> bitmap;
    if (!factory || FAILED(factory->CreateBitmapFromHICON(icon, &bitmap))) return {};
    return ReadPixels(factory.Get(), bitmap.Get(), desired);
}

std::shared_ptr<Pixels> ShellIcon(const fs::path& path, UINT desired) {
    SHFILEINFOW info{};
    HICON icon = nullptr;
    if (SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX)) {
        ComPtr<IImageList> images;
        const int size = desired > 60 ? SHIL_JUMBO : SHIL_EXTRALARGE;
        if (SUCCEEDED(SHGetImageList(size, IID_PPV_ARGS(&images)))) {
            images->GetIcon(info.iIcon, ILD_TRANSPARENT, &icon);
        }
    }
    if (!icon && SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON)) {
        icon = info.hIcon;
    }
    if (!icon) return {};
    auto pixels = ReadIcon(icon, desired);
    DestroyIcon(icon);
    return pixels;
}

struct IconRequest {
    HWND window;
    UINT message;
    std::wstring key;
    fs::path shortcut, image;
    UINT desired;
    bool cover;
};

struct IconQueue {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<IconRequest> requests;
    bool stopped = false, paused = false;
};

std::shared_ptr<IconQueue> queue;

void IconLoop(std::shared_ptr<IconQueue> worker) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (;;) {
        std::unique_lock lock(worker->mutex);
        worker->wake.wait(lock, [&] { return worker->stopped || (!worker->paused && !worker->requests.empty()); });
        if (worker->stopped) break;
        auto request = std::move(worker->requests.front());
        worker->requests.pop_front();
        lock.unlock();
        auto result = std::make_unique<IconResult>();
        result->key = std::move(request.key);
        if (request.cover) result->pixels = DecodeCover(request.image, request.desired);
        else {
            if (!request.image.empty()) result->pixels = DecodeIcon(request.image, request.desired);
            if (!result->pixels) result->pixels = ShellIcon(request.shortcut, request.desired);
        }
        lock.lock();
        if (!worker->stopped && PostMessageW(request.window, request.message, 0,
                                           reinterpret_cast<LPARAM>(result.get()))) result.release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
}
}

static std::shared_ptr<Pixels> DecodeImage(const fs::path& file, UINT desired, bool cover) {
    const auto extension = Fold(file.extension().wstring());
    const bool ico = extension == L".ico";
    const bool jpeg = extension == L".jpg" || extension == L".jpeg";
    if (extension != L".png" && !(cover ? jpeg : ico)) return {};
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error || size > 32 * 1024 * 1024) return {};
    const auto factory = Wic();
    ComPtr<IWICBitmapDecoder> decoder;
    GUID format{};
    const GUID& expected = ico ? GUID_ContainerFormatIco : jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng;
    if (!factory || FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder)) || FAILED(decoder->GetContainerFormat(&format)) ||
        format != expected) return {};
    UINT count = 0;
    if (FAILED(decoder->GetFrameCount(&count))) return {};
    std::vector<std::pair<UINT, UINT>> candidates;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IWICBitmapFrameDecode> frame;
        UINT width = 0, height = 0;
        if (FAILED(decoder->GetFrame(i, &frame)) || FAILED(frame->GetSize(&width, &height))) continue;
        const UINT edge = std::max(width, height);
        const UINT cost = edge >= desired ? edge - desired : desired - edge + 10000;
        candidates.emplace_back(cost, i);
    }
    std::stable_sort(candidates.begin(), candidates.end());
    for (const auto& candidate : candidates) {
        ComPtr<IWICBitmapFrameDecode> frame;
        if (SUCCEEDED(decoder->GetFrame(candidate.second, &frame))) {
            if (auto image = ReadPixels(factory.Get(), frame.Get(), desired)) return image;
        }
    }
    return {};
}

std::shared_ptr<Pixels> DecodeIcon(const fs::path& file, UINT desired) {
    if (Fold(file.extension().wstring()) == L".exe") {
        HICON icon{};
        const UINT size = std::clamp(desired, 32u, 256u);
        const auto result = SHDefExtractIconW(file.c_str(), 0, 0, &icon, nullptr, size);
        auto pixels = result == S_OK && icon ? ReadIcon(icon, desired) : nullptr;
        if (icon) DestroyIcon(icon);
        if (pixels && pixels->artwork.right <= pixels->artwork.left) return {};
        return pixels;
    }
    return DecodeImage(file, desired, false);
}

std::shared_ptr<Pixels> ReadStandardIcon(const fs::path& file, UINT desired) {
    return ShellIcon(file, desired);
}

bool WriteIconPng(const Pixels& pixels, const fs::path& file) {
    const auto factory = Wic();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICFormatConverter> converter;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    return factory &&
        SUCCEEDED(factory->CreateBitmapFromMemory(pixels.width, pixels.height, GUID_WICPixelFormat32bppPBGRA,
            pixels.width * 4, static_cast<UINT>(pixels.bytes.size()), const_cast<BYTE*>(pixels.bytes.data()), &bitmap)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone,
                                        nullptr, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
        SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(pixels.width, pixels.height)) &&
        SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA &&
        SUCCEEDED(frame->WriteSource(converter.Get(), nullptr)) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

std::shared_ptr<Pixels> DecodeCover(const fs::path& file, UINT desired) {
    auto pixels = DecodeImage(file, desired, true);
    if (pixels && (pixels->artwork.left >= pixels->artwork.right || pixels->artwork.top >= pixels->artwork.bottom)) {
        return {};
    }
    return pixels;
}

D2D1_RECT_F CropFrame(const Pixels& pixels, D2D1_SIZE_F viewport, Crop crop) {
    const auto art = pixels.artwork;
    const float width = art.right - art.left, height = art.bottom - art.top;
    if (width <= 0.f || height <= 0.f || viewport.width <= 0.f || viewport.height <= 0.f) return {};
    const float scale = std::max(viewport.width / width, viewport.height / height) * crop.zoom;
    const float halfW = std::min(width, viewport.width / scale) / 2.f;
    const float halfH = std::min(height, viewport.height / scale) / 2.f;
    const float x = art.left + std::clamp(crop.x * width, halfW, width - halfW);
    const float y = art.top + std::clamp(crop.y * height, halfH, height - halfH);
    return D2D1::RectF(x - halfW, y - halfH, x + halfW, y + halfH);
}

void QueueIcon(HWND window, UINT message, std::wstring key, fs::path shortcut, fs::path image, UINT desired, bool cover) {
    if (!queue) {
        queue = std::make_shared<IconQueue>();
        std::thread(IconLoop, queue).detach();
    }
    {
        std::lock_guard lock(queue->mutex);
        queue->requests.push_back({window, message, std::move(key), std::move(shortcut), std::move(image), desired, cover});
    }
    queue->wake.notify_one();
}

void StopIconWorker() {
    if (!queue) return;
    {
        std::lock_guard lock(queue->mutex);
        queue->stopped = true;
        queue->requests.clear();
    }
    queue->wake.notify_one();
    queue.reset();
}

void PauseIconWorker(bool paused) {
    if (!queue) return;
    {
        std::lock_guard lock(queue->mutex);
        queue->paused = paused;
    }
    if (!paused) queue->wake.notify_one();
}

bool Canvas::Init(HWND window, float dpi) {
    if (!factory && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return false;
    if (!writer) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(writer.GetAddressOf())))) return false;
        auto make = [&](float size, DWRITE_FONT_WEIGHT weight, ComPtr<IDWriteTextFormat>& format) {
            const auto status = writer->CreateTextFormat(L"Segoe UI Variable Text", nullptr, weight,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, UiLocale(), &format);
            if (FAILED(status)) return false;
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ComPtr<IDWriteInlineObject> ellipsis;
            if (SUCCEEDED(writer->CreateEllipsisTrimmingSign(format.Get(), &ellipsis))) {
                const DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                format->SetTrimming(&trim, ellipsis.Get());
            }
            return true;
        };
        if (!make(14.f, DWRITE_FONT_WEIGHT_NORMAL, body) || !make(14.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, semibold) ||
            !make(21.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, heading) || !make(12.f, DWRITE_FONT_WEIGHT_NORMAL, caption)) {
            writer.Reset();
            return false;
        }
    }
    if (target) return true;
    RECT rectangle{};
    GetClientRect(window, &rectangle);
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    if (FAILED(factory->CreateHwndRenderTarget(properties, D2D1::HwndRenderTargetProperties(window,
        D2D1::SizeU(std::max(1L, rectangle.right), std::max(1L, rectangle.bottom))), &target))) return false;
    if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0), &brush))) {
        Drop();
        return false;
    }
    return true;
}

void Canvas::Drop() {
    glows.clear();
    coverLayer.Reset();
    menuLayer.Reset();
    grain.Reset();
    brush.Reset();
    target.Reset();
}

void Canvas::Size(HWND window, float dpi) {
    if (!target) return;
    RECT rectangle{};
    GetClientRect(window, &rectangle);
    if (rectangle.right > 0 && rectangle.bottom > 0) {
        target->SetDpi(dpi, dpi);
        target->Resize(D2D1::SizeU(rectangle.right, rectangle.bottom));
    }
}

void Canvas::Fill(D2D1_RECT_F box, D2D1_COLOR_F color, float radius) {
    brush->SetColor(color);
    if (radius > 0) target->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), brush.Get());
    else target->FillRectangle(box, brush.Get());
}

void Canvas::Stroke(D2D1_RECT_F box, D2D1_COLOR_F color, float radius, float weight) {
    brush->SetColor(color);
    if (radius > 0) target->DrawRoundedRectangle(D2D1::RoundedRect(box, radius, radius), brush.Get(), weight);
    else target->DrawRectangle(box, brush.Get(), weight);
}

void Canvas::Line(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float width) {
    brush->SetColor(color);
    target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush.Get(), width);
}

void Canvas::Circle(float x, float y, float radius, D2D1_COLOR_F color, float width) {
    brush->SetColor(color);
    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), brush.Get(), width);
}

void Canvas::Shadow(D2D1_RECT_F box, float radius, float opacity) {
    for (int spread = 6; spread >= 1; --spread) {
        const float size = static_cast<float>(spread);
        Fill(D2D1::RectF(box.left - size, box.top + 2.f - size, box.right + size, box.bottom + 2.f + size),
             D2D1::ColorF(0, opacity * (7 - spread) / 21.f), radius + size);
    }
}

void Canvas::Glow(D2D1_POINT_2F position, float rx, float ry, D2D1_COLOR_F color, D2D1_RECT_F bounds,
                  bool ambient) {
    if (color.a < .002f) return;
    const auto r = static_cast<unsigned>(std::clamp(std::lround(color.r * 255.f), 0L, 255L));
    const auto g = static_cast<unsigned>(std::clamp(std::lround(color.g * 255.f), 0L, 255L));
    const auto b = static_cast<unsigned>(std::clamp(std::lround(color.b * 255.f), 0L, 255L));
    const unsigned strength = ambient ? static_cast<unsigned>(std::lround(color.a * 254.f)) : 0u;
    const unsigned key = ((ambient ? strength + 1 : 0u) << 24) | (r << 16) | (g << 8) | b;
    auto texture = glows.find(key);
    if (texture == glows.end()) {
        // Ordered dithering distributes quantization steps across the cached texture.
        constexpr int dither[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        const unsigned size = ambient ? 384u : 128u;
        const float opacity = ambient ? strength / 254.f : 1.f;
        Pixels pixels;
        pixels.width = pixels.height = size;
        pixels.bytes.resize(size * size * 4);
        for (unsigned y = 0; y < size; ++y) {
            const float dy = (2.f * y + 1.f) / size - 1.f;
            for (unsigned x = 0; x < size; ++x) {
                const float dx = (2.f * x + 1.f) / size - 1.f;
                const float w = std::max(0.f, 1.f - dx * dx - dy * dy);
                const float a = w * w * w * opacity;
                const float threshold = (dither[y % 4][x % 4] + .5f) / 16.f;
                BYTE* pixel = &pixels.bytes[(y * size + x) * 4];
                pixel[0] = static_cast<BYTE>(b * a + threshold);
                pixel[1] = static_cast<BYTE>(g * a + threshold);
                pixel[2] = static_cast<BYTE>(r * a + threshold);
                pixel[3] = static_cast<BYTE>(255 * a + threshold);
            }
        }
        auto bitmap = Bitmap(pixels);
        if (!bitmap) return;
        if (glows.size() >= 16) {
            const auto oldest = std::min_element(glows.begin(), glows.end(), [](const auto& a, const auto& b) {
                return a.second.lastUsed < b.second.lastUsed;
            });
            glows.erase(oldest);
        }
        texture = glows.emplace(key, GlowTexture{std::move(bitmap), 0}).first;
    }
    texture->second.lastUsed = ++glowSequence;
    target->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->DrawBitmap(texture->second.image.Get(), D2D1::RectF(position.x - rx, position.y - ry,
        position.x + rx, position.y + ry), ambient ? 1.f : color.a, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    target->PopAxisAlignedClip();
}

void Canvas::AmbientGrain(D2D1_RECT_F bounds, float dpi, float strength) {
    if (!grain) {
        constexpr UINT Size = 128;
        Pixels pixels;
        pixels.width = pixels.height = Size;
        pixels.bytes.resize(Size * Size * 4);
        // Keep the dither fixed to physical pixels while the light moves underneath.
        uint32_t random = 0x596E4C3D;
        for (size_t i = 0; i < pixels.bytes.size(); i += 4) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            const BYTE alpha = static_cast<BYTE>((random >> 1) & 3);
            const BYTE tint = (random & 1) ? alpha : 0;
            pixels.bytes[i] = pixels.bytes[i + 1] = pixels.bytes[i + 2] = tint;
            pixels.bytes[i + 3] = alpha;
        }
        const auto bitmap = Bitmap(pixels);
        if (!bitmap || FAILED(target->CreateBitmapBrush(bitmap.Get(), D2D1::BitmapBrushProperties(
            D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR), &grain))) return;
    }
    grain->SetTransform(D2D1::Matrix3x2F::Scale(96.f / dpi, 96.f / dpi));
    grain->SetOpacity(strength);
    target->FillRectangle(bounds, grain.Get());
}

void Canvas::Text(std::wstring_view value, D2D1_RECT_F box, D2D1_COLOR_F color,
                  IDWriteTextFormat* style, bool center) {
    if (value.empty()) return;
    if (!style) style = body.Get();
    style->SetTextAlignment(center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    brush->SetColor(color);
    target->DrawText(value.data(), static_cast<UINT32>(value.size()), style, box, brush.Get(),
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

ComPtr<ID2D1Bitmap> Canvas::Bitmap(const Pixels& pixels) {
    ComPtr<ID2D1Bitmap> bitmap;
    target->CreateBitmap(D2D1::SizeU(pixels.width, pixels.height), pixels.bytes.data(), pixels.width * 4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96), &bitmap);
    return bitmap;
}

bool Canvas::TileImage(ID2D1Bitmap* image, const Pixels& pixels, D2D1_RECT_F picture, D2D1_RECT_F tile,
                       D2D1_COLOR_F background, ID2D1Bitmap* backdrop, std::optional<Crop> crop, float opacity) {
    const auto viewport = D2D1::SizeF(picture.right - picture.left, picture.bottom - picture.top);
    const auto source = crop ? CropFrame(pixels, viewport, *crop) : pixels.artwork;
    if (!image || source.left >= source.right || source.top >= source.bottom) return false;
    const auto size = D2D1::SizeF(tile.right - tile.left, tile.bottom - tile.top);
    if (!coverMask || std::abs(size.width - coverSize.width) > .1f ||
        std::abs(size.height - coverSize.height) > .1f) {
        coverMask.Reset();
        if (FAILED(factory->CreateRoundedRectangleGeometry(
            D2D1::RoundedRect(D2D1::RectF(0, 0, size.width, size.height), 11, 11), &coverMask))) return false;
        coverSize = size;
    }
    if (!coverLayer && FAILED(target->CreateLayer(&coverLayer))) return false;
    auto destination = picture;
    if (!crop) {
        const float sourceWidth = source.right - source.left, sourceHeight = source.bottom - source.top;
        // Keep transparent emblems clear of the tile edges even when their source has no padding.
        const float imageFill = pixels.topEdge.a > 0.f ? 1.f : .78f;
        const float scale = imageFill * std::min(viewport.width / sourceWidth, viewport.height / sourceHeight);
        const float insetX = (viewport.width - sourceWidth * scale) / 2.f;
        const float insetY = (viewport.height - sourceHeight * scale) / 2.f;
        destination = D2D1::RectF(picture.left + insetX, picture.top + insetY,
                                 picture.right - insetX, picture.bottom - insetY);
    }
    target->PushLayer(D2D1::LayerParameters(tile, coverMask.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                      D2D1::Matrix3x2F::Translation(tile.left, tile.top), opacity), coverLayer.Get());
    if (backdrop) target->DrawBitmap(backdrop, picture, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    else Fill(picture, background);
    target->DrawBitmap(image, destination, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
    target->PopLayer();
    return true;
}

ComPtr<ID2D1Bitmap> Canvas::TileBackdrop(const Pixels& pixels) {
    if (!pixels.topEdge.a) return {};
    constexpr UINT height = 128;
    const auto top = pixels.topEdge, bottom = pixels.bottomEdge;
    Pixels gradient;
    gradient.width = 1;
    gradient.height = height;
    gradient.bytes.resize(static_cast<size_t>(height) * 4);
    auto channel = [](float a, float b, float x) {
        return static_cast<BYTE>(std::clamp(std::lround(std::lerp(a, b, x) * 255.f), 0L, 255L));
    };
    for (UINT row = 0; row < height; ++row) {
        const float x = static_cast<float>(row) / (height - 1);
        const float blend = x * x * (3.f - 2.f * x);
        auto* p = &gradient.bytes[static_cast<size_t>(row) * 4];
        p[0] = channel(top.b, bottom.b, blend);
        p[1] = channel(top.g, bottom.g, blend);
        p[2] = channel(top.r, bottom.r, blend);
        p[3] = 255;
    }
    return Bitmap(gradient);
}
