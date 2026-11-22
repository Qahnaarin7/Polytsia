#include "app.hpp"
#include <algorithm>
#include <cmath>

namespace {
D2D1_COLOR_F Opacity(D2D1_COLOR_F color, float alpha) { color.a = alpha; return color; }
D2D1_COLOR_F Blend(D2D1_COLOR_F from, D2D1_COLOR_F to, float amount) {
    return D2D1::ColorF(std::lerp(from.r, to.r, amount), std::lerp(from.g, to.g, amount),
                       std::lerp(from.b, to.b, amount), std::lerp(from.a, to.a, amount));
}

std::array<D2D1_COLOR_F, 2> AmbientLights(const ColorScheme& colors, float brightness, bool darkTheme) {
    return {D2D1::ColorF(colors.nearGlow, std::min(.98f, brightness * (darkTheme ? .87f : .51f))),
            D2D1::ColorF(colors.farGlow, std::min(.94f, brightness * (darkTheme ? .72f : .46f)))};
}
constexpr float Margin = 24.f;
constexpr float FullTurn = 6.283185307f;

D2D1_RECT_F Inset(D2D1_RECT_F box, float amount) {
    return D2D1::RectF(box.left + amount, box.top + amount, box.right - amount, box.bottom - amount);
}

D2D1_RECT_F VisiblePart(D2D1_RECT_F box, D2D1_RECT_F clip) {
    return D2D1::RectF(std::max(box.left, clip.left), std::max(box.top, clip.top),
                      std::min(box.right, clip.right), std::min(box.bottom, clip.bottom));
}
}

D2D1_RECT_F App::SearchBox() const {
    return D2D1::RectF(Margin, 19, width - Margin - 52, 59);
}

D2D1_RECT_F App::GridBox(int position) const {
    const float left = gridLeft + position % columns * (tileWidth + TileGap());
    const float top = GridTop() + position / columns * RowHeight() - scroll;
    return D2D1::RectF(left, top, left + tileWidth, top + TileHeight());
}

D2D1_RECT_F App::Popup() const {
    const float w = std::min(width - 48.f, dialog == Dialog::Cover ? 440.f : 408.f);
    const float h = dialog == Dialog::Cover ? 416.f : dialog == Dialog::Edit ? 370.f : 216.f;
    const float left = (width - w) / 2.f;
    const float p = dialogSince ? std::clamp((GetTickCount64() - dialogSince) / 160.f, 0.f, 1.f) : 1.f;
    const float top = (height - h) / 2.f + 7.f * (1.f - p) * (1.f - p);
    return D2D1::RectF(left, top, left + w, top + h);
}

D2D1_RECT_F App::MenuBox() const {
    const auto anchor = contextMenu->anchor;
    const float x = ToDip(anchor.x), y = ContentY(anchor.y);
    const float left = std::clamp(x + MenuWidth <= width - 8.f ? x : x - MenuWidth,
                                  8.f, std::max(8.f, width - MenuWidth - 8.f));
    float menuHeight = 2 * MenuPadding;
    for (const auto& item : MenuItems()) menuHeight += MenuRow + (item.separator ? MenuGap : 0.f);
    const float top = std::clamp(y + menuHeight <= height - 8.f ? y : y - menuHeight,
                                 8.f, std::max(8.f, height - menuHeight - 8.f));
    const float progress = contextMenu->opened
        ? std::min(1.f, static_cast<float>(GetTickCount64() - contextMenu->opened) / MenuDuration) : 1.f;
    const float offset = 5.f * (1.f - progress) * (1.f - progress);
    return D2D1::RectF(left, top - offset, left + MenuWidth, top + menuHeight - offset);
}

D2D1_RECT_F App::MenuItemBox(int id) const {
    const auto menu = MenuBox();
    float top = menu.top + MenuPadding;
    for (const auto& item : MenuItems()) {
        if (item.separator) top += MenuGap;
        if (item.id == id) break;
        top += MenuRow;
    }
    return D2D1::RectF(menu.left + MenuPadding, top, menu.right - MenuPadding, top + MenuRow);
}

D2D1_RECT_F App::CoverViewport() const {
    const auto panel = Popup();
    const float caption = library.prefs.labels == TileLabels::Always ? TileCaptionHeight : 0.f;
    const float size = std::min(panel.right - panel.left - 48.f, 198.f - caption);
    const float x = (panel.left + panel.right - size) / 2.f;
    const float y = panel.top + 56.f + (198.f - size - caption) / 2.f;
    return D2D1::RectF(x, y, x + size, y + size);
}

D2D1_RECT_F App::AmbientTrack() const {
    const float right = width - SettingsEdge();
    const float y = SettingsPosition(AmbientPosition()) - scroll + SettingsLayout::RowHeight / 2.f;
    return D2D1::RectF(right - 196.f, y - 2.f, right - 67.f, y + 2.f);
}

void App::Paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    if (dc && !minimized && canvas.Init(window, dpi)) {
        canvas.target->BeginDraw();
        canvas.target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        DrawRoot();
        if (canvas.target->EndDraw() == D2DERR_RECREATE_TARGET) {
            for (auto& [key, icon] : icons) {
                icon.image.Reset();
                icon.backdrop.Reset();
            }
            previewBitmap.Reset();
            cropPreview.image.Reset();
            canvas.Drop();
            Invalidate();
        }
    } else if (dc) {
        FillRect(dc, &paint.rcPaint, GetSysColorBrush(COLOR_WINDOW));
    }
    EndPaint(window, &paint);
}

void App::DrawAmbient(D2D1_RECT_F full) {
    const float brightness = AmbientValue() / 100.f;
    const auto [nearLight, farLight] = AmbientLights(*scheme, brightness, dark);
    const float h = full.bottom;
    const auto glow = [&](float x, float y, float rx, float ry, D2D1_COLOR_F color) {
        canvas.Glow(D2D1::Point2F(width * x, h * y), width * rx, h * ry, color, full, true);
    };
    // With animations off the effects freeze at a pleasant frame instead of disappearing.
    const auto style = library.prefs.ambientStyle;
    const auto phaseOf = [&](ULONGLONG period) {
        return style == AmbientStyle::Still ? 0.f : static_cast<float>(AmbientTime() % period) * (FullTurn / period);
    };
    switch (style) {
    case AmbientStyle::Aurora: {
        // Wide, flat ribbons of light drifting across the top of the window.
        const float phase = phaseOf(14000);
        for (int band = 0; band < 3; ++band) {
            const float offset = band * 2.1f;
            const float x = .2f + band * .3f + .08f * std::sin(phase + offset);
            const float y = .16f + .07f * band + .06f * std::sin(phase * 2.f + offset);
            glow(x, y, .52f + .08f * std::cos(phase + offset), .34f, band % 2 ? farLight : nearLight);
        }
        glow(.5f, .95f, .8f, .38f, Opacity(farLight, farLight.a * .55f));
        break;
    }
    case AmbientStyle::Pulse: {
        // Two lights in opposite corners that slowly breathe in turn.
        const float phase = phaseOf(5200);
        const float a = .5f + .5f * std::sin(phase), b = .5f + .5f * std::sin(phase + FullTurn / 2.f);
        glow(.16f, .18f, .5f + .16f * a, .6f + .16f * a, nearLight);
        glow(.86f, .84f, .48f + .16f * b, .58f + .16f * b, farLight);
        break;
    }
    case AmbientStyle::Cursor: {
        // The near light eases toward the mouse pointer; the far light drifts opposite to it.
        const D2D1_POINT_2F target = activeWindow && pointer.x > 0 && pointer.y > 0
            ? D2D1::Point2F(std::clamp(ToDip(pointer.x) / std::max(1.f, width), 0.f, 1.f),
                            std::clamp(ToDip(pointer.y) / std::max(1.f, h), 0.f, 1.f))
            : D2D1::Point2F(.35f, .3f);
        if (ambientFocus.x < 0.f) ambientFocus = target;
        const float ease = effects ? .14f : 1.f;
        ambientFocus.x += (target.x - ambientFocus.x) * ease;
        ambientFocus.y += (target.y - ambientFocus.y) * ease;
        glow(ambientFocus.x, ambientFocus.y, .5f, .6f, nearLight);
        glow(1.f - ambientFocus.x * .6f, 1.f - ambientFocus.y * .6f, .62f, .7f, farLight);
        break;
    }
    case AmbientStyle::Drift:
    case AmbientStyle::Still:
    default: {
        const float phase = phaseOf(8200);
        const float sweep = std::cos(phase);
        glow(.31f + .24f * std::sin(phase), .23f + .20f * std::sin(phase - .57f), .69f + .06f * sweep, .78f, nearLight);
        glow(.74f - .21f * std::sin(phase + .35f), .72f + .22f * std::cos(phase + .35f), .64f - .06f * sweep, .72f, farLight);
        break;
    }
    }
}

void App::DrawRoot() {
    hits.clear();
    const auto full = D2D1::RectF(0, 0, width, height + TitleHeight);
    canvas.target->Clear(Opacity(theme.background, glass ? (dark ? .23f : .24f) : 1.f));
    if (!contrast && AmbientValue() > 0) {
        DrawAmbient(full);
        canvas.AmbientGrain(full, dpi, std::min(1.f, AmbientValue() / 50.f));
    }
    if (page != Page::Settings) DrawTileHalos(full);
    DrawTitle();
    canvas.target->SetTransform(D2D1::Matrix3x2F::Translation(0, TitleHeight));
    if (page == Page::Settings) DrawSettings();
    else DrawLibrary();
    if (dialog != Dialog::None) DrawDialog();
    if (contextMenu) DrawContextMenu();
    canvas.target->SetTransform(D2D1::Matrix3x2F::Identity());
}

D2D1_RECT_F App::TitleButton(int id) const {
    const float right = width - 6.f - (CaptionClose - id) * 46.f;
    return D2D1::RectF(right - 42.f, 4.f, right, TitleHeight - 4.f);
}

void App::DrawBrandMark(float left, float top, float size, D2D1_COLOR_F color) {
    auto tile = [&](float x1, float y1, float x2, float y2, float alpha) {
        canvas.Fill(D2D1::RectF(left + x1 * size, top + y1 * size, left + x2 * size, top + y2 * size),
                    Opacity(color, contrast ? 1.f : alpha), .062f * size);
    };
    tile(.13f, .13f, .44f, .54f, 1.f);
    tile(.13f, .63f, .44f, .87f, .64f);
    tile(.56f, .13f, .87f, .36f, .72f);
    tile(.56f, .45f, .87f, .87f, .90f);
}

void App::DrawTitle() {
    DrawBrandMark(11.f, 7.f, 22.f, theme.accent);
    for (int id = CaptionMinimize; id <= CaptionClose; ++id) {
        auto box = TitleButton(id);
        const float over = ControlAmount(hoverMotion, id, id == titleHover);
        const float push = ControlAmount(pressMotion, id, id == titlePress);
        if (effects) box = Inset(box, .8f * push);
        const bool close = id == CaptionClose;
        const auto ink = activeWindow ? theme.text : theme.muted;
        const float amount = std::max(over, push);
        if (amount > 0.f) {
            const auto glow = contrast ? theme.accent : close ? D2D1::ColorF(dark ? 0xAD343F : 0xAF2734, .92f)
                : Opacity(theme.text, (dark ? .13f : .10f) + .07f * push);
            canvas.Fill(box, Opacity(glow, glow.a * amount), 7);
        }
        const auto foreground = contrast || close ? Blend(ink, theme.primaryText, amount) : ink;
        const float x = (box.left + box.right) / 2.f, y = TitleHeight / 2.f;
        if (id == CaptionMinimize) canvas.Line(x - 5, y, x + 5, y, foreground, 1.3f);
        else if (id == CaptionMaximize && IsZoomed(window)) {
            canvas.Line(x - 1, y - 6, x + 6, y - 6, foreground, 1.3f);
            canvas.Line(x + 6, y - 6, x + 6, y + 1, foreground, 1.3f);
            canvas.Stroke(D2D1::RectF(x - 5, y - 3, x + 3, y + 5), foreground, 1.f, 1.3f);
        } else if (id == CaptionMaximize) {
            canvas.Stroke(D2D1::RectF(x - 4.5f, y - 4.5f, x + 4.5f, y + 4.5f), foreground, 1.2f, 1.3f);
        } else Glyph(CancelEdit, x, y, foreground);
    }
}

void App::Glyph(int kind, float x, float y, D2D1_COLOR_F color) {
    if (kind == Add || kind == AddMenu || kind == ZoomIn || kind == ZoomOut) {
        canvas.Line(x - 6, y, x + 6, y, color);
        if (kind != ZoomOut) canvas.Line(x, y - 6, x, y + 6, color);
    } else if (kind == Back) {
        canvas.Line(x - 7, y, x + 6, y, color);
        canvas.Line(x - 7, y, x - 1, y - 6, color);
        canvas.Line(x - 7, y, x - 1, y + 6, color);
    } else if (kind == SearchMenu) {
        canvas.Circle(x - 1.5f, y - 1.5f, 5.4f, color);
        canvas.Line(x + 2.6f, y + 2.7f, x + 7, y + 7, color);
    } else if (kind == Settings || kind == SettingsMenu) {
        canvas.Circle(x, y, 3.f, color);
        D2D1_POINT_2F last{};
        for (int i = 0; i <= 32; ++i) {
            const float theta = i * .19634954f;
            const float radius = i % 4 == 0 || i % 4 == 3 ? 7.f : 9.f;
            const auto p = D2D1::Point2F(x + std::sin(theta) * radius, y + std::cos(theta) * radius);
            if (i) canvas.Line(last.x, last.y, p.x, p.y, color, 1.2f);
            last = p;
        }
    } else if (kind == CancelEdit || kind == Search) {
        canvas.Line(x - 5, y - 5, x + 5, y + 5, color);
        canvas.Line(x + 5, y - 5, x - 5, y + 5, color);
    } else if (kind == LaunchMenu) {
        canvas.Line(x - 4, y - 6, x + 6, y, color);
        canvas.Line(x + 6, y, x - 4, y + 6, color);
        canvas.Line(x - 4, y + 6, x - 4, y - 6, color);
    } else if (kind == EditMenu) {
        canvas.Line(x - 6, y + 5, x + 4, y - 5, color);
        canvas.Line(x + 4, y - 5, x + 6, y - 3, color);
        canvas.Line(x + 6, y - 3, x - 2, y + 5, color);
        canvas.Line(x - 6, y + 5, x - 2, y + 5, color);
    } else if (kind == ReplaceMenu) {
        canvas.Line(x - 6, y - 3, x + 6, y - 3, color);
        canvas.Line(x + 3, y - 6, x + 6, y - 3, color);
        canvas.Line(x + 6, y + 3, x - 6, y + 3, color);
        canvas.Line(x - 3, y + 6, x - 6, y + 3, color);
    } else if (kind == EarlierMenu || kind == LaterMenu) {
        const float sign = kind == EarlierMenu ? 1.f : -1.f;
        canvas.Line(x, y + sign * 6.f, x, y - sign * 6.f, color);
        canvas.Line(x, y - sign * 6.f, x - 4.f, y - sign * 2.f, color);
        canvas.Line(x, y - sign * 6.f, x + 4.f, y - sign * 2.f, color);
    } else if (kind == AdminMenu) {
        // Shield outline.
        canvas.Line(x, y - 7, x + 6, y - 4.5f, color, 1.3f);
        canvas.Line(x + 6, y - 4.5f, x + 5, y + 2, color, 1.3f);
        canvas.Line(x + 5, y + 2, x, y + 7, color, 1.3f);
        canvas.Line(x, y + 7, x - 5, y + 2, color, 1.3f);
        canvas.Line(x - 5, y + 2, x - 6, y - 4.5f, color, 1.3f);
        canvas.Line(x - 6, y - 4.5f, x, y - 7, color, 1.3f);
    } else if (kind == LocationMenu) {
        // Folder.
        canvas.Line(x - 7, y - 5, x - 2, y - 5, color, 1.3f);
        canvas.Line(x - 2, y - 5, x, y - 3, color, 1.3f);
        canvas.Stroke(D2D1::RectF(x - 7, y - 3, x + 7, y + 6), color, 1.5f, 1.3f);
    } else if (kind == SortMenu) {
        // Bars of decreasing length.
        canvas.Line(x - 7, y - 5, x + 7, y - 5, color);
        canvas.Line(x - 7, y, x + 3, y, color);
        canvas.Line(x - 7, y + 5, x - 1, y + 5, color);
    } else if (kind == DeleteMenuItem) {
        canvas.Line(x - 7, y - 4, x + 7, y - 4, color);
        canvas.Line(x - 2, y - 7, x + 2, y - 7, color);
        canvas.Stroke(D2D1::RectF(x - 5, y - 4, x + 5, y + 7), color, 2, 1.3f);
    }
}

void App::DrawButton(int id, D2D1_RECT_F rectangle, const wchar_t* text, bool primary) {
    hits.push_back({id, rectangle});
    const float over = ControlAmount(hoverMotion, id, id == hover);
    const float push = ControlAmount(pressMotion, id, id == pressed);
    if (effects) rectangle = Inset(rectangle, .8f * push);
    auto fill = primary ? theme.primary : theme.button;
    if (!contrast) {
        if (primary) {
            const float shift = 1.f + .12f * over - .22f * push;
            fill = D2D1::ColorF(std::min(1.f, fill.r * shift), std::min(1.f, fill.g * shift),
                               std::min(1.f, fill.b * shift));
        } else {
            const auto lit = dark ? D2D1::ColorF(0xFFFFFF, .15f + .03f * push)
                                  : D2D1::ColorF(scheme->tileTint, .62f + .23f * push);
            fill = Blend(fill, lit, std::max(over, push));
        }
    }
    canvas.Fill(rectangle, fill, 8);
    canvas.Stroke(rectangle, theme.line, 8);
    if (id == Search && searchExpanded) canvas.Stroke(rectangle, theme.accent, 8, 1.4f);
    if (focused == id && keyboard) canvas.Stroke(Inset(rectangle, 2), primary ? theme.primaryText : theme.accent, 6, 2);
    const auto fg = primary ? theme.primaryText : theme.text;
    if (!text || !*text) {
        Glyph(id, (rectangle.left + rectangle.right) / 2, (rectangle.top + rectangle.bottom) / 2, fg);
    } else {
        canvas.Text(text, Inset(rectangle, 5), fg, canvas.semibold.Get(), true);
    }
}

void App::DrawPaletteSwatch(D2D1_RECT_F box, const ColorScheme& option) {
    canvas.Fill(box, contrast ? theme.field : D2D1::ColorF(option.background), 5);
    const float centerX = (box.left + box.right) / 2.f, centerY = (box.top + box.bottom) / 2.f;
    const float size = std::min(7.f, (box.right - box.left - 8.f) / 3.f);
    const float pitch = size + 1.5f;
    const unsigned colors[] = {option.nearGlow, option.accent, option.farGlow};
    for (int i = 0; i < 3; ++i) {
        const float x = centerX + (i - 1) * pitch;
        canvas.Fill(D2D1::RectF(x - size / 2.f, centerY - size / 2.f, x + size / 2.f, centerY + size / 2.f),
                    contrast ? theme.text : D2D1::ColorF(colors[i]), 1.5f);
    }
    canvas.Stroke(Inset(box, .5f), contrast ? theme.text : theme.line, 5);
}

void App::DrawPaletteButton(int id, D2D1_RECT_F box, const ColorScheme& option, bool chosen) {
    hits.push_back({id, box});
    const auto fill = !contrast && (id == hover || id == pressed) ? Opacity(theme.text, .13f) : theme.button;
    canvas.Fill(box, fill, 8);
    canvas.Stroke(box, chosen ? Opacity(theme.text, .85f) : theme.line, 8, chosen ? 1.6f : 1.f);
    if (focused == id && keyboard) canvas.Stroke(Inset(box, 3), theme.accent, 5, 1.4f);
    const float x = box.left + 9.f, y = (box.top + box.bottom) / 2.f;
    DrawPaletteSwatch(D2D1::RectF(x, y - 9.f, x + 29.f, y + 9.f), option);
    canvas.Text(option.name.Get(), D2D1::RectF(box.left + 42, box.top, box.right - 8, box.bottom),
                theme.text, chosen ? canvas.semibold.Get() : canvas.body.Get());
}

void App::DrawTileHalos(D2D1_RECT_F windowBounds) {
    if (!library.prefs.tileHalo || contrast || dragging || !activeWindow || dialog != Dialog::None ||
        contextMenu || AmbientValue() <= 0 || (hotId.empty() && transitions.empty())) return;
    const float gridTop = GridTop() - 3.f, gridBottom = height - 20.f;
    const float tileHeight = TileHeight();
    const float baseSpread = std::clamp(std::min(tileWidth, tileHeight) * .43f, 32.f, 92.f);
    constexpr ULONGLONG pulsePeriod = 2100;
    const float pulse = effects ? .5f + .5f * std::sin(static_cast<float>(AmbientTime() % pulsePeriod) *
                                                      (FullTurn / pulsePeriod)) : .5f;
    const float brightness = std::min(1.f, AmbientValue() / 100.f * (dark ? 1.22f : 1.06f))
        * (.79f + .21f * pulse);
    const int start = static_cast<int>(scroll / RowHeight()) * columns;
    for (int i = start; i < static_cast<int>(visible.size()); ++i) {
        const auto box = GridBox(i);
        if (box.top >= gridBottom) break;
        const auto& entry = library.items[visible[i]];
        const float amount = TileHover(entry);
        if (amount <= .01f) continue;
        const float shownTop = std::max(box.top, gridTop), shownBottom = std::min(box.bottom, gridBottom);
        if (shownBottom <= shownTop) continue;
        const float visibleHeight = shownBottom - shownTop;
        auto* image = TileCover(entry);
        if (!image || !image->pixels) image = Icon(entry);
        const auto color = image && image->pixels ? image->pixels->tint : theme.accent;
        const float spread = baseSpread * (.82f + .37f * pulse) * (.66f + .34f * amount);
        // Put the glow in the window background so title and search controls can be drawn over it.
        const auto center = D2D1::Point2F((box.left + box.right) / 2.f, TitleHeight + (shownTop + shownBottom) / 2.f);
        canvas.Glow(center, tileWidth / 2.f + spread, visibleHeight / 2.f + spread,
                    Opacity(color, amount * brightness * visibleHeight / tileHeight), windowBounds);
    }
}

void App::DrawScrollArrows(D2D1_RECT_F viewport, float left) {
    if (maxScroll <= 0 || viewport.bottom - viewport.top < 45.f) return;
    auto draw = [&](int id, float y, float direction) {
        const auto box = D2D1::RectF(left, y, left + 20.f, y + 23.f);
        hits.push_back({id, box});
        const float amount = ControlAmount(hoverMotion, id, hover == id);
        const auto ink = Blend(theme.muted, theme.text, amount);
        const float x = left + 10.f, mid = y + 11.5f;
        canvas.Line(x - 4.f, mid - 2.f * direction, x, mid + 2.f * direction, ink, 1.4f);
        canvas.Line(x, mid + 2.f * direction, x + 4.f, mid - 2.f * direction, ink, 1.4f);
    };
    if (scroll > .5f) draw(ScrollUp, viewport.top, -1.f);
    if (scroll < maxScroll - .5f) draw(ScrollDown, viewport.bottom - 23.f, 1.f);
}

void App::DrawLibrary() {
    if (searchExpanded) {
        const auto bar = SearchBox();
        canvas.Fill(bar, theme.field, 8);
        canvas.Stroke(bar, GetFocus() == search && dialog == Dialog::None ? theme.accent : theme.line, 8,
                      GetFocus() == search && dialog == Dialog::None ? 1.5f : 1.f);
        if (dialog != Dialog::None || contextMenu) {
            canvas.Text(query.empty() ? Texts::FindShortcut.Get() : query, Inset(bar, 11),
                        query.empty() ? theme.muted : theme.text);
        }
        DrawButton(Search, D2D1::RectF(width - Margin - 40, 19, width - Margin, 59), L"");
    }

    if (visible.empty()) {
        const float cy = std::max(180.f, (height + GridTop()) / 2.f - 23.f);
        const float x = width / 2;
        const auto badge = D2D1::RectF(x - 22, cy - 72, x + 22, cy - 28);
        canvas.Fill(badge, theme.button, 12);
        canvas.Stroke(badge, theme.line, 12);
        DrawBrandMark(x - 16.f, cy - 66.f, 32.f, theme.accent);
        canvas.Text(query.empty() ? Texts::EmptyLibrary.Get() : Texts::NoMatches.Get(),
                    D2D1::RectF(Margin, cy - 18, width - Margin, cy + 18),
                    theme.text, canvas.heading.Get(), true);
        canvas.Text(query.empty() ? Texts::EmptyLibraryHint.Get() : Texts::SearchHint.Get(),
                    D2D1::RectF(Margin, cy + 19, width - Margin, cy + 49), theme.muted, nullptr, true);
        return;
    }
    const auto clip = D2D1::RectF(Margin - 2, GridTop() - 3, width - Margin + 2, height - 20);
    const int start = static_cast<int>(scroll / RowHeight()) * columns;
    canvas.target->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (int i = start; i < static_cast<int>(visible.size()); ++i) {
        const auto box = GridBox(i);
        if (box.top >= clip.bottom) break;
        hits.push_back({Tile + i, VisiblePart(box, clip)});
        DrawTile(Tile + i, box);
    }
    if (dragging) DrawDragPreview();
    canvas.target->PopAxisAlignedClip();
    DrawScrollArrows(clip, width - Margin + 2.f);
}

ID2D1Bitmap* App::IconBitmap(IconCache* icon) {
    if (!icon || !icon->pixels) return nullptr;
    if (!icon->image) icon->image = canvas.Bitmap(*icon->pixels);
    return icon->image.Get();
}

void App::DrawIcon(IconCache* icon, D2D1_RECT_F rectangle, float opacity) {
    if (auto* bitmap = IconBitmap(icon)) {
        const auto dimensions = bitmap->GetSize();
        const float scale = std::min((rectangle.right - rectangle.left) / dimensions.width,
                                     (rectangle.bottom - rectangle.top) / dimensions.height);
        const float cx = (rectangle.left + rectangle.right) / 2, cy = (rectangle.top + rectangle.bottom) / 2;
        const float rx = dimensions.width * scale / 2, ry = dimensions.height * scale / 2;
        canvas.target->DrawBitmap(bitmap, D2D1::RectF(cx - rx, cy - ry, cx + rx, cy + ry),
                                  opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        return;
    }
    canvas.Fill(rectangle, contrast ? theme.field : D2D1::ColorF(scheme->tileTint, dark ? .72f : .80f), 10);
    const float x = (rectangle.left + rectangle.right) / 2, y = (rectangle.top + rectangle.bottom) / 2;
    canvas.Stroke(D2D1::RectF(x - 8, y - 9, x + 8, y + 9), theme.muted, 2, 1.4f);
    canvas.Line(x - 3, y + 4, x + 5, y - 4, theme.muted);
    canvas.Line(x + 1, y - 4, x + 5, y - 4, theme.muted);
    canvas.Line(x + 5, y - 4, x + 5, y, theme.muted);
}

float App::TileImageOpacity(const IconCache* icon) const {
    if (!tileImageFadeUntil || !icon || !icon->appeared) return 1.f;
    const float t = std::min(1.f, static_cast<float>(GetTickCount64() - icon->appeared) / TileImageFadeDuration);
    return t * (2.f - t);
}

float App::TileHover(const Shortcut& entry) const {
    if (const auto motion = transitions.find(entry.id); motion != transitions.end()) {
        return motion->second.Value(GetTickCount64());
    }
    return entry.id == hotId ? 1.f : 0.f;
}

IconCache* App::TileCover(const Shortcut& entry) {
    if (entry.cover.empty() || library.prefs.tileStyle != TileStyle::Cover) return nullptr;
    return CachedIcon(CoverKey(entry));
}

bool App::DrawTilePicture(const Shortcut& entry, IconCache* displayed, bool cover, D2D1_RECT_F picture,
                          D2D1_RECT_F tile, const Palette& colors, bool darkTheme, float opacity) {
    if (!displayed || !displayed->pixels) return false;
    auto background = contrast ? colors.background : colors.dialog;
    if (!contrast && !cover) {
        if (displayed->pixels->topEdge.a == 0.f) {
            background = Blend(background, displayed->pixels->tint, darkTheme ? .10f : .07f);
        } else if (!displayed->backdrop) {
            displayed->backdrop = canvas.TileBackdrop(*displayed->pixels);
        }
    }
    return canvas.TileImage(IconBitmap(displayed), *displayed->pixels, picture, tile, background,
                            !contrast && !cover ? displayed->backdrop.Get() : nullptr,
                            cover ? std::optional{entry.crop} : std::nullopt, opacity);
}

void App::DrawTile(int id, D2D1_RECT_F box) {
    const auto& entry = library.items[visible[id - Tile]];
    auto* image = Icon(entry);
    auto* cover = TileCover(entry);
    auto* art = cover && cover->pixels ? cover : nullptr;
    const float amount = TileHover(entry);
    const float push = ControlAmount(pressMotion, id, pressed == id);
    auto* displayed = art ? art : image;
    const auto highlight = displayed && displayed->pixels ? displayed->pixels->tint : theme.accent;
    const float mid = (box.left + box.right) / 2.f;
    const auto style = library.prefs.tileStyle;
    const bool compact = style == TileStyle::Compact;
    const float scale = TileScale();
    const auto labels = library.prefs.labels;
    const bool always = labels == TileLabels::Always;
    const float reveal = labels == TileLabels::Hover ? amount : 0.f;
    const float centerY = (box.top + box.bottom) / 2.f;
    auto revealLabel = [&] {
        if (reveal <= .01f) return;
        const float bottom = box.bottom - 6.f;
        const auto pill = D2D1::RectF(box.left + 6, bottom - 27, box.right - 6, bottom);
        canvas.Fill(pill, Opacity(theme.dialog, reveal * (contrast ? 1.f : .93f)), 6);
        canvas.Text(entry.name, Inset(pill, 3), Opacity(theme.text, reveal), canvas.semibold.Get(), true);
    };
    if (amount > .01f && !contrast) {
        if (dark && glass) canvas.Fill(box, D2D1::ColorF(scheme->tileTint, .47f + amount * .08f), 11);
        else canvas.Fill(box, Opacity(theme.tile, theme.tile.a + amount * (dark ? .055f : .19f)), 11);
        canvas.Glow(D2D1::Point2F(compact ? box.left + 29.f * scale : mid,
                                  compact ? centerY : box.top + std::min(55.f * scale, TileHeight() / 2.f)),
                    std::max(12.f, tileWidth * .43f), (compact ? 30.f : 53.f) * scale,
                    Opacity(highlight, amount * (dark ? .43f : .25f)), Inset(box, 9));
    } else canvas.Fill(box, dark && glass ? D2D1::ColorF(scheme->tileTint, .46f) : theme.tile, 11);
    if (style == TileStyle::Cover) {
        const auto picture = D2D1::RectF(box.left, box.top, box.right, box.bottom - (always ? TileCaptionHeight : 0.f));
        const bool pendingCover = !entry.cover.empty() && !cover;
        if (!pendingCover && displayed) {
            const bool drawn = DrawTilePicture(entry, displayed, art != nullptr, picture, box, theme, dark,
                                               TileImageOpacity(displayed));
            if (!drawn) {
                const float pictureCenterY = (picture.top + picture.bottom) / 2.f;
                DrawIcon(nullptr, D2D1::RectF(mid - 24.f, pictureCenterY - 24.f, mid + 24.f, pictureCenterY + 24.f));
            }
        }
        if (always) {
            canvas.Line(box.left + 1.f, picture.bottom, box.right - 1.f, picture.bottom, theme.line, 1.f);
            canvas.Text(entry.name, D2D1::RectF(box.left + 8, picture.bottom, box.right - 8, box.bottom),
                        theme.text, canvas.semibold.Get(), true);
        } else revealLabel();
    }
    if (compact) {
        const float half = 16.f * scale;
        const float left = labels == TileLabels::Hidden ? mid - half : box.left + 12.f;
        const float textLeft = left + 2.f * half + 9.f;
        if (image) {
            DrawIcon(image, D2D1::RectF(left, centerY - half, left + 2.f * half, centerY + half), TileImageOpacity(image));
        }
        if (always || (reveal > .01f && textLeft < box.right - 10.f)) {
            canvas.Text(entry.name, D2D1::RectF(textLeft, box.top, box.right - 10, box.bottom),
                        Opacity(theme.text, always ? 1.f : reveal));
        }
    } else if (style == TileStyle::Centered) {
        const float half = 24.f * scale;
        const float iconTop = always ? box.top + 28.f * scale :
            centerY - half - (labels == TileLabels::Hover ? 7.f : 0.f);
        if (image) {
            DrawIcon(image, D2D1::RectF(mid - half, iconTop, mid + half, iconTop + 2.f * half), TileImageOpacity(image));
        }
        if (always) {
            canvas.Text(entry.name, D2D1::RectF(box.left + 9, box.top + 81.f * scale, box.right - 9, box.bottom - 12),
                        theme.text, canvas.body.Get(), true);
        } else revealLabel();
    }
    if (push > .01f && !contrast && !dragging) {
        canvas.Fill(Inset(box, .5f), D2D1::ColorF(0, push * .16f), 10.5f);
    }
    canvas.Stroke(Inset(box, .5f), theme.line, 11);
    if (dragTarget == id || (keyboard && focused == id && dialog == Dialog::None)) {
        canvas.Stroke(Inset(box, 1), theme.accent, 10, 2);
    } else if (amount > .01f && !contrast) {
        canvas.Stroke(Inset(box, 1), Opacity(highlight, amount * (dark ? .56f : .47f)), 10, 1.2f);
    }
    if (dragging && id == pressed) {
        canvas.Fill(Inset(box, .5f), Opacity(theme.background, .53f), 11);
    } else if (dragging && id == dragTarget) {
        const float edge = id < pressed ? box.left + 3 : box.right - 3;
        canvas.Line(edge, box.top + 20, edge, box.bottom - 20, theme.accent, 3);
    }
}

void App::DrawDragPreview() {
    const auto position = static_cast<size_t>(pressed - Tile);
    if (position >= visible.size()) return;
    const auto& item = library.items[visible[position]];
    const float x = std::clamp(ToDip(pointer.x) + 14.f, Margin, width - Margin - 146.f);
    const float y = std::clamp(ContentY(pointer.y) + 15.f, GridTop(), height - 69.f);
    const auto box = D2D1::RectF(x, y, x + 146, y + 44);
    if (!contrast) canvas.Shadow(box, 9, dark ? .23f : .14f);
    canvas.Fill(box, theme.dialog, 9);
    canvas.Stroke(Inset(box, .5f), theme.accent, 9);
    DrawIcon(Icon(item), D2D1::RectF(x + 8, y + 8, x + 36, y + 36));
    canvas.Text(item.name, D2D1::RectF(x + 45, y + 3, x + 138, y + 41), theme.text);
}

void App::DrawThemePreview(D2D1_RECT_F panel) {
    const int first = browseDark ? FirstDarkPalette : FirstLightPalette;
    const int pointed = keyboard ? focused : hover;
    const int saved = browseDark ? library.prefs.darkPalette : library.prefs.lightPalette;
    const int index = pointed >= first && pointed < first + PaletteCount ? pointed - first : saved;
    const auto& colors = (browseDark ? DarkSchemes : LightSchemes)[index];
    const auto sample = MakePalette(colors, browseDark, contrast);
    const bool sideLabel = panel.right - panel.left >= 620.f;
    const auto mini = D2D1::RectF(panel.left + (sideLabel ? 192.f : 9.f), panel.top + 9.f,
                                 panel.right - 9.f, panel.bottom - 9.f);
    canvas.Fill(panel, contrast ? theme.background : Opacity(theme.dialog, dark ? .62f : .68f), 14);
    canvas.Stroke(Inset(panel, .5f), theme.line, 14);
    if (sideLabel) {
        const float center = (panel.top + panel.bottom) / 2.f, left = panel.left + 18.f;
        canvas.Text(Texts::Preview.Get(), D2D1::RectF(left, center - 40, mini.left - 12, center - 20),
                    theme.muted, canvas.caption.Get());
        canvas.Text(colors.name.Get(), D2D1::RectF(left, center - 20, mini.left - 12, center + 12),
                    theme.text, canvas.heading.Get());
        DrawPaletteSwatch(D2D1::RectF(left, center + 20, left + 39, center + 39), colors);
    }
    if (!contrast) canvas.Shadow(mini, 10, dark ? .22f : .11f);
    canvas.Fill(mini, sample.background, 10);
    if (!contrast) {
        const float w = mini.right - mini.left, h = mini.bottom - mini.top;
        const auto [nearLight, farLight] = AmbientLights(colors, AmbientValue() / 100.f, browseDark);
        canvas.Glow(D2D1::Point2F(mini.left + w * .28f, mini.top + h * .34f),
                    w * .28f, h * .34f, nearLight, mini);
        canvas.Glow(D2D1::Point2F(mini.left + w * .73f, mini.top + h * .62f),
                    w * .27f, h * .38f, farLight, mini);
    }
    canvas.Stroke(Inset(mini, .5f), sample.line, 10);
    const float chromeY = mini.top + 14.f;
    DrawBrandMark(mini.left + 10.f, chromeY - 8.f, 16.f, sample.accent);
    if (!sideLabel) {
        canvas.Text(colors.name.Get(), D2D1::RectF(mini.left + 30, mini.top + 3, mini.right - 66, mini.top + 25),
                    sample.text, canvas.caption.Get());
    }
    const float close = mini.right - 15.f, maximize = close - 15.f, minimize = maximize - 15.f;
    canvas.Line(minimize - 3, chromeY, minimize + 3, chromeY, sample.muted, 1.f);
    canvas.Stroke(D2D1::RectF(maximize - 2.7f, chromeY - 2.7f, maximize + 2.7f, chromeY + 2.7f),
                  sample.muted, 1.f, 1.f);
    canvas.Line(close - 3, chromeY - 3, close + 3, chromeY + 3, sample.muted, 1.f);
    canvas.Line(close + 3, chromeY - 3, close - 3, chromeY + 3, sample.muted, 1.f);

    const size_t count = std::min(ThemeSampleCount, library.items.size());
    if (!count) {
        canvas.Text(Texts::EmptyPreview.Get(),
                    D2D1::RectF(mini.left + 12, mini.top + 28, mini.right - 12, mini.bottom - 6),
                    sample.muted, canvas.caption.Get(), true);
        return;
    }
    const auto style = library.prefs.tileStyle;
    const bool coverStyle = style == TileStyle::Cover, compact = style == TileStyle::Compact;
    const bool always = library.prefs.labels == TileLabels::Always;
    const float caption = always ? 18.f : 0.f, gap = std::max(5.f, TileGap() * .64f);
    const float maxWidth = (mini.right - mini.left - 22.f - (count - 1) * gap) / count;
    const float roomHeight = mini.bottom - mini.top - 37.f;
    const float maxHeight = roomHeight * std::min(1.f, TileScale());
    const float cardWidth = std::min(maxWidth, coverStyle ? maxHeight - caption : compact ? 119.f : 91.f);
    const float cardHeight = coverStyle ? cardWidth + caption : compact ? 36.f : maxHeight;
    const float leftStart = (mini.left + mini.right - count * cardWidth - (count - 1) * gap) / 2.f;
    const float top = mini.top + 29.f + (roomHeight - cardHeight) / 2.f;
    for (size_t i = 0; i < count; ++i) {
        const auto& entry = library.items[i];
        const float left = leftStart + i * (cardWidth + gap);
        const auto box = D2D1::RectF(left, top, left + cardWidth, top + cardHeight);
        const float mid = left + cardWidth / 2.f;
        canvas.Fill(box, contrast ? sample.field : D2D1::ColorF(colors.tileTint, browseDark ? .83f : .69f), 10);
        auto* image = Icon(entry);
        auto* cover = TileCover(entry);
        if (cover && !cover->pixels) cover = nullptr;
        const auto picture = D2D1::RectF(left, top, box.right, box.bottom - (coverStyle ? caption : 0.f));
        const bool drawn = coverStyle &&
            DrawTilePicture(entry, cover ? cover : image, cover != nullptr, picture, box, sample, browseDark);
        if (!drawn) {
            const float size = compact ? 22.f : std::clamp((picture.bottom - top) * .52f, 18.f, 42.f);
            const float centerY = (top + picture.bottom) / 2.f - (!coverStyle && !compact && always ? 8.f : 0.f);
            const float centerX = compact && always ? left + 17.f : mid;
            const auto iconBox = D2D1::RectF(centerX - size / 2.f, centerY - size / 2.f,
                                            centerX + size / 2.f, centerY + size / 2.f);
            if (image && image->pixels) DrawIcon(image, iconBox);
            else canvas.Stroke(Inset(iconBox, 4.f), sample.muted, 5);
        }
        if (always) {
            const auto label = D2D1::RectF(left + (compact ? 32.f : 4.f),
                                           compact ? top : box.bottom - caption, box.right - 4.f, box.bottom);
            canvas.Text(entry.name, label, sample.text, canvas.caption.Get(), !compact);
        }
        canvas.Stroke(Inset(box, .5f), sample.line, 10);
    }
}

void App::DrawSettings() {
    using namespace SettingsLayout;
    const float x = SettingsEdge(), right = width - x;
    const float span = right - x;
    DrawButton(Back, D2D1::RectF(x, 17, x + 40, 57), L"");
    canvas.Text(Texts::Settings.Get(), D2D1::RectF(x + 53, 17, right, 57), theme.text, canvas.heading.Get());
    const wchar_t* tabs[] = {Texts::Themes.Get(), Texts::Tiles.Get(), Texts::Behavior.Get(), Texts::Backup.Get()};
    const float tabRoom = span - 3 * SettingsLayout::Gap;
    const float backupTabWidth = std::max(121.f, tabRoom / 4.f);
    const float tabWidth = (tabRoom - backupTabWidth) / 3.f;
    for (int i = 0; i < static_cast<int>(std::size(tabs)); ++i) {
        const float left = x + i * (tabWidth + SettingsLayout::Gap);
        const float rightEdge = i == 3 ? right : left + tabWidth;
        DrawButton(SettingsThemes + i, D2D1::RectF(left, TabsTop, rightEdge, TabsTop + ChipHeight),
                   tabs[i], static_cast<int>(settingsSection) == i);
    }

    const float top = SettingsClipTop();
    if (settingsSection == SettingsSection::Themes) {
        DrawThemePreview(D2D1::RectF(x, BodyTop, right, top - PreviewMargin));
    }
    const auto clip = D2D1::RectF(x - 2, top, right + 2, height - 16);
    canvas.target->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const size_t firstControl = hits.size();
    const auto row = [&](float position) { return SettingsPosition(position) - scroll; };
    const auto section = [&](float position, const wchar_t* title) {
        const float y = row(position);
        canvas.Text(title, D2D1::RectF(x, y, right, y + 24), theme.text, canvas.semibold.Get());
    };
    const auto options = [&](int first, float position, std::span<const wchar_t* const> labels,
                             int selectedOption, float indent = 0.f) {
        const float y = row(position);
        const float chipWidth = std::min(170.f, (span - indent - (labels.size() - 1) * SettingsLayout::Gap) / labels.size());
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            const float left = x + indent + i * (chipWidth + SettingsLayout::Gap);
            DrawButton(first + i, D2D1::RectF(left, y, left + chipWidth, y + ChipHeight), labels[i], i == selectedOption);
        }
    };
    const auto choices = [&](const wchar_t* title, int first, float position,
                             std::span<const wchar_t* const> labels, int choice) {
        const float y = row(position);
        canvas.Text(title, D2D1::RectF(x, y, x + 78, y + ChipHeight), theme.text);
        options(first, position, labels, choice, 82.f);
    };
    const auto setting = [&](int id, float position, float expansion, bool enabled) {
        const float y = row(position);
        const auto area = D2D1::RectF(x, y, right, y + SettingsLayout::RowHeight);
        if (enabled) hits.push_back({id, area});
        const auto group = D2D1::RectF(x, y, right, area.bottom + expansion);
        const auto fill = expansion > 0.f ? theme.dialog : enabled && hover == id ? theme.button : theme.tile;
        canvas.Fill(group, fill, 9);
        canvas.Stroke(group, theme.line, 9);
        if (enabled && focused == id && keyboard) canvas.Stroke(Inset(area, 2.f), theme.accent, 7, 1.4f);
        return y;
    };
    const auto toggle = [&](int id, float position, const wchar_t* title, bool checked, bool enabled = true,
                            const wchar_t* hint = nullptr) {
        const float y = setting(id, position, 0.f, enabled);
        const float bottom = y + SettingsLayout::RowHeight;
        canvas.Text(title, D2D1::RectF(x + 14, y, right - 70, hint ? y + 24 : bottom),
                    enabled ? theme.text : theme.muted);
        if (hint) canvas.Text(hint, D2D1::RectF(x + 14, y + 22, right - 70, bottom), theme.muted, canvas.caption.Get());
        const float mid = (y + bottom) / 2.f;
        const auto track = D2D1::RectF(right - 55, mid - 9.5f, right - 14, mid + 9.5f);
        const auto trackColor = checked && enabled ? theme.primary : theme.field;
        canvas.Fill(track, enabled ? trackColor : Opacity(trackColor, .45f), 9);
        canvas.Stroke(track, enabled && checked ? theme.primary : enabled ? theme.muted : theme.line, 9);
        const float center = checked && enabled ? right - 24 : right - 45;
        const auto knob = checked && enabled ? theme.primaryText : theme.muted;
        canvas.Fill(D2D1::RectF(center - 5.5f, mid - 5.5f, center + 5.5f, mid + 5.5f),
                    enabled ? knob : Opacity(knob, .52f), 6);
    };
    if (settingsSection == SettingsSection::Themes) {
        const wchar_t* modes[] = {Texts::MatchWindows.Get(), Texts::LightMode.Get(), Texts::DarkMode.Get()};
        const wchar_t* palettes[] = {Texts::LightPalettes.Get(), Texts::DarkPalettes.Get()};
        choices(Texts::Mode.Get(), ThemeSystem, ThemeMode, modes, library.prefs.theme);
        choices(Texts::Palette.Get(), BrowseLight, PaletteMode, palettes, browseDark ? 1 : 0);
        const int paletteColumns = PaletteColumns();
        const float chipWidth = (span - (paletteColumns - 1) * SettingsLayout::Gap) / paletteColumns;
        const auto& colors = browseDark ? DarkSchemes : LightSchemes;
        const int first = browseDark ? FirstDarkPalette : FirstLightPalette;
        const int chosen = browseDark ? library.prefs.darkPalette : library.prefs.lightPalette;
        for (int i = 0; i < PaletteCount; ++i) {
            const float left = x + i % paletteColumns * (chipWidth + SettingsLayout::Gap);
            const float y = PaletteOptionTop(i) - scroll;
            DrawPaletteButton(first + i, D2D1::RectF(left, y, left + chipWidth, y + ChipHeight), colors[i], i == chosen);
        }
        const float y = setting(AmbientControl, AmbientPosition(), 0.f, true);
        const auto track = AmbientTrack();
        hits.back().box.left = track.left - 14.f;
        hits.back().box.right = track.right + 14.f;
        canvas.Text(Texts::AmbientBrightness.Get(), D2D1::RectF(x + 14, y, track.left - 15, y + SettingsLayout::RowHeight), theme.text);
        canvas.Fill(track, theme.field, 2);
        canvas.Stroke(track, theme.line, 2);
        const int amount = AmbientValue();
        const float knob = track.left + (track.right - track.left) * amount / AmbientMax;
        if (amount > 0) canvas.Fill(D2D1::RectF(track.left, track.top, knob, track.bottom), theme.accent, 2);
        const float mid = (track.top + track.bottom) / 2.f;
        const auto dot = D2D1::RectF(knob - 6, mid - 6, knob + 6, mid + 6);
        canvas.Fill(dot, theme.primaryText, 6);
        canvas.Stroke(dot, theme.accent, 6, 1.6f);
        canvas.Text(std::to_wstring(amount) + L"%", D2D1::RectF(right - 58, y, right - 8, y + SettingsLayout::RowHeight),
                    theme.text, canvas.body.Get(), true);
        section(EffectPosition(), Texts::AmbientEffect.Get());
        static constexpr UiText EffectNames[AmbientStyleCount] = {
            Texts::EffectDrift, Texts::EffectAurora, Texts::EffectPulse, Texts::EffectCursor, Texts::EffectStill};
        const int effectColumns = PaletteColumns();
        const float effectWidth = (span - (effectColumns - 1) * SettingsLayout::Gap) / effectColumns;
        for (int i = 0; i < AmbientStyleCount; ++i) {
            const float left = x + i % effectColumns * (effectWidth + SettingsLayout::Gap);
            const float top = row(EffectPosition() + 26.f) + i / effectColumns * SettingsLayout::Pitch;
            DrawButton(FirstAmbientStyle + i, D2D1::RectF(left, top, left + effectWidth, top + ChipHeight),
                       EffectNames[i].Get(), static_cast<int>(library.prefs.ambientStyle) == i);
        }
    } else if (settingsSection == SettingsSection::Tiles) {
        const wchar_t* tileStyles[] = {Texts::Centered.Get(), Texts::FullTile.Get(), Texts::Compact.Get()};
        const wchar_t* labelStyles[] = {Texts::Always.Get(), Texts::OnHover.Get(), Texts::Hidden.Get()};
        const wchar_t* sizes[] = {Texts::Small.Get(), Texts::Medium.Get(), Texts::Large.Get()};
        const wchar_t* gaps[] = {Texts::Tight.Get(), Texts::Normal.Get(), Texts::Wide.Get()};
        section(0, Texts::Layout.Get());
        choices(Texts::TileStyle.Get(), TilesCentered, Tiles, tileStyles, static_cast<int>(library.prefs.tileStyle));
        choices(Texts::Labels.Get(), LabelsAlways, Labels, labelStyles, static_cast<int>(library.prefs.labels));
        choices(Texts::Size.Get(), SizeSmall, SettingsLayout::TileSize, sizes, static_cast<int>(library.prefs.tileSize));
        choices(Texts::Spacing.Get(), SpacingTight, SettingsLayout::TileSpacing, gaps, static_cast<int>(library.prefs.tileSpacing));
        toggle(Control::TileHalo, SettingsLayout::TileHalo, Texts::TileHalo.Get(), library.prefs.tileHalo);
    } else if (settingsSection == SettingsSection::Behavior) {
        section(0, Texts::WindowActions.Get());
        for (const auto& menu : BehaviorControls) {
            const bool expanded = expandedSetting == menu.id;
            const float y = setting(menu.id, menu.top, expanded ? menu.Expansion() : 0.f, true);
            const int chosen = BehaviorSelection(menu.id);
            const float selectionLeft = right - std::min(192.f, span * .47f);
            const float mid = y + SettingsLayout::RowHeight / 2.f;
            canvas.Text(menu.title.Get(), D2D1::RectF(x + 14, y, selectionLeft - 7, y + SettingsLayout::RowHeight), theme.text);
            canvas.Fill(D2D1::RectF(selectionLeft, y + 6, right - 7, y + 37), theme.field, 6);
            canvas.Text(menu.options[chosen].Get(), D2D1::RectF(selectionLeft + 10, y + 6, right - 27, y + 37), theme.text);
            const float tip = mid + (expanded ? -2.f : 2.f), side = mid + (expanded ? 2.f : -2.f);
            canvas.Line(right - 21, side, right - 17, tip, theme.muted);
            canvas.Line(right - 17, tip, right - 13, side, theme.muted);
            if (!expanded) continue;
            canvas.Line(x + 9, y + SettingsLayout::RowHeight, right - 9, y + SettingsLayout::RowHeight, theme.line, 1.f);
            for (int i = 0; i < static_cast<int>(menu.options.size()); ++i) {
                const int id = FirstBehaviorOption + i;
                const float optionY = y + SettingsLayout::RowHeight + i * ChipHeight;
                const auto box = D2D1::RectF(x + 7, optionY + 2, right - 7, optionY + ChipHeight);
                hits.push_back({id, box});
                if (!contrast && (i == chosen || hover == id)) canvas.Fill(box, Opacity(theme.accent, hover == id ? .18f : .12f), 6);
                if (focused == id && keyboard) canvas.Stroke(Inset(box, 1), theme.accent, 6, 1.4f);
                canvas.Text(menu.options[i].Get(), D2D1::RectF(x + 31, box.top, right - 14, box.bottom), theme.text);
                if (i == chosen) {
                    const float cy = (box.top + box.bottom) / 2.f;
                    const auto mark = contrast ? theme.text : theme.accent;
                    canvas.Line(x + 15, cy, x + 18, cy + 3, mark);
                    canvas.Line(x + 18, cy + 3, x + 24, cy - 3, mark);
                }
            }
        }
        section(SettingsLayout::StartupSection, Texts::Startup.Get());
        toggle(AutoStartControl, AutoStart, Texts::StartInTray.Get(), autoStart);
        section(SettingsLayout::MotionSection, Texts::MotionAndPower.Get());
        toggle(Control::Motion, SettingsLayout::Motion, Texts::Animations.Get(), library.prefs.motion);
        const auto hint = !portableBattery ? Texts::BatteryHint.Get()
            : onBattery && library.prefs.motion && library.prefs.pauseMotionOnBattery ? Texts::MotionPaused.Get() : nullptr;
        toggle(Control::MotionOnBattery, SettingsLayout::MotionOnBattery, Texts::PauseOnBattery.Get(),
               library.prefs.pauseMotionOnBattery, portableBattery, hint);
    } else {
        const float backupY = row(Backup);
        DrawButton(BackupControl, D2D1::RectF(x, backupY, x + 171, backupY + ChipHeight), Texts::SaveBackup.Get(), true);
        DrawButton(RestoreControl, D2D1::RectF(x + 179, backupY, x + 350, backupY + ChipHeight), Texts::RestoreBackup.Get());
        canvas.Text(Texts::BackupContents.Get(),
                    D2D1::RectF(x, row(0), right, row(24)), theme.text);
        section(DataSection, Texts::Data.Get());
        canvas.Text(Texts::DataFolderHint.Get(),
                    D2D1::RectF(x, row(DataSection + 24.f), right, row(DataSection + 48.f)), theme.muted);
        const float dataY = row(DataFolder);
        DrawButton(OpenDataControl, D2D1::RectF(x, dataY, x + 220, dataY + ChipHeight), Texts::OpenDataFolder.Get());
    }
    canvas.target->PopAxisAlignedClip();
    for (size_t i = firstControl; i < hits.size(); ++i) hits[i].box = VisiblePart(hits[i].box, clip);
    DrawScrollArrows(clip, right + 3.f);
}

void App::DrawContextMenu() {
    const auto panel = MenuBox();
    bool faded = false;
    if (contextMenu->opened) {
        if (!canvas.menuLayer) canvas.target->CreateLayer(&canvas.menuLayer);
        if (canvas.menuLayer) {
            const float time = std::min(1.f, static_cast<float>(GetTickCount64() - contextMenu->opened) / MenuDuration);
            canvas.target->PushLayer(D2D1::LayerParameters(
                D2D1::RectF(panel.left - 7, panel.top - 7, panel.right + 7, panel.bottom + 10),
                nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::Matrix3x2F::Identity(),
                1.f - (1.f - time) * (1.f - time)), canvas.menuLayer.Get());
            faded = true;
        }
    }
    if (!contrast) canvas.Shadow(panel, 12, dark ? .25f : .16f);
    canvas.Fill(panel, theme.dialog, 12);
    if (!contrast) {
        canvas.Glow(D2D1::Point2F(panel.left + 108.f, panel.top + 54.f), 100.f, 52.f,
                    D2D1::ColorF(scheme->nearGlow, dark ? .16f : .17f), panel);
    }
    canvas.Stroke(Inset(panel, .5f), theme.line, 12);
    for (const auto& item : MenuItems()) {
        const int id = item.id;
        const auto box = MenuItemBox(id);
        if (item.separator) {
            canvas.Line(box.left + 7, box.top - MenuGap / 2, box.right - 7, box.top - MenuGap / 2, theme.line, 1);
        }
        const bool enabled = MenuEnabled(id);
        const bool keyFocus = focused == id && keyboard;
        const float over = ControlAmount(hoverMotion, id, hover == id);
        const float push = ControlAmount(pressMotion, id, pressed == id);
        auto ink = id == DeleteMenuItem && !contrast ? D2D1::ColorF(dark ? 0xFFC2B9 : 0x9D3728) : theme.text;
        if (!enabled) ink = Opacity(theme.muted, .57f);
        if (enabled && (over > 0 || keyFocus || push > 0)) {
            canvas.Fill(Inset(box, 1.f), contrast ? theme.accent :
                Opacity(theme.accent, .18f * std::max(over, keyFocus ? 1.f : push) + .09f * push), 7);
            if (contrast) ink = theme.primaryText;
            if (keyFocus && !contrast) canvas.Stroke(Inset(box, 1.f), Opacity(theme.accent, .50f), 7);
        }
        const float textRight = box.right - (id == EditMenu ? 40.f : 10.f);
        Glyph(id, box.left + 18, (box.top + box.bottom) / 2, ink);
        canvas.Text(item.label.Get(), D2D1::RectF(box.left + 35, box.top, textRight, box.bottom), ink);
        if (id == EditMenu) {
            canvas.Text(L"F2", D2D1::RectF(box.right - 34, box.top, box.right - 8, box.bottom),
                        contrast ? ink : theme.muted, canvas.caption.Get(), true);
        }
    }
    if (faded) canvas.target->PopLayer();
}

void App::DrawDialog() {
    hits.clear();
    const float p = dialogSince ? std::clamp((GetTickCount64() - dialogSince) / 160.f, 0.f, 1.f) : 1.f;
    canvas.Fill(D2D1::RectF(0, 0, width, height), Opacity(theme.dim, theme.dim.a * (.42f + .58f * p)));
    const auto panel = Popup();
    if (!contrast) canvas.Shadow(panel, 12, (dark ? .24f : .15f) * p);
    canvas.Fill(panel, theme.dialog, 12);
    if (!contrast) {
        canvas.Glow(D2D1::Point2F(panel.left + 30, panel.top + 16), 200, 135,
        D2D1::ColorF(scheme->nearGlow, dark ? .14f : .16f), Inset(panel, 6));
    }
    canvas.Stroke(Inset(panel, .5f), theme.line, 12);
    const float x = panel.left + 24, right = panel.right - 24, y = panel.top;
    const auto title = dialog == Dialog::Edit ? Texts::NameAndArtwork.Get() :
                       dialog == Dialog::Cover ? Texts::CropCover.Get() :
                       dialog == Dialog::Missing ? Texts::ShortcutDidNotOpen.Get() :
                       dialog == Dialog::Remove ? Texts::RemoveShortcutQuestion.Get() :
                       dialog == Dialog::Restore ? Texts::RestoreBackupQuestion.Get() :
                       dialog == Dialog::Info ? Texts::Backup.Get() : Texts::ShortcutLibrary.Get();
    canvas.Text(title, D2D1::RectF(x, y + 14, right - 28, y + 49), theme.text, canvas.heading.Get());
    const int close = dialog == Dialog::Cover ? CancelCover :
                      dialog == Dialog::Edit || dialog == Dialog::Remove || dialog == Dialog::Restore ? CancelEdit : DismissError;
    hits.push_back({close, D2D1::RectF(right - 23, y + 17, right + 7, y + 47)});
    Glyph(CancelEdit, right - 7, y + 32, theme.muted);
    if (dialog == Dialog::Cover) {
        DrawCoverDialog(panel);
        return;
    }
    if (dialog == Dialog::Edit) {
        canvas.Text(Texts::Name.Get(), D2D1::RectF(x, y + 55, right, y + 80), theme.muted);
        const auto field = D2D1::RectF(x + 2, y + 83, right - 2, y + 119);
        hits.push_back({EditName, field});
        canvas.Fill(field, theme.field, 7);
        canvas.Stroke(field, GetFocus() == nameEdit ? theme.accent : theme.line, 7);
        const auto previewBox = D2D1::RectF(x, y + 140, x + 68, y + 208);
        canvas.Fill(previewBox, theme.tile, 10);
        canvas.Stroke(previewBox, theme.line, 10);
        if (preview) {
            IconCache custom{preview, previewBitmap};
            DrawIcon(&custom, Inset(previewBox, 8));
            previewBitmap = custom.image;
        } else {
            const auto index = library.Find(editing);
            DrawIcon(index < library.items.size() ? Icon(library.items[index], resetIcon) : nullptr, Inset(previewBox, 8));
        }
        DrawButton(ChangeIcon, D2D1::RectF(x + 82, y + 140, right, y + 177), Texts::ChangeIcon.Get());
        canvas.Text(Texts::IconFormats.Get(), D2D1::RectF(x + 86, y + 181, right, y + 205), theme.muted, canvas.caption.Get());
        const auto reset = D2D1::RectF(x, y + 215, std::min(right, x + 212), y + 244);
        hits.push_back({ResetIcon, reset});
        if (keyboard && focused == ResetIcon) canvas.Stroke(reset, theme.accent, 5);
        canvas.Text(Texts::RestoreDefault.Get(), reset,
                    contrast ? theme.text : theme.accent);
        const bool hasCover = HasCover();
        DrawButton(EditCover, D2D1::RectF(x, y + 246, hasCover ? right - 148 : right, y + 284),
                   hasCover ? Texts::EditCover.Get() : Texts::AddCover.Get());
        if (hasCover) {
            DrawButton(RemoveCoverEdit, D2D1::RectF(right - 140, y + 246, right, y + 284), Texts::Remove.Get());
        }
        if (!notice.empty()) {
            canvas.Text(notice, D2D1::RectF(x, y + 288, right, y + 309),
                        contrast ? theme.text : D2D1::ColorF(dark ? 0xFFB7AB : 0xA23123), canvas.caption.Get());
        } else {
            canvas.Text(coverDraft && coverDraft->remove ? Texts::CoverPendingRemoval.Get()
                       : Texts::CoverStyleHint.Get(), D2D1::RectF(x, y + 288, right, y + 309),
                        theme.muted, canvas.caption.Get());
        }
        DrawButton(CancelEdit, D2D1::RectF(right - 204, y + 314, right - 106, y + 352), Texts::Cancel.Get());
        DrawButton(SaveEdit, D2D1::RectF(right - 96, y + 314, right, y + 352), Texts::Save.Get(), true);
        return;
    }
    const float bottom = panel.bottom - 22;
    if (dialog == Dialog::Remove) {
        canvas.Text(notice, D2D1::RectF(x, y + 59, right, y + 88), theme.text, canvas.semibold.Get());
        canvas.Text(Texts::RemoveLibraryCopy.Get(), D2D1::RectF(x, y + 90, right, y + 120), theme.muted);
        DrawButton(CancelEdit, D2D1::RectF(right - 218, bottom - 39, right - 113, bottom), Texts::Cancel.Get());
        DrawButton(ConfirmRemoval, D2D1::RectF(right - 102, bottom - 39, right, bottom), Texts::Remove.Get(), true);
    } else {
        auto format = canvas.body.Get();
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        canvas.Text(notice, D2D1::RectF(x, y + 56, right, y + 135), theme.muted);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (dialog == Dialog::Restore) {
            DrawButton(CancelEdit, D2D1::RectF(right - 232, bottom - 39, right - 122, bottom), Texts::Cancel.Get());
            DrawButton(ConfirmRestore, D2D1::RectF(right - 114, bottom - 39, right, bottom), Texts::Restore.Get(), true);
        } else if (dialog == Dialog::Missing) {
            DrawButton(DismissError, D2D1::RectF(right - 286, bottom - 39, right - 184, bottom), Texts::Close.Get());
            DrawButton(ReplaceError, D2D1::RectF(right - 174, bottom - 39, right, bottom), Texts::ChangeTarget.Get(), true);
        } else DrawButton(DismissError, D2D1::RectF(right - 105, bottom - 39, right, bottom), Texts::Okay.Get(), true);
    }
}

void App::DrawCoverDialog(D2D1_RECT_F panel) {
    const float x = panel.left + 24.f, right = panel.right - 24.f, y = panel.top;
    const auto view = CoverViewport();
    const bool always = library.prefs.labels == TileLabels::Always;
    const auto tile = D2D1::RectF(view.left, view.top, view.right, view.bottom + (always ? TileCaptionHeight : 0.f));
    const bool hasImage = cropPreview.pixels != nullptr;
    canvas.Fill(tile, theme.field, 11);
    if (hasImage) {
        hits.push_back({CoverPreview, view});
        const bool drawn = canvas.TileImage(IconBitmap(&cropPreview), *cropPreview.pixels, view, tile,
                                            contrast ? theme.background : theme.dialog, nullptr, coverWork.crop);
        if (drawn && always) {
            wchar_t title[256]{};
            GetWindowTextW(nameEdit, title, ARRAYSIZE(title));
            canvas.Line(tile.left + 1.f, view.bottom, tile.right - 1.f, view.bottom, theme.line, 1.f);
            canvas.Text(title, D2D1::RectF(tile.left + 8, view.bottom, tile.right - 8, tile.bottom),
                        theme.text, canvas.semibold.Get(), true);
        }
    } else {
        canvas.Text(Texts::CoverFormats.Get(), Inset(tile, 8), theme.muted, canvas.body.Get(), true);
    }
    canvas.Stroke(Inset(tile, .5f), keyboard && focused == CoverPreview ? theme.accent : theme.line, 10);
    const auto hintBox = D2D1::RectF(x, y + 259, right, y + 283);
    if (!notice.empty()) {
        canvas.Text(notice, hintBox, contrast ? theme.text : D2D1::ColorF(dark ? 0xFFB7AB : 0xA23123), canvas.caption.Get(), true);
    } else if (hasImage) {
        canvas.Text(Texts::DragCrop.Get(), hintBox, theme.muted, canvas.caption.Get(), true);
    } else {
        canvas.Text(Texts::PickOrDropCover.Get(), hintBox, theme.muted, canvas.caption.Get(), true);
    }
    if (hasImage) {
        DrawButton(ZoomOut, D2D1::RectF(x, y + 288, x + 34, y + 322), L"");
        DrawButton(ZoomIn, D2D1::RectF(x + 42, y + 288, x + 76, y + 322), L"");
        canvas.Text(Texts::Zoom.Get() + std::to_wstring(std::lround(coverWork.crop.zoom * 100.f)) + L"%",
                    D2D1::RectF(x + 88, y + 288, right - 101, y + 322), theme.text);
        DrawButton(ResetCrop, D2D1::RectF(right - 92, y + 288, right, y + 322), Texts::Reset.Get());
    }
    DrawButton(ChooseCover, D2D1::RectF(x, y + 329, x + 159, y + 364), Texts::ChooseFile.Get());
    if (CoverRemovable()) {
        DrawButton(RemoveCover, D2D1::RectF(right - 162, y + 329, right, y + 364), Texts::RemoveCover.Get());
    }
    DrawButton(CancelCover, D2D1::RectF(right - 222, y + 371, right - 114, y + 405), Texts::Cancel.Get());
    if (hasImage || coverWork.remove) {
        DrawButton(ApplyCover, D2D1::RectF(right - 106, y + 371, right, y + 405), Texts::Apply.Get(), true);
    }
}
