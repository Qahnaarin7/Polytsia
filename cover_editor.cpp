#include "app.hpp"
#include <algorithm>

void App::OpenCover() {
    const auto index = library.Find(editing);
    if (index == library.items.size()) return;
    const auto& entry = library.items[index];
    coverWork = coverDraft.value_or(CoverChange{{}, entry.crop, false});
    cropPreview = {};
    std::wstring error;
    if (!coverWork.remove) {
        const auto path = !coverWork.image.empty() ? coverWork.image :
            entry.cover.empty() ? fs::path{} : library.File(entry.cover);
        if (!path.empty()) {
            cropPreview.pixels = DecodeCover(path, 896);
            if (!cropPreview.pixels) error = Texts::ReadCurrentCoverError.Get();
        }
    }
    OpenDialog(Dialog::Cover, error);
    focused = cropPreview.pixels ? CoverPreview : ChooseCover;
}

void App::FinishCover(bool keep) {
    if (pressed == CoverPreview) {
        pressed = 0;
        if (GetCapture() == window) ReleaseCapture();
    }
    if (keep && (cropPreview.pixels || coverWork.remove)) coverDraft = coverWork;
    cropPreview = {};
    coverWork = {};
    OpenDialog(Dialog::Edit);
}

void App::SetCoverImage(const fs::path& path) {
    auto pixels = DecodeCover(path, 896);
    if (!pixels) {
        notice = Texts::ReadCoverError.Get();
    } else {
        coverWork = {path, {}, false};
        cropPreview = {std::move(pixels), {}};
        focused = CoverPreview;
        notice.clear();
    }
    Invalidate();
}

bool App::CoverRemovable() const {
    const auto index = library.Find(editing);
    return !coverWork.remove && (cropPreview.pixels ||
        (index < library.items.size() && !library.items[index].cover.empty()));
}

bool App::HasCover() const {
    if (coverDraft && coverDraft->remove) return false;
    if (coverDraft && !coverDraft->image.empty()) return true;
    const auto index = library.Find(editing);
    return index < library.items.size() && !library.items[index].cover.empty();
}

void App::PanCover(float dx, float dy) {
    if (!cropPreview.pixels) return;
    const auto view = CoverViewport();
    const float viewWidth = view.right - view.left, viewHeight = view.bottom - view.top;
    if (viewWidth <= 0.f || viewHeight <= 0.f) return;
    const auto art = cropPreview.pixels->artwork;
    const auto frame = CropFrame(*cropPreview.pixels, D2D1::SizeF(viewWidth, viewHeight), coverWork.crop);
    const float artW = art.right - art.left, artH = art.bottom - art.top;
    const float spanX = std::min(1.f, (frame.right - frame.left) / artW);
    const float spanY = std::min(1.f, (frame.bottom - frame.top) / artH);
    const float x = ((frame.left + frame.right) / 2.f - art.left) / artW - dx * spanX / viewWidth;
    const float y = ((frame.top + frame.bottom) / 2.f - art.top) / artH - dy * spanY / viewHeight;
    coverWork.crop.x = std::clamp(x, spanX / 2.f, 1.f - spanX / 2.f);
    coverWork.crop.y = std::clamp(y, spanY / 2.f, 1.f - spanY / 2.f);
    Invalidate();
}

void App::ZoomCover(float amount) {
    if (!cropPreview.pixels) return;
    PanCover(0.f, 0.f);
    coverWork.crop.zoom = std::clamp(coverWork.crop.zoom + amount, 1.f, CropZoomMax);
    Invalidate();
}

void App::CoverCommand(int id) {
    switch (id) {
    case ChooseCover: {
        const auto paths = ChooseFiles(window, FileKind::Cover, false);
        if (!paths.empty()) SetCoverImage(paths.front());
        break;
    }
    case RemoveCover:
        coverWork = {{}, {}, true};
        cropPreview = {};
        notice.clear();
        focused = ApplyCover;
        break;
    case ResetCrop: coverWork.crop = {}; break;
    case ZoomOut: ZoomCover(-.1f); break;
    case ZoomIn: ZoomCover(.1f); break;
    case CancelCover: FinishCover(false); break;
    case ApplyCover: if (cropPreview.pixels || coverWork.remove) FinishCover(true); break;
    }
    Invalidate();
}
