#pragma once
#include <windows.h>

enum class UiLanguage { Ukrainian, English, Russian };

constexpr UiLanguage LanguageFor(LANGID language) {
    switch (PRIMARYLANGID(language)) {
    case LANG_UKRAINIAN: return UiLanguage::Ukrainian;
    case LANG_RUSSIAN: return UiLanguage::Russian;
    default: return UiLanguage::English;
    }
}

inline UiLanguage WindowsUiLanguage() {
    static const auto language = LanguageFor(GetThreadUILanguage());
    return language;
}

inline const wchar_t* UiLocale() {
    switch (WindowsUiLanguage()) {
    case UiLanguage::Ukrainian: return L"uk-UA";
    case UiLanguage::Russian: return L"ru-RU";
    default: return L"en-US";
    }
}

struct UiText {
    const wchar_t* uk;
    const wchar_t* en;
    const wchar_t* ru;

    constexpr const wchar_t* Get(UiLanguage language) const {
        switch (language) {
        case UiLanguage::Ukrainian: return uk;
        case UiLanguage::Russian: return ru;
        default: return en;
        }
    }

    const wchar_t* Get() const { return Get(WindowsUiLanguage()); }
};

namespace Texts {
inline constexpr UiText AppName{L"Полиця", L"Polytsia", L"Полиця"};
inline constexpr UiText FindShortcut{L"Знайти ярлик…", L"Find a shortcut…", L"Найти ярлык…"};
inline constexpr UiText EmptyLibrary{L"Тут будуть твої ярлики", L"Your shortcuts will appear here", L"Здесь будут твои ярлыки"};
inline constexpr UiText NoMatches{L"Ярликів не знайдено", L"No shortcuts found", L"Ярлыки не найдены"};
inline constexpr UiText EmptyLibraryHint{L"Відкрий меню правою кнопкою або перетягни файли з Провідника.", L"Right-click to open the menu or drag files from File Explorer.", L"Открой меню правой кнопкой или перетащи файлы из Проводника."};
inline constexpr UiText SearchHint{L"Спробуй іншу назву.", L"Try a different name.", L"Попробуй другое название."};
inline constexpr UiText Preview{L"Попередній перегляд", L"Preview", L"Предпросмотр"};
inline constexpr UiText EmptyPreview{L"Тут з’являться твої ярлики", L"Your shortcuts will appear here", L"Здесь появятся твои ярлыки"};
inline constexpr UiText Settings{L"Налаштування", L"Settings", L"Настройки"};
inline constexpr UiText Themes{L"Теми", L"Themes", L"Темы"};
inline constexpr UiText Tiles{L"Плитки", L"Tiles", L"Плитки"};
inline constexpr UiText Behavior{L"Поведінка", L"Behavior", L"Поведение"};
inline constexpr UiText Backup{L"Резервна копія", L"Backup", L"Резервная копия"};
inline constexpr UiText MatchWindows{L"Як у Windows", L"Match Windows", L"Как в Windows"};
inline constexpr UiText LightMode{L"Світла", L"Light", L"Светлая"};
inline constexpr UiText DarkMode{L"Темна", L"Dark", L"Тёмная"};
inline constexpr UiText LightPalettes{L"Світлі", L"Light", L"Светлые"};
inline constexpr UiText DarkPalettes{L"Темні", L"Dark", L"Тёмные"};
inline constexpr UiText Mode{L"Режим", L"Mode", L"Режим"};
inline constexpr UiText Palette{L"Палітра", L"Palette", L"Палитра"};
inline constexpr UiText AmbientBrightness{L"Яскравість Ambient", L"Ambient brightness", L"Яркость фонового света"};
inline constexpr UiText Centered{L"По центру", L"Centered", L"По центру"};
inline constexpr UiText FullTile{L"На всю плитку", L"Full tile", L"На всю плитку"};
inline constexpr UiText Compact{L"Компактні", L"Compact", L"Компактные"};
inline constexpr UiText Always{L"Завжди", L"Always", L"Всегда"};
inline constexpr UiText OnHover{L"При наведенні", L"On hover", L"При наведении"};
inline constexpr UiText Hidden{L"Приховані", L"Hidden", L"Скрытые"};
inline constexpr UiText Small{L"Малі", L"Small", L"Малые"};
inline constexpr UiText Medium{L"Середні", L"Medium", L"Средние"};
inline constexpr UiText Large{L"Великі", L"Large", L"Большие"};
inline constexpr UiText Tight{L"Щільні", L"Tight", L"Плотные"};
inline constexpr UiText Normal{L"Звичайні", L"Normal", L"Обычные"};
inline constexpr UiText Wide{L"Широкі", L"Wide", L"Широкие"};
inline constexpr UiText Layout{L"Розкладка", L"Layout", L"Раскладка"};
inline constexpr UiText TileStyle{L"Вигляд", L"Style", L"Вид"};
inline constexpr UiText Labels{L"Підписи", L"Labels", L"Подписи"};
inline constexpr UiText Size{L"Розмір", L"Size", L"Размер"};
inline constexpr UiText Spacing{L"Проміжки", L"Spacing", L"Интервалы"};
inline constexpr UiText TileHalo{L"Кольоровий ореол при наведенні", L"Colored glow on hover", L"Цветной ореол при наведении"};
inline constexpr UiText WindowActions{L"Дії вікна", L"Window actions", L"Действия окна"};
inline constexpr UiText StartInTray{L"Автозапуск із Windows у трей", L"Start with Windows in the tray", L"Запуск с Windows в трее"};
inline constexpr UiText MotionAndPower{L"Анімації та живлення", L"Animation and power", L"Анимация и питание"};
inline constexpr UiText Animations{L"Анімації", L"Animations", L"Анимации"};
inline constexpr UiText BatteryHint{L"Доступно на пристроях із вбудованим акумулятором", L"Available on devices with a built-in battery", L"Доступно на устройствах со встроенной батареей"};
inline constexpr UiText MotionPaused{L"Анімації призупинено", L"Animations paused", L"Анимации приостановлены"};
inline constexpr UiText PauseOnBattery{L"Вимикати анімації від акумулятора", L"Pause animations on battery", L"Отключать анимации от батареи"};
inline constexpr UiText SaveBackup{L"Зберегти копію…", L"Save backup…", L"Сохранить копию…"};
inline constexpr UiText RestoreBackup{L"Відновити з копії…", L"Restore backup…", L"Восстановить из копии…"};
inline constexpr UiText BackupContents{L"Ярлики, іконки, обкладинки й налаштування", L"Shortcuts, icons, covers and settings", L"Ярлыки, иконки, обложки и настройки"};
inline constexpr UiText NameAndArtwork{L"Назва та зображення", L"Name and artwork", L"Название и изображения"};
inline constexpr UiText CropCover{L"Кадрувати обкладинку", L"Crop cover", L"Кадрировать обложку"};
inline constexpr UiText ShortcutDidNotOpen{L"Ярлик не відкрився", L"Couldn't open shortcut", L"Ярлык не открылся"};
inline constexpr UiText RemoveShortcutQuestion{L"Видалити ярлик?", L"Remove shortcut?", L"Удалить ярлык?"};
inline constexpr UiText RestoreBackupQuestion{L"Відновити резервну копію?", L"Restore backup?", L"Восстановить резервную копию?"};
inline constexpr UiText ShortcutLibrary{L"Бібліотека ярликів", L"Shortcut library", L"Библиотека ярлыков"};
inline constexpr UiText Name{L"Назва", L"Name", L"Название"};
inline constexpr UiText ChangeIcon{L"Змінити іконку…", L"Change icon…", L"Сменить иконку…"};
inline constexpr UiText IconFormats{L"З PNG, ICO або EXE", L"From PNG, ICO, or EXE", L"Из PNG, ICO или EXE"};
inline constexpr UiText RestoreDefault{L"Відновити стандартну", L"Restore default", L"Восстановить стандартную"};
inline constexpr UiText EditCover{L"Редагувати обкладинку…", L"Edit cover…", L"Изменить обложку…"};
inline constexpr UiText AddCover{L"Додати обкладинку…", L"Add cover…", L"Добавить обложку…"};
inline constexpr UiText Remove{L"Видалити", L"Remove", L"Удалить"};
inline constexpr UiText CoverPendingRemoval{L"Обкладинку буде видалено після збереження.", L"The cover will be removed when you save.", L"Обложка будет удалена после сохранения."};
inline constexpr UiText CoverStyleHint{L"Для стилю «На всю плитку»", L"Used in the Full tile style", L"Для стиля «На всю плитку»"};
inline constexpr UiText Cancel{L"Скасувати", L"Cancel", L"Отмена"};
inline constexpr UiText Save{L"Зберегти", L"Save", L"Сохранить"};
inline constexpr UiText RemoveLibraryCopy{L"Буде видалено лише копію в «Полиці».", L"This removes only the copy in Polytsia.", L"Будет удалена только копия в «Полиця»."};
inline constexpr UiText Restore{L"Відновити", L"Restore", L"Восстановить"};
inline constexpr UiText Close{L"Закрити", L"Close", L"Закрыть"};
inline constexpr UiText ChangeTarget{L"Змінити файл запуску…", L"Change launch target…", L"Изменить файл запуска…"};
inline constexpr UiText Okay{L"Гаразд", L"OK", L"OK"};
inline constexpr UiText CoverFormats{L"PNG або JPG", L"PNG or JPG", L"PNG или JPG"};
inline constexpr UiText DragCrop{L"Перетягни зображення, щоб змінити кадр", L"Drag the image to adjust the crop", L"Перетащи изображение, чтобы изменить кадр"};
inline constexpr UiText PickOrDropCover{L"Вибери або перетягни зображення", L"Choose or drop an image", L"Выбери или перетащи изображение"};
inline constexpr UiText Zoom{L"Масштаб ", L"Zoom ", L"Масштаб "};
inline constexpr UiText Reset{L"Скинути", L"Reset", L"Сбросить"};
inline constexpr UiText ChooseFile{L"Вибрати файл…", L"Choose file…", L"Выбрать файл…"};
inline constexpr UiText RemoveCover{L"Видалити обкладинку", L"Remove cover", L"Удалить обложку"};
inline constexpr UiText Apply{L"Застосувати", L"Apply", L"Применить"};
inline constexpr UiText ReadCurrentCoverError{L"Не вдалося відкрити поточну обкладинку.", L"Couldn't open the current cover.", L"Не удалось открыть текущую обложку."};
inline constexpr UiText ReadCoverError{L"Не вдалося відкрити обкладинку. Вибери PNG або JPG.", L"Couldn't open the cover. Choose a PNG or JPG.", L"Не удалось открыть обложку. Выбери PNG или JPG."};
inline constexpr UiText SaveLibraryError{L"Не вдалося зберегти бібліотеку.", L"Couldn't save the library.", L"Не удалось сохранить библиотеку."};
inline constexpr UiText AddedCount{L"Додано: ", L"Added: ", L"Добавлено: "};
inline constexpr UiText FailedCount{L". Не вдалося додати: ", L". Failed to add: ", L". Не удалось добавить: "};
inline constexpr UiText ShortcutFormatsHint{L". Вибери доступні файли .lnk, .url або .exe.", L". Choose accessible .lnk, .url, or .exe files.", L". Выбери доступные файлы .lnk, .url или .exe."};
inline constexpr UiText DuplicateCount{L" Уже в бібліотеці: ", L" Already in the library: ", L" Уже в библиотеке: "};
inline constexpr UiText ShortcutType{L"Ярлики та програми (LNK, URL, EXE)", L"Shortcuts and programs (LNK, URL, EXE)", L"Ярлыки и программы (LNK, URL, EXE)"};
inline constexpr UiText AddShortcutDialog{L"Додати ярлики або програми", L"Add shortcuts or programs", L"Добавить ярлыки или программы"};
inline constexpr UiText SelectTarget{L"Вибрати файл запуску", L"Choose launch target", L"Выбрать файл запуска"};
inline constexpr UiText IconType{L"Іконки та програми (PNG, ICO, EXE)", L"Icons and programs (PNG, ICO, EXE)", L"Иконки и программы (PNG, ICO, EXE)"};
inline constexpr UiText SelectIcon{L"Вибрати іконку", L"Choose icon", L"Выбрать иконку"};
inline constexpr UiText CoverType{L"Обкладинки (PNG, JPG)", L"Covers (PNG, JPG)", L"Обложки (PNG, JPG)"};
inline constexpr UiText SelectCover{L"Вибрати обкладинку", L"Choose cover", L"Выбрать обложку"};
inline constexpr UiText BackupType{L"Резервна копія «Полиці»", L"Polytsia backup", L"Резервная копия «Полиця»"};
inline constexpr UiText SelectBackup{L"Вибрати резервну копію", L"Choose backup", L"Выбрать резервную копию"};
inline constexpr UiText BackupFilename{L"Полиця.polytsia", L"Polytsia.polytsia", L"Полиця.polytsia"};
inline constexpr UiText SaveBackupDialog{L"Зберегти резервну копію", L"Save backup", L"Сохранить резервную копию"};
inline constexpr UiText Open{L"Відкрити", L"Open", L"Открыть"};
inline constexpr UiText EditShortcut{L"Назва та зображення…", L"Name and artwork…", L"Название и изображения…"};
inline constexpr UiText MoveEarlier{L"Перемістити раніше", L"Move earlier", L"Переместить раньше"};
inline constexpr UiText MoveLater{L"Перемістити пізніше", L"Move later", L"Переместить позже"};
inline constexpr UiText RemoveShortcut{L"Видалити з бібліотеки…", L"Remove from library…", L"Удалить из библиотеки…"};
inline constexpr UiText Search{L"Пошук", L"Search", L"Поиск"};
inline constexpr UiText RunAsAdmin{L"Запустити від адміністратора", L"Run as administrator", L"Запустить от администратора"};
inline constexpr UiText OpenLocation{L"Відкрити розташування файлу", L"Open file location", L"Открыть расположение файла"};
inline constexpr UiText SortByUse{L"Сортувати за частотою запуску", L"Sort by most used", L"Сортировать по частоте запуска"};
inline constexpr UiText TargetNotFound{L"Не вдалося знайти файл, на який вказує ярлик.", L"Couldn't find the file this shortcut points to.", L"Не удалось найти файл, на который указывает ярлык."};
inline constexpr UiText SortUnchanged{L"Порядок уже відповідає частоті запусків.", L"The order already matches how often you open each shortcut.", L"Порядок уже соответствует частоте запусков."};
inline constexpr UiText SortNoLaunches{L"Статистики запусків ще немає. «Полиця» рахує запуски, починаючи з версії 1.3.0: відкрий кілька ярликів і спробуй знову.", L"There are no launch counts yet. Polytsia counts launches starting with version 1.3.0, so open a few shortcuts and try again.", L"Статистики запусков ещё нет. «Полиця» считает запуски, начиная с версии 1.3.0: открой несколько ярлыков и попробуй снова."};
inline constexpr UiText GlobalHotkey{L"Викликати вікно клавішами", L"Show window shortcut", L"Вызывать окно клавишами"};
inline constexpr UiText HotkeyOff{L"Вимкнено", L"Off", L"Выключено"};
inline constexpr UiText HotkeyCtrlAltP{L"Ctrl+Alt+P", L"Ctrl+Alt+P", L"Ctrl+Alt+P"};
inline constexpr UiText HotkeyCtrlAltG{L"Ctrl+Alt+G", L"Ctrl+Alt+G", L"Ctrl+Alt+G"};
inline constexpr UiText HotkeyCtrlAltF12{L"Ctrl+Alt+F12", L"Ctrl+Alt+F12", L"Ctrl+Alt+F12"};
inline constexpr UiText HotkeyTaken{L"Це сполучення вже використовує інша програма. Вибери інше.", L"Another app is already using this shortcut. Choose a different one.", L"Это сочетание уже использует другая программа. Выбери другое."};
inline constexpr UiText Startup{L"Запуск", L"Startup", L"Запуск"};
inline constexpr UiText Data{L"Дані", L"Data", L"Данные"};
inline constexpr UiText DataFolderHint{L"Бібліотека, налаштування й журнал помилок (log.txt)", L"Library, settings, and the error log (log.txt)", L"Библиотека, настройки и журнал ошибок (log.txt)"};
inline constexpr UiText OpenDataFolder{L"Відкрити папку даних", L"Open data folder", L"Открыть папку данных"};
inline constexpr UiText NewerLibraryError{L"Бібліотеку створено новішою версією «Полиці». Онови програму, щоб відкрити її.", L"The library was created by a newer version of Polytsia. Update the app to open it.", L"Библиотека создана более новой версией «Полиця». Обнови программу, чтобы открыть её."};
inline constexpr UiText NewerBackupError{L"Цю копію створено новішою версією «Полиці». Онови програму, щоб відновити її.", L"This backup was made by a newer version of Polytsia. Update the app to restore it.", L"Эта копия создана более новой версией «Полиця». Обнови программу, чтобы восстановить её."};
inline constexpr UiText AddShortcuts{L"Додати ярлики…", L"Add shortcuts…", L"Добавить ярлыки…"};
inline constexpr UiText Exit{L"Вийти", L"Exit", L"Выход"};
inline constexpr UiText ToTray{L"У трей", L"To tray", L"В трей"};
inline constexpr UiText KeepOpen{L"Залишити відкритим", L"Keep open", L"Оставить открытым"};
inline constexpr UiText ToTaskbar{L"На панель завдань", L"To taskbar", L"На панель задач"};
inline constexpr UiText AfterLaunch{L"Після відкриття ярлика", L"After opening a shortcut", L"После открытия ярлыка"};
inline constexpr UiText WhenMinimized{L"При згортанні", L"When minimized", L"При сворачивании"};
inline constexpr UiText WhenClosed{L"При закритті вікна", L"When closing the window", L"При закрытии окна"};
inline constexpr UiText Sky{L"Блакитна", L"Sky", L"Голубая"};
inline constexpr UiText Mint{L"М’ятна", L"Mint", L"Мятная"};
inline constexpr UiText Peach{L"Персикова", L"Peach", L"Персиковая"};
inline constexpr UiText Lavender{L"Лавандова", L"Lavender", L"Лавандовая"};
inline constexpr UiText Cream{L"Кремова", L"Cream", L"Кремовая"};
inline constexpr UiText Rose{L"Трояндова", L"Rose", L"Розовая"};
inline constexpr UiText Lemon{L"Лимонна", L"Lemon", L"Лимонная"};
inline constexpr UiText Turquoise{L"Бірюзова", L"Turquoise", L"Бирюзовая"};
inline constexpr UiText Pistachio{L"Фісташкова", L"Pistachio", L"Фисташковая"};
inline constexpr UiText Porcelain{L"Порцелянова", L"Porcelain", L"Фарфоровая"};
inline constexpr UiText Indigo{L"Індиго", L"Indigo", L"Индиго"};
inline constexpr UiText Emerald{L"Смарагд", L"Emerald", L"Изумруд"};
inline constexpr UiText Bronze{L"Бронза", L"Bronze", L"Бронза"};
inline constexpr UiText Plum{L"Слива", L"Plum", L"Слива"};
inline constexpr UiText Graphite{L"Графіт", L"Graphite", L"Графит"};
inline constexpr UiText Ruby{L"Рубін", L"Ruby", L"Рубин"};
inline constexpr UiText Ocean{L"Океан", L"Ocean", L"Океан"};
inline constexpr UiText Olive{L"Олива", L"Olive", L"Олива"};
inline constexpr UiText Amber{L"Бурштин", L"Amber", L"Янтарь"};
inline constexpr UiText Midnight{L"Опівніч", L"Midnight", L"Полночь"};
inline constexpr UiText ChangeTargetError{L"Не вдалося змінити файл запуску. Вибери доступний файл .lnk, .url або .exe.", L"Couldn't change the launch target. Choose an accessible .lnk, .url, or .exe file.", L"Не удалось изменить файл запуска. Выбери доступный файл .lnk, .url или .exe."};
inline constexpr UiText SaveSettingsError{L"Не вдалося зберегти налаштування.", L"Couldn't save the settings.", L"Не удалось сохранить настройки."};
inline constexpr UiText NoExeIcon{L"У файлі EXE не вдалося знайти іконку.", L"Couldn't find an icon in the EXE file.", L"Не удалось найти иконку в файле EXE."};
inline constexpr UiText ReadIconError{L"Не вдалося прочитати PNG або ICO.", L"Couldn't read the PNG or ICO file.", L"Не удалось прочитать файл PNG или ICO."};
inline constexpr UiText EnterName{L"Введи назву ярлика.", L"Enter a name for the shortcut.", L"Введи название ярлыка."};
inline constexpr UiText SaveChangesError{L"Не вдалося зберегти зміни. Перевір файли зображень.", L"Couldn't save the changes. Check the image files.", L"Не удалось сохранить изменения. Проверь файлы изображений."};
inline constexpr UiText WaitForRestore{L"Зачекай, поки відкриється ярлик, і повтори відновлення.", L"Wait for the shortcut to open, then retry the restore.", L"Подожди, пока откроется ярлык, и повтори восстановление."};
inline constexpr UiText RestoreSuccess{L"Бібліотеку й налаштування відновлено.", L"Library and settings restored.", L"Библиотека и настройки восстановлены."};
inline constexpr UiText RestoreError{L"Не вдалося відновити резервну копію. Поточну бібліотеку збережено.", L"Couldn't restore the backup. Your current library is intact.", L"Не удалось восстановить резервную копию. Текущая библиотека сохранена."};
inline constexpr UiText RemoveShortcutError{L"Не вдалося видалити ярлик з бібліотеки.", L"Couldn't remove the shortcut from the library.", L"Не удалось удалить ярлык из библиотеки."};
inline constexpr UiText AutostartError{L"Не вдалося змінити автозапуск. Перевір, чи доступний запис автозапуску Windows і чи шлях до програми не задовгий.", L"Couldn't change startup. Check access to the Windows startup registry entry and the length of the app path.", L"Не удалось изменить автозапуск. Проверь доступ к записи автозапуска Windows и длину пути к программе."};
inline constexpr UiText BackupSuccess{L"Резервну копію збережено.", L"Backup saved.", L"Резервная копия сохранена."};
inline constexpr UiText BackupError{L"Не вдалося створити копію. Перевір доступ до папки та файлів бібліотеки.", L"Couldn't create the backup. Check access to the folder and library files.", L"Не удалось создать копию. Проверь доступ к папке и файлам библиотеки."};
inline constexpr UiText RestoreConfirmation{L"Копія замінить поточні ярлики, зображення й налаштування. За потреби спершу збережи поточну бібліотеку.", L"The backup will replace your current shortcuts, images, and settings. Save a copy of your current library first if needed.", L"Копия заменит текущие ярлыки, изображения и настройки. При необходимости сначала сохрани текущую библиотеку."};
inline constexpr UiText SaveOrderError{L"Не вдалося зберегти порядок ярликів.", L"Couldn't save the shortcut order.", L"Не удалось сохранить порядок ярлыков."};
inline constexpr UiText OpenApp{L"Відкрити «Полицю»", L"Open Polytsia", L"Открыть «Полиця»"};
inline constexpr UiText LaunchError{L"Windows не вдалося відкрити цей ярлик.", L"Windows couldn't open this shortcut.", L"Windows не удалось открыть этот ярлык."};
inline constexpr UiText OpenLibraryError{L"Не вдалося відкрити локальну бібліотеку ярликів.", L"Couldn't open the local shortcut library.", L"Не удалось открыть локальную библиотеку ярлыков."};
}
