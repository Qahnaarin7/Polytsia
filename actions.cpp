// Tile actions (launch, reveal, import, sort) and the in-window context menu.
#include "app.hpp"
#include "log.hpp"
#include <thread>

void App::Launch(const std::wstring& id, bool elevated) {
    const size_t index = library.Find(id);
    if (index == library.items.size() || launching) return;
    launching = true;
    const auto file = library.File(library.items[index].file);
    auto requestedId = std::make_unique<std::wstring>(id);
    const HWND owner = window;
    std::thread([owner, file, elevated, requestedId = std::move(requestedId)]() mutable {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const auto result = LaunchShortcut(file, owner, elevated);
        if (SUCCEEDED(com)) CoUninitialize();
        if (PostMessageW(owner, LaunchReady, static_cast<WPARAM>(result), reinterpret_cast<LPARAM>(requestedId.get()))) {
            requestedId.release();
        }
    }).detach();
}

void App::RevealTarget(const std::wstring& id) {
    const size_t index = library.Find(id);
    if (index == library.items.size()) return;
    if (!RevealInExplorer(ShortcutTarget(library.File(library.items[index].file)))) Error(Texts::TargetNotFound.Get());
}

void App::SortByUse() {
    switch (library.SortByLaunches()) {
    case Library::SortResult::Sorted: Filter(); break;
    case Library::SortResult::Unchanged: OpenDialog(Dialog::Info, Texts::SortUnchanged.Get()); break;
    case Library::SortResult::NoLaunches: OpenDialog(Dialog::Info, Texts::SortNoLaunches.Get()); break;
    case Library::SortResult::Failed: Error(Texts::SaveOrderError.Get()); break;
    }
}

void App::Context(int id, POINT position) {
    const size_t row = static_cast<size_t>(id - Tile);
    ScreenToClient(window, &position);
    contextMenu = ContextMenu{{}, position, effects ? GetTickCount64() : 0, false, false};
    if (id >= Tile && row < visible.size()) {
        selected = static_cast<int>(row);
        const auto originalIndex = visible[row];
        contextMenu->entry = library.items[originalIndex].id;
        contextMenu->elevatable = contextMenu->located = ShortcutElevatable(library.File(library.items[originalIndex].file));
        contextMenu->earlier = originalIndex > 0;
        contextMenu->later = originalIndex + 1 < library.items.size();
    }
    focused = MenuItems().front().id;
    ChangePress(0);
    EndDrag();
    if (GetCapture() == window) ReleaseCapture();
    ChangeHover(0);
    SetFocus(window);
    Layout();
    if (contextMenu->opened) SetTimer(window, AnimationTimer, 16, nullptr);
}

std::span<const MenuItem> App::MenuItems() const {
    return contextMenu->entry.empty() ? std::span<const MenuItem>(BackgroundMenuItems)
                                      : std::span<const MenuItem>(TileMenuItems);
}

bool App::MenuEnabled(int action) const {
    return contextMenu && action >= MenuItems().front().id && action <= MenuItems().back().id &&
        (action != EarlierMenu || contextMenu->earlier) && (action != LaterMenu || contextMenu->later) &&
        (action != AdminMenu || contextMenu->elevatable) && (action != LocationMenu || contextMenu->located) &&
        (action != SortMenu || library.items.size() > 1);
}

bool App::MenuKey(UINT key) {
    switch (key) {
    case VK_ESCAPE: case VK_APPS: case VK_F10: CloseMenu(); return true;
    case VK_RETURN: case VK_SPACE: RunMenu(hover ? hover : focused); return true;
    case VK_F2: RunMenu(EditMenu); return true;
    case VK_UP: case VK_DOWN: case VK_TAB: case VK_HOME: case VK_END: {
        const int first = MenuItems().front().id, last = MenuItems().back().id;
        const int count = last - first + 1;
        const bool reverse = key == VK_UP || (key == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
        int next = hover ? hover : focused;
        if (key == VK_HOME) next = first;
        else if (key == VK_END) next = last;
        else do {
            next = first + (next - first + (reverse ? count - 1 : 1)) % count;
        } while (!MenuEnabled(next));
        keyboard = true;
        focused = next;
        ChangeHover(0);
        Invalidate();
        return true;
    }
    default: return false;
    }
}

void App::CloseMenu() {
    if (!contextMenu) return;
    contextMenu.reset();
    if (MenuCommand(pressed)) {
        ChangePress(0);
        if (GetCapture() == window) ReleaseCapture();
    }
    focused = selected >= 0 ? Tile + selected : 0;
    ChangeHover(0);
    Layout();
}

void App::RunMenu(int command) {
    if (!MenuEnabled(command)) return;
    const auto id = contextMenu->entry;
    CloseMenu();
    if (command == SearchMenu) { ShowSearch(true); return; }
    if (command == AddMenu) { Command(Add); return; }
    if (command == SettingsMenu) { View(Page::Settings); return; }
    if (command == SortMenu) { SortByUse(); return; }
    const auto originalIndex = library.Find(id);
    if (originalIndex >= library.items.size()) return;
    switch (command) {
    case EditMenu: OpenEditor(id); break;
    case LaunchMenu: Launch(id); break;
    case AdminMenu: Launch(id, true); break;
    case LocationMenu: RevealTarget(id); break;
    case ReplaceMenu: Replace(id); break;
    case EarlierMenu:
    case LaterMenu: {
        const auto other = command == EarlierMenu ? originalIndex - 1 : originalIndex + 1;
        if (other >= library.items.size()) break;
        if (library.Move(id, library.items[other].id)) Filter();
        else Error(Texts::SaveOrderError.Get());
        break;
    }
    case DeleteMenuItem: editing = id; OpenDialog(Dialog::Remove, library.items[originalIndex].name); break;
    default: break;
    }
}
