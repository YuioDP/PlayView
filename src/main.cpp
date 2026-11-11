#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <commdlg.h>
#include <wincodec.h>
#include <filesystem>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iterator>
#include <memory>
#include <sstream>
#include <iomanip>
#include "resource.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kAppName[] = L"PlayView";
constexpr wchar_t kWindowClass[] = L"PlayViewMainWindow";
constexpr wchar_t kPipWindowClass[] = L"PlayViewPictureInPictureWindow";
constexpr UINT WM_APP_FRAME = WM_APP + 1;
constexpr UINT WM_APP_VIDEO_READY = WM_APP + 2;
constexpr UINT WM_APP_VIDEO_LOST = WM_APP + 3;
constexpr UINT WM_APP_AUDIO_LOST = WM_APP + 4;
constexpr UINT WM_APP_REBUILD_MENU = WM_APP + 5;
constexpr UINT_PTR TIMER_RECONNECT = 1;
constexpr UINT_PTR TIMER_OVERLAY = 2;

constexpr int IDC_VIDEO_DEVICE = 1001;
constexpr int IDC_AUDIO_DEVICE = 1002;
constexpr int IDC_RESOLUTION = 1003;
constexpr int IDC_RELOAD = 1004;
constexpr int IDC_VOLUME = 1005;
constexpr int IDC_MUTE = 1006;
constexpr int IDC_PIP = 1009;
constexpr int IDC_SYNC = 1008;
constexpr int IDM_VIEW_WINDOW = 2001;
constexpr int IDM_VIEW_FULLSCREEN = 2003;
constexpr int IDM_SHOW_TOOLBAR = 2006;
constexpr int IDM_EXIT = 2004;
constexpr int IDM_ABOUT = 2005;
constexpr int IDM_VIEW_PIP = 2010;
constexpr int IDM_PIP_TOPMOST = 2011;
constexpr int IDM_PROFILE_SAVE = 2100;
constexpr int IDM_PROFILE_DEFAULT = 2101;
constexpr int IDM_PROFILE_LOAD_BASE = 2200;
constexpr int IDM_PROFILE_SHORTCUT_BASE = 2300;
constexpr int IDM_PROFILE_DELETE_BASE = 2400;
constexpr int IDM_PROFILE_ICON_BASE = 2500;
constexpr int IDM_PIP_CLOSE = 2600;
constexpr int ID_PROFILE_MENU_LIMIT = 100;

struct DeviceInfo {
    std::wstring name;
    std::wstring id;
};

struct VideoMode {
    UINT32 width = 0;
    UINT32 height = 0;
};

struct Settings {
    std::wstring videoId;
    std::wstring videoName;
    std::wstring audioId;
    std::wstring audioName;
    int width = 0;
    int height = 0;
    int volume = 80;
    bool muted = false;
    int windowMode = 0; // 0 windowed, 2 fullscreen (1 was the removed borderless mode)
    bool showToolbar = true;
    bool windowMaximized = false;
    int windowX = 100;
    int windowY = 100;
    int windowWidth = 1180;
    int windowHeight = 760;
};

struct ProfileEntry {
    std::wstring name;
    std::filesystem::path path;
    std::filesystem::path iconPath;
};

struct FrameData {
    std::vector<BYTE> pixels;
    int width = 0;
    int height = 0;
};

struct PaintBuffer {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    HBRUSH backgroundBrush = nullptr;
    int width = 0;
    int height = 0;
};

HWND g_hwnd = nullptr;
HWND g_pipHwnd = nullptr;
HWND g_videoCombo = nullptr;
HWND g_audioCombo = nullptr;
HWND g_resolutionCombo = nullptr;
HWND g_volumeSlider = nullptr;
HWND g_volumeTooltip = nullptr;
HWND g_muteButton = nullptr;
std::wstring g_volumeTooltipText;
bool g_volumeTooltipVisible = false;
HFONT g_uiFont = nullptr;
HMENU g_appMenu = nullptr;
PaintBuffer g_mainPaintBuffer;
PaintBuffer g_pipPaintBuffer;

Settings g_settings;
std::vector<DeviceInfo> g_videoDevices;
std::vector<DeviceInfo> g_audioDevices;
std::vector<VideoMode> g_videoModes;
std::mutex g_frameMutex;
FrameData g_latestFrame;
std::vector<BYTE> g_pipDimFrameScratch;
std::atomic_bool g_frameMessagePending{ false };
std::atomic_int g_volume{ 80 };
std::atomic_bool g_muted{ false };
std::atomic_bool g_videoHasFrame{ false };
std::atomic<ULONGLONG> g_lastVideoFrameTick{ 0 };
ULONGLONG g_videoStartTick = 0;
std::wstring g_videoStatus = L"Selecciona una capturadora para comenzar.";
std::wstring g_audioStatus = L"Audio sin configurar.";
std::wstring g_statusText;
ULONGLONG g_nextVideoRetry = 0;
ULONGLONG g_nextAudioRetry = 0;
RECT g_restoreRect{ 100, 100, 1280, 860 };
int g_windowMode = 0;
bool g_windowMaximized = false;
bool g_isChangingWindowMode = false;
bool g_preserveRestoreRect = false;
// Keep the complete native placement captured before entering fullscreen.
// WINDOWPLACEMENT::rcNormalPosition uses workspace coordinates; restoring it
// with SetWindowPos (screen coordinates) can produce a mismatched size/state.
WINDOWPLACEMENT g_preFullscreenPlacement{};
bool g_hasPreFullscreenPlacement = false;
bool g_overlayVisible = true;
bool g_pipHover = false;
std::filesystem::path g_appDirectory;
std::filesystem::path g_settingsPath;
std::filesystem::path g_logPath;
std::filesystem::path g_profilesDirectory;
std::filesystem::path g_profileIconsDirectory;
std::filesystem::path g_activeProfilePath;
std::wstring g_activeProfileName;
std::vector<ProfileEntry> g_profileMenuEntries;
bool g_pipAlwaysOnTop = true;

void RefreshDevices(bool initial = false);
void SetWindowMode(int mode);
void CreateAppMenu(HWND hwnd);
void TogglePipTopmost();
void UpdateOverlayVisibility(bool force = false);
void InvalidateVideoSurface(HWND target);
void CaptureNormalWindowPlacement();
void RequestMenuRebuild();
void ApplyLoadedWindowPreferences();
void DrawPipHoverActions(HDC dc, HWND hwnd);

void Log(const std::wstring& message) {
    if (g_logPath.empty()) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::wstringstream line;
    line << std::setfill(L'0') << L'[' << st.wYear << L'-' << std::setw(2) << st.wMonth
         << L'-' << std::setw(2) << st.wDay << L' ' << std::setw(2) << st.wHour
         << L':' << std::setw(2) << st.wMinute << L':' << std::setw(2) << st.wSecond
         << L"] " << message << L"\r\n";
    const std::wstring text = line.str();
    HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}

std::wstring GetLocalAppData() {
    wchar_t buffer[MAX_PATH]{};
    DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (count == 0 || count >= MAX_PATH) return L".";
    return buffer;
}

void InitializePaths() {
    g_appDirectory = std::filesystem::path(GetLocalAppData()) / L"PlayView";
    std::error_code ec;
    std::filesystem::create_directories(g_appDirectory / L"logs", ec);
    g_settingsPath = g_appDirectory / L"settings.ini";
    g_profilesDirectory = g_appDirectory / L"profiles";
    g_profileIconsDirectory = g_appDirectory / L"profiles-icons";
    std::filesystem::create_directories(g_profilesDirectory, ec);
    std::filesystem::create_directories(g_profileIconsDirectory, ec);
    g_logPath = g_appDirectory / L"logs" / L"playview.log";
}

std::wstring ReadIniString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback = L"") {
    wchar_t value[32768]{};
    GetPrivateProfileStringW(section, key, fallback, value, static_cast<DWORD>(std::size(value)), g_settingsPath.c_str());
    return value;
}

int ReadIniInt(const wchar_t* section, const wchar_t* key, int fallback) {
    return GetPrivateProfileIntW(section, key, fallback, g_settingsPath.c_str());
}

void WriteIniString(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), g_settingsPath.c_str());
}

std::filesystem::path CurrentSettingsFile() {
    return g_activeProfilePath.empty() ? g_settingsPath : g_activeProfilePath;
}

void CaptureNormalWindowPlacement() {
    if (!g_hwnd || g_windowMode != 0 || g_isChangingWindowMode) return;
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (!GetWindowPlacement(g_hwnd, &placement)) return;
    if (IsIconic(g_hwnd)) return;

    g_windowMaximized = placement.showCmd == SW_SHOWMAXIMIZED || IsZoomed(g_hwnd) != FALSE;
    const RECT normal = placement.rcNormalPosition;
    if (normal.right - normal.left >= 500 && normal.bottom - normal.top >= 300) {
        g_restoreRect = normal;
    }
}

void LoadSettingsFrom(const std::filesystem::path& path) {
    const auto previous = g_settingsPath;
    g_settingsPath = path;
    g_settings.videoId = ReadIniString(L"Devices", L"VideoId");
    g_settings.videoName = ReadIniString(L"Devices", L"VideoName");
    g_settings.audioId = ReadIniString(L"Devices", L"AudioId");
    g_settings.audioName = ReadIniString(L"Devices", L"AudioName");
    g_settings.width = ReadIniInt(L"Video", L"Width", 0);
    g_settings.height = ReadIniInt(L"Video", L"Height", 0);
    g_settings.volume = std::clamp(ReadIniInt(L"Audio", L"Volume", 80), 0, 100);
    g_settings.muted = ReadIniInt(L"Audio", L"Muted", 0) != 0;
    const int savedWindowMode = ReadIniInt(L"Window", L"Mode", 0);
    // Mode 1 used to mean borderless. It has been removed, so old settings
    // safely migrate to the regular windowed mode.
    g_settings.windowMode = (savedWindowMode == 2) ? 2 : 0;
    g_settings.showToolbar = ReadIniInt(L"Window", L"ShowToolbar", 1) != 0;
    g_settings.windowMaximized = ReadIniInt(L"Window", L"Maximized", 0) != 0;
    g_settings.windowX = ReadIniInt(L"Window", L"X", 100);
    g_settings.windowY = ReadIniInt(L"Window", L"Y", 100);
    g_settings.windowWidth = std::max(950, ReadIniInt(L"Window", L"Width", 1180));
    g_settings.windowHeight = std::max(440, ReadIniInt(L"Window", L"Height", 760));
    g_windowMaximized = g_settings.windowMaximized;
    g_restoreRect = { g_settings.windowX, g_settings.windowY,
        g_settings.windowX + g_settings.windowWidth, g_settings.windowY + g_settings.windowHeight };
    g_volume.store(g_settings.volume);
    g_muted.store(g_settings.muted);
    g_settingsPath = previous;
}

void LoadSettings() {
    LoadSettingsFrom(g_settingsPath);
}

void SaveSettingsTo(const std::filesystem::path& path) {
    CaptureNormalWindowPlacement();
    g_settings.windowMaximized = g_windowMaximized;
    g_settings.windowX = g_restoreRect.left;
    g_settings.windowY = g_restoreRect.top;
    g_settings.windowWidth = std::max(950L, g_restoreRect.right - g_restoreRect.left);
    g_settings.windowHeight = std::max(440L, g_restoreRect.bottom - g_restoreRect.top);
    const auto previous = g_settingsPath;
    g_settingsPath = path;
    WriteIniString(L"Devices", L"VideoId", g_settings.videoId);
    WriteIniString(L"Devices", L"VideoName", g_settings.videoName);
    WriteIniString(L"Devices", L"AudioId", g_settings.audioId);
    WriteIniString(L"Devices", L"AudioName", g_settings.audioName);
    WriteIniString(L"Video", L"Width", std::to_wstring(g_settings.width));
    WriteIniString(L"Video", L"Height", std::to_wstring(g_settings.height));
    WriteIniString(L"Audio", L"Volume", std::to_wstring(g_settings.volume));
    WriteIniString(L"Audio", L"Muted", g_settings.muted ? L"1" : L"0");
    WriteIniString(L"Window", L"Mode", std::to_wstring(g_windowMode));
    WriteIniString(L"Window", L"ShowToolbar", g_settings.showToolbar ? L"1" : L"0");
    WriteIniString(L"Window", L"Maximized", g_windowMaximized ? L"1" : L"0");
    WriteIniString(L"Window", L"X", std::to_wstring(g_restoreRect.left));
    WriteIniString(L"Window", L"Y", std::to_wstring(g_restoreRect.top));
    WriteIniString(L"Window", L"Width", std::to_wstring(g_restoreRect.right - g_restoreRect.left));
    WriteIniString(L"Window", L"Height", std::to_wstring(g_restoreRect.bottom - g_restoreRect.top));
    g_settingsPath = previous;
}

void SaveSettings() {
    SaveSettingsTo(CurrentSettingsFile());
}

std::vector<ProfileEntry> EnumerateProfiles() {
    std::vector<ProfileEntry> profiles;
    std::error_code ec;
    std::filesystem::directory_iterator iterator(g_profilesDirectory, ec);
    const std::filesystem::directory_iterator end;
    while (!ec && iterator != end) {
        const auto path = iterator->path();
        std::error_code typeError;
        if (iterator->is_regular_file(typeError) && !typeError &&
            _wcsicmp(path.extension().c_str(), L".ini") == 0) {
            ProfileEntry profile;
            profile.path = path;
            wchar_t value[32768]{};
            GetPrivateProfileStringW(L"Profile", L"Name", path.stem().c_str(), value,
                static_cast<DWORD>(std::size(value)), path.c_str());
            profile.name = value;
            wchar_t iconValue[32768]{};
            GetPrivateProfileStringW(L"Profile", L"IconPath", L"", iconValue,
                static_cast<DWORD>(std::size(iconValue)), path.c_str());
            if (iconValue[0]) profile.iconPath = iconValue;
            if (profile.name.empty()) profile.name = path.stem().wstring();
            profiles.push_back(std::move(profile));
        }
        iterator.increment(ec);
    }
    std::sort(profiles.begin(), profiles.end(), [](const ProfileEntry& a, const ProfileEntry& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return profiles;
}

std::wstring GetExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return path;
}

std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') result += L"\\\"";
        else result += ch;
    }
    result += L"\"";
    return result;
}

bool CreateProfileShortcut(const ProfileEntry& profile) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;

    ComPtr<IShellLinkW> link;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    const std::wstring executable = GetExecutablePath();
    if (executable.empty()) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = link->SetPath(executable.c_str());
    const std::wstring arguments = L"--profile " + QuoteArgument(profile.path.wstring());
    if (SUCCEEDED(hr)) hr = link->SetArguments(arguments.c_str());
    if (SUCCEEDED(hr)) hr = link->SetDescription((L"PlayView: " + profile.name).c_str());
    if (SUCCEEDED(hr)) hr = link->SetWorkingDirectory(std::filesystem::path(executable).parent_path().c_str());
    const std::wstring icon = profile.iconPath.empty() ? executable : profile.iconPath.wstring();
    if (SUCCEEDED(hr)) hr = link->SetIconLocation(icon.c_str(), 0);

    ComPtr<IPersistFile> persist;
    if (SUCCEEDED(hr)) hr = link.As(&persist);
    PWSTR desktopRaw = nullptr;
    if (SUCCEEDED(hr)) hr = SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_CREATE, nullptr, &desktopRaw);
    if (SUCCEEDED(hr)) {
        std::filesystem::path shortcutPath = std::filesystem::path(desktopRaw) / (profile.name + L".lnk");
        hr = persist->Save(shortcutPath.c_str(), TRUE);
    }
    if (desktopRaw) CoTaskMemFree(desktopRaw);
    if (uninitialize) CoUninitialize();
    return SUCCEEDED(hr);
}

#pragma pack(push, 1)
struct IconDirectoryHeader { WORD reserved; WORD type; WORD count; };
struct IconDirectoryEntry {
    BYTE width; BYTE height; BYTE colorCount; BYTE reserved;
    WORD planes; WORD bitCount; DWORD bytesInResource; DWORD imageOffset;
};
#pragma pack(pop)

bool ConvertImageToIco(const std::filesystem::path& sourcePath, const std::filesystem::path& destinationPath) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(sourcePath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return false;

    UINT originalWidth = 0, originalHeight = 0;
    hr = frame->GetSize(&originalWidth, &originalHeight);
    if (FAILED(hr) || originalWidth == 0 || originalHeight == 0) return false;

    constexpr UINT canvasSize = 256;
    const double scale = std::min(static_cast<double>(canvasSize) / originalWidth,
                                  static_cast<double>(canvasSize) / originalHeight);
    const UINT scaledWidth = std::max(1U, static_cast<UINT>(std::lround(originalWidth * scale)));
    const UINT scaledHeight = std::max(1U, static_cast<UINT>(std::lround(originalHeight * scale)));

    ComPtr<IWICBitmapScaler> scaler;
    hr = factory->CreateBitmapScaler(&scaler);
    if (FAILED(hr)) return false;
    hr = scaler->Initialize(frame.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant);
    if (FAILED(hr)) return false;

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return false;
    hr = converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    const UINT sourceStride = scaledWidth * 4;
    std::vector<BYTE> sourcePixels(static_cast<size_t>(sourceStride) * scaledHeight);
    hr = converter->CopyPixels(nullptr, sourceStride, static_cast<UINT>(sourcePixels.size()), sourcePixels.data());
    if (FAILED(hr)) return false;

    constexpr UINT destinationStride = canvasSize * 4;
    constexpr size_t xorBytes = static_cast<size_t>(destinationStride) * canvasSize;
    constexpr UINT maskStride = ((canvasSize + 31) / 32) * 4;
    constexpr size_t maskBytes = static_cast<size_t>(maskStride) * canvasSize;
    std::vector<BYTE> imageData(sizeof(BITMAPINFOHEADER) + xorBytes + maskBytes, 0);

    BITMAPINFOHEADER bitmapHeader{};
    bitmapHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapHeader.biWidth = canvasSize;
    bitmapHeader.biHeight = canvasSize * 2;
    bitmapHeader.biPlanes = 1;
    bitmapHeader.biBitCount = 32;
    bitmapHeader.biCompression = BI_RGB;
    bitmapHeader.biSizeImage = static_cast<DWORD>(xorBytes + maskBytes);
    std::memcpy(imageData.data(), &bitmapHeader, sizeof(bitmapHeader));

    const UINT offsetX = (canvasSize - scaledWidth) / 2;
    const UINT offsetY = (canvasSize - scaledHeight) / 2;
    BYTE* destinationPixels = imageData.data() + sizeof(BITMAPINFOHEADER);
    for (UINT y = 0; y < scaledHeight; ++y) {
        const UINT destinationY = canvasSize - 1 - (offsetY + y);
        BYTE* destinationRow = destinationPixels + static_cast<size_t>(destinationY) * destinationStride + offsetX * 4;
        const BYTE* sourceRow = sourcePixels.data() + static_cast<size_t>(y) * sourceStride;
        std::memcpy(destinationRow, sourceRow, static_cast<size_t>(scaledWidth) * 4);
    }

    IconDirectoryHeader directory{ 0, 1, 1 };
    IconDirectoryEntry entry{};
    entry.width = 0; // 0 represents 256 pixels in an ICO directory entry.
    entry.height = 0;
    entry.planes = 1;
    entry.bitCount = 32;
    entry.bytesInResource = static_cast<DWORD>(imageData.size());
    entry.imageOffset = sizeof(IconDirectoryHeader) + sizeof(IconDirectoryEntry);

    HANDLE output = CreateFileW(destinationPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(output, &directory, sizeof(directory), &written, nullptr) && written == sizeof(directory);
    if (ok) ok = WriteFile(output, &entry, sizeof(entry), &written, nullptr) && written == sizeof(entry);
    if (ok) ok = WriteFile(output, imageData.data(), static_cast<DWORD>(imageData.size()), &written, nullptr) &&
        written == imageData.size();
    CloseHandle(output);
    if (!ok) DeleteFileW(destinationPath.c_str());
    return ok;
}

bool PickIconFile(std::filesystem::path& selectedPath) {
    wchar_t file[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_hwnd;
    dialog.lpstrFilter = L"Imágenes e iconos (*.ico;*.png;*.jpg;*.jpeg;*.bmp)\0*.ico;*.png;*.jpg;*.jpeg;*.bmp\0Todos los archivos (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = static_cast<DWORD>(std::size(file));
    dialog.lpstrTitle = L"Seleccionar logo del perfil";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    dialog.lpstrDefExt = L"ico";
    if (!GetOpenFileNameW(&dialog)) return false;
    selectedPath = file;
    return true;
}

bool SaveProfileIcon(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (_wcsicmp(source.extension().c_str(), L".ico") == 0) {
        std::error_code ec;
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
        return !ec;
    }
    return ConvertImageToIco(source, destination);
}

void RebuildProfilesMenu(HMENU menu) {
    g_profileMenuEntries = EnumerateProfiles();
    HMENU profilesMenu = CreatePopupMenu();
    AppendMenuW(profilesMenu, MF_STRING, IDM_PROFILE_SAVE, L"Guardar configuración actual como perfil...");
    AppendMenuW(profilesMenu, MF_STRING, IDM_PROFILE_DEFAULT, L"Usar configuración principal");
    AppendMenuW(profilesMenu, MF_SEPARATOR, 0, nullptr);
    HMENU loadMenu = CreatePopupMenu();
    HMENU shortcutMenu = CreatePopupMenu();
    HMENU deleteMenu = CreatePopupMenu();
    HMENU iconMenu = CreatePopupMenu();
    if (g_profileMenuEntries.empty()) {
        AppendMenuW(loadMenu, MF_STRING | MF_GRAYED, 0, L"No hay perfiles guardados");
        AppendMenuW(shortcutMenu, MF_STRING | MF_GRAYED, 0, L"No hay perfiles guardados");
        AppendMenuW(deleteMenu, MF_STRING | MF_GRAYED, 0, L"No hay perfiles guardados");
        AppendMenuW(iconMenu, MF_STRING | MF_GRAYED, 0, L"No hay perfiles guardados");
    } else {
        const size_t count = std::min(g_profileMenuEntries.size(), static_cast<size_t>(ID_PROFILE_MENU_LIMIT));
        for (size_t i = 0; i < count; ++i) {
            const auto& profile = g_profileMenuEntries[i];
            const UINT loadId = IDM_PROFILE_LOAD_BASE + static_cast<UINT>(i);
            const UINT shortcutId = IDM_PROFILE_SHORTCUT_BASE + static_cast<UINT>(i);
            const UINT deleteId = IDM_PROFILE_DELETE_BASE + static_cast<UINT>(i);
            const UINT iconId = IDM_PROFILE_ICON_BASE + static_cast<UINT>(i);
            AppendMenuW(loadMenu, MF_STRING, loadId, profile.name.c_str());
            AppendMenuW(shortcutMenu, MF_STRING, shortcutId, profile.name.c_str());
            AppendMenuW(deleteMenu, MF_STRING, deleteId, profile.name.c_str());
            AppendMenuW(iconMenu, MF_STRING, iconId, profile.name.c_str());
        }
    }
    AppendMenuW(profilesMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(loadMenu), L"Cargar perfil");
    AppendMenuW(profilesMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(shortcutMenu), L"Crear acceso directo en el escritorio");
    AppendMenuW(profilesMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(iconMenu), L"Cambiar logo del perfil");
    AppendMenuW(profilesMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(deleteMenu), L"Eliminar perfil");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(profilesMenu), L"Perfiles");
}

void SaveCurrentAsProfile() {
    try {
        wchar_t file[MAX_PATH] = L"Nuevo perfil.ini";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = g_hwnd;
        dialog.lpstrFilter = L"Perfil PlayView (*.ini)\0*.ini\0";
        dialog.lpstrFile = file;
        dialog.nMaxFile = static_cast<DWORD>(std::size(file));
        dialog.lpstrInitialDir = g_profilesDirectory.c_str();
        dialog.lpstrTitle = L"Guardar perfil de PlayView";
        dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_EXPLORER;
        dialog.lpstrDefExt = L"ini";
        if (!GetSaveFileNameW(&dialog)) return;

        std::filesystem::path profilePath(file);
        if (profilePath.extension().empty()) profilePath += L".ini";
        std::error_code rootError, chosenError;
        const auto profileRoot = std::filesystem::weakly_canonical(g_profilesDirectory, rootError);
        const auto chosenRoot = std::filesystem::weakly_canonical(profilePath.parent_path(), chosenError);
        if (rootError || chosenError || CompareStringOrdinal(profileRoot.c_str(), -1,
            chosenRoot.c_str(), -1, TRUE) != CSTR_EQUAL) {
            MessageBoxW(g_hwnd, L"Los perfiles deben guardarse dentro de la carpeta de datos de PlayView.",
                L"PlayView", MB_OK | MB_ICONWARNING);
            return;
        }

        const std::wstring profileName = profilePath.stem().wstring();
        if (profileName.empty()) {
            MessageBoxW(g_hwnd, L"El nombre del perfil no es válido.", L"PlayView", MB_OK | MB_ICONWARNING);
            return;
        }

        std::filesystem::path iconSource;
        if (MessageBoxW(g_hwnd,
            L"¿Deseas asignar un logo personalizado a este perfil? Se admiten .ico, .png, .jpg, .jpeg y .bmp.",
            L"Logo del perfil", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            if (!PickIconFile(iconSource)) iconSource.clear();
        }

        std::filesystem::path iconDestination;
        if (!iconSource.empty()) {
            iconDestination = g_profileIconsDirectory / (profilePath.stem().wstring() + L".ico");
            if (!SaveProfileIcon(iconSource, iconDestination)) {
                MessageBoxW(g_hwnd, L"No se pudo copiar el icono. Se utilizará el icono predeterminado.",
                    L"PlayView", MB_OK | MB_ICONWARNING);
                iconDestination.clear();
            }
        }

        SaveSettingsTo(profilePath);
        WritePrivateProfileStringW(L"Profile", L"Name", profileName.c_str(), profilePath.c_str());
        WritePrivateProfileStringW(L"Profile", L"IconPath",
            iconDestination.empty() ? L"" : iconDestination.c_str(), profilePath.c_str());

        ProfileEntry profile{ profileName, profilePath, iconDestination };
        RequestMenuRebuild();
        if (MessageBoxW(g_hwnd,
            L"El perfil se guardó. ¿Deseas crear ahora un acceso directo en el escritorio con su nombre y logo?",
            L"Perfil creado", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            if (CreateProfileShortcut(profile))
                MessageBoxW(g_hwnd, L"Acceso directo creado en el escritorio.", L"PlayView", MB_OK | MB_ICONINFORMATION);
            else
                MessageBoxW(g_hwnd, L"No se pudo crear el acceso directo.", L"PlayView", MB_OK | MB_ICONWARNING);
        }
    } catch (const std::filesystem::filesystem_error& error) {
        (void)error;
        Log(L"Error de archivos al guardar perfil.");
        MessageBoxW(g_hwnd, L"Ocurrió un problema al guardar el perfil. Consulta playview.log para más detalles.",
            L"PlayView", MB_OK | MB_ICONERROR);
    } catch (const std::exception&) {
        Log(L"Error de ejecución al guardar un perfil.");
        MessageBoxW(g_hwnd, L"Ocurrió un error al guardar el perfil. La aplicación continuará abierta.",
            L"PlayView", MB_OK | MB_ICONERROR);
    } catch (...) {
        Log(L"Error inesperado al guardar un perfil.");
        MessageBoxW(g_hwnd, L"Ocurrió un error inesperado al guardar el perfil. La aplicación continuará abierta.",
            L"PlayView", MB_OK | MB_ICONERROR);
    }
}

std::wstring GetAttributeString(IMFAttributes* attributes, REFGUID key) {
    if (!attributes) return {};
    LPWSTR value = nullptr;
    UINT32 length = 0;
    if (FAILED(attributes->GetAllocatedString(key, &value, &length)) || !value) return {};
    std::wstring result(value, length);
    CoTaskMemFree(value);
    return result;
}

HRESULT CreateMediaSource(const std::wstring& deviceId, bool video, IMFMediaSource** source) {
    if (!source) return E_POINTER;
    *source = nullptr;
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 2);
    if (FAILED(hr)) return hr;
    hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        video ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
    if (FAILED(hr)) return hr;
    hr = attributes->SetString(video ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK
                                    : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID,
                              deviceId.c_str());
    if (FAILED(hr)) return hr;
    return MFCreateDeviceSource(attributes.Get(), source);
}

std::vector<DeviceInfo> EnumerateDevices(bool video) {
    std::vector<DeviceInfo> devices;
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) return devices;
    hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        video ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
    if (FAILED(hr)) return devices;

    IMFActivate** activations = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attributes.Get(), &activations, &count);
    if (FAILED(hr) || !activations) return devices;

    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IMFActivate> activation;
        activation.Attach(activations[i]);
        DeviceInfo entry;
        entry.name = GetAttributeString(activation.Get(), MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
        entry.id = GetAttributeString(activation.Get(), video ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK
                                                             : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID);
        if (!entry.name.empty() && !entry.id.empty()) devices.push_back(std::move(entry));
    }
    CoTaskMemFree(activations);
    return devices;
}

bool ContainsDevice(const std::vector<DeviceInfo>& devices, const std::wstring& id) {
    return std::any_of(devices.begin(), devices.end(), [&](const DeviceInfo& device) { return device.id == id; });
}

std::vector<VideoMode> EnumerateVideoModes(const std::wstring& deviceId) {
    std::vector<VideoMode> modes;
    ComPtr<IMFMediaSource> source;
    if (FAILED(CreateMediaSource(deviceId, true, &source))) return modes;
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader);
    if (FAILED(hr)) { source->Shutdown(); return modes; }

    for (DWORD index = 0; index < 128; ++index) {
        ComPtr<IMFMediaType> mediaType;
        hr = reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), index, &mediaType);
        if (hr == MF_E_NO_MORE_TYPES) break;
        if (FAILED(hr)) continue;
        UINT32 width = 0, height = 0;
        if (SUCCEEDED(MFGetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, &width, &height)) && width && height) {
            const bool exists = std::any_of(modes.begin(), modes.end(), [&](const VideoMode& m) { return m.width == width && m.height == height; });
            if (!exists) modes.push_back({ width, height });
        }
    }
    reader.Reset();
    source->Shutdown();
    std::sort(modes.begin(), modes.end(), [](const VideoMode& a, const VideoMode& b) {
        const uint64_t areaA = static_cast<uint64_t>(a.width) * a.height;
        const uint64_t areaB = static_cast<uint64_t>(b.width) * b.height;
        if (areaA != areaB) return areaA > areaB;
        return a.width > b.width;
    });
    return modes;
}

void UpdateStatusText() {
    g_statusText = g_videoStatus;
    if (!g_audioStatus.empty()) g_statusText += L"    |    " + g_audioStatus;
    // Status and toolbar labels are painted into the same video back buffer.
    // This avoids the opaque gray STATIC-control strip around the toolbar.
    InvalidateVideoSurface(g_hwnd);
}

class VideoCapture {
public:
    ~VideoCapture() { Stop(); }

    bool IsRunning() const { return running_.load(); }

    void Start(HWND notifyWindow, const std::wstring& deviceId, int width, int height) {
        Stop();
        if (deviceId.empty()) return;
        stopRequested_.store(false);
        running_.store(true);
        worker_ = std::thread([this, notifyWindow, deviceId, width, height]() {
            Run(notifyWindow, deviceId, width, height);
        });
    }

    void Stop() {
        stopRequested_.store(true);
        if (worker_.joinable()) {
            // Best-effort cancellation for a driver that has stalled inside
            // the synchronous Source Reader call.
            CancelSynchronousIo(worker_.native_handle());
            worker_.join();
        }
        running_.store(false);
    }

private:
    void Run(HWND notifyWindow, const std::wstring& deviceId, int requestedWidth, int requestedHeight) {
        const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ComPtr<IMFMediaSource> source;
        ComPtr<IMFSourceReader> reader;
        HRESULT hr = CreateMediaSource(deviceId, true, &source);
        if (SUCCEEDED(hr)) {
            ComPtr<IMFAttributes> readerAttributes;
            hr = MFCreateAttributes(&readerAttributes, 2);
            if (SUCCEEDED(hr)) hr = readerAttributes->SetUINT32(MF_LOW_LATENCY, TRUE);
            // Keep the advanced Source Reader processor enabled for real-time
            // format conversion. It supports the conversion/deinterlace path
            // and can use a registered hardware video processor when available.
            if (SUCCEEDED(hr)) hr = readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
            if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source.Get(), readerAttributes.Get(), &reader);
        }

        if (SUCCEEDED(hr)) {
            ComPtr<IMFMediaType> requestedType;
            hr = MFCreateMediaType(&requestedType);
            if (SUCCEEDED(hr)) hr = requestedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(hr)) hr = requestedType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            if (SUCCEEDED(hr) && requestedWidth > 0 && requestedHeight > 0)
                hr = MFSetAttributeSize(requestedType.Get(), MF_MT_FRAME_SIZE, requestedWidth, requestedHeight);
            if (SUCCEEDED(hr)) hr = requestedType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, requestedType.Get());
        }

        UINT32 frameWidth = 0, frameHeight = 0;
        LONG stride = 0;
        if (SUCCEEDED(hr)) {
            ComPtr<IMFMediaType> currentType;
            hr = reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &currentType);
            if (SUCCEEDED(hr)) hr = MFGetAttributeSize(currentType.Get(), MF_MT_FRAME_SIZE, &frameWidth, &frameHeight);
            UINT32 strideValue = 0;
            if (SUCCEEDED(hr) && SUCCEEDED(currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideValue))) stride = static_cast<LONG>(strideValue);
            if (stride == 0) stride = static_cast<LONG>(frameWidth * 4);
        }

        if (FAILED(hr) || frameWidth == 0 || frameHeight == 0) {
            Log(L"No se pudo iniciar la captura de video. HRESULT=" + std::to_wstring(static_cast<unsigned long>(hr)));
            PostMessageW(notifyWindow, WM_APP_VIDEO_LOST, 0, 0);
            reader.Reset();
            if (source) source->Shutdown();
            running_.store(false);
            if (SUCCEEDED(comHr)) CoUninitialize();
            return;
        }

        bool signaledReady = false;
        FrameData nextFrame;
        while (!stopRequested_.load()) {
            DWORD streamIndex = 0, flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &streamIndex, &flags, &timestamp, &sample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) break;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
                ComPtr<IMFMediaType> changedType;
                UINT32 changedWidth = 0, changedHeight = 0;
                if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &changedType)) &&
                    SUCCEEDED(MFGetAttributeSize(changedType.Get(), MF_MT_FRAME_SIZE, &changedWidth, &changedHeight)) &&
                    changedWidth > 0 && changedHeight > 0) {
                    frameWidth = changedWidth;
                    frameHeight = changedHeight;
                    UINT32 changedStride = 0;
                    stride = SUCCEEDED(changedType->GetUINT32(MF_MT_DEFAULT_STRIDE, &changedStride))
                        ? static_cast<LONG>(changedStride) : static_cast<LONG>(frameWidth * 4);
                }
            }
            if (!sample) { Sleep(1); continue; }

            ComPtr<IMFMediaBuffer> buffer;
            hr = sample->ConvertToContiguousBuffer(&buffer);
            if (FAILED(hr)) continue;
            const size_t rowBytes = static_cast<size_t>(frameWidth) * 4;
            const size_t frameBytes = rowBytes * frameHeight;
            nextFrame.width = static_cast<int>(frameWidth);
            nextFrame.height = static_cast<int>(frameHeight);
            if (nextFrame.pixels.size() != frameBytes) nextFrame.pixels.resize(frameBytes);
            bool copiedFrame = false;

            // Prefer IMF2DBuffer::Lock2D. It supplies the actual first scanline
            // and signed pitch, avoiding assumptions about the driver's layout.
            ComPtr<IMF2DBuffer> buffer2D;
            if (SUCCEEDED(buffer.As(&buffer2D))) {
                BYTE* scanline0 = nullptr;
                LONG pitch = 0;
                if (SUCCEEDED(buffer2D->Lock2D(&scanline0, &pitch))) {
                    const size_t absPitch = static_cast<size_t>(pitch < 0 ? -static_cast<int64_t>(pitch) : pitch);
                    if (scanline0 && absPitch >= rowBytes && rowBytes <= MAXDWORD) {
                        copiedFrame = SUCCEEDED(MFCopyImage(nextFrame.pixels.data(),
                            static_cast<LONG>(rowBytes), scanline0, pitch,
                            static_cast<DWORD>(rowBytes), frameHeight));
                    }
                    buffer2D->Unlock2D();
                }
            }

            // Some drivers expose only IMFMediaBuffer::Lock. Keep a guarded
            // fallback for them, but do the copy outside the paint mutex.
            if (!copiedFrame) {
                BYTE* data = nullptr;
                DWORD maxLength = 0, currentLength = 0;
                hr = buffer->Lock(&data, &maxLength, &currentLength);
                if (SUCCEEDED(hr)) {
                    const size_t absStride = static_cast<size_t>(stride < 0
                        ? -static_cast<int64_t>(stride) : stride);
                    const size_t requiredBytes = absStride * frameHeight;
                    if (data && absStride >= rowBytes && currentLength >= requiredBytes) {
                        for (UINT32 row = 0; row < frameHeight; ++row) {
                            const UINT32 sourceRow = stride < 0 ? (frameHeight - 1 - row) : row;
                            std::memcpy(nextFrame.pixels.data() + static_cast<size_t>(row) * rowBytes,
                                data + static_cast<size_t>(sourceRow) * absStride, rowBytes);
                        }
                        copiedFrame = true;
                    }
                    buffer->Unlock();
                }
            }

            if (copiedFrame) {
                {
                    std::lock_guard<std::mutex> lock(g_frameMutex);
                    g_latestFrame.width = nextFrame.width;
                    g_latestFrame.height = nextFrame.height;
                    // Swap rather than resize/copy under the mutex. The UI can
                    // finish drawing the previous frame before the next one is published.
                    g_latestFrame.pixels.swap(nextFrame.pixels);
                }
                g_videoHasFrame.store(true);
                g_lastVideoFrameTick.store(GetTickCount64());
                if (!signaledReady) {
                    signaledReady = true;
                    PostMessageW(notifyWindow, WM_APP_VIDEO_READY, 0, 0);
                }
                if (!g_frameMessagePending.exchange(true)) PostMessageW(notifyWindow, WM_APP_FRAME, 0, 0);
            }
        }

        reader.Reset();
        if (source) source->Shutdown();
        if (!stopRequested_.load()) {
            Log(L"La captura de video se interrumpio o perdio el dispositivo.");
            PostMessageW(notifyWindow, WM_APP_VIDEO_LOST, 0, 0);
        }
        running_.store(false);
        if (SUCCEEDED(comHr)) CoUninitialize();
    }

    std::thread worker_;
    std::atomic_bool stopRequested_{ false };
    std::atomic_bool running_{ false };
};

class AudioCapture {
public:
    ~AudioCapture() { Stop(); }
    bool IsRunning() const { return running_.load(); }

    void Start(HWND notifyWindow, const std::wstring& endpointId) {
        Stop();
        if (endpointId.empty()) return;
        stopRequested_.store(false);
        running_.store(true);
        worker_ = std::thread([this, notifyWindow, endpointId]() { Run(notifyWindow, endpointId); });
    }

    void Stop() {
        stopRequested_.store(true);
        if (worker_.joinable()) {
            CancelSynchronousIo(worker_.native_handle());
            worker_.join();
        }
        running_.store(false);
    }

private:
    struct QueuedAudio {
        std::vector<char> bytes;
        WAVEHDR header{};
    };

    void Run(HWND notifyWindow, const std::wstring& endpointId) {
        const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ComPtr<IMFMediaSource> source;
        ComPtr<IMFSourceReader> reader;
        HRESULT hr = CreateMediaSource(endpointId, false, &source);
        if (SUCCEEDED(hr)) {
            ComPtr<IMFAttributes> readerAttributes;
            hr = MFCreateAttributes(&readerAttributes, 2);
            if (SUCCEEDED(hr)) hr = readerAttributes->SetUINT32(MF_LOW_LATENCY, TRUE);
            if (SUCCEEDED(hr)) hr = readerAttributes->SetString(MF_READWRITE_MMCSS_CLASS_AUDIO, L"Pro Audio");
            if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source.Get(), readerAttributes.Get(), &reader);
        }

        if (SUCCEEDED(hr)) {
            ComPtr<IMFMediaType> pcmType;
            hr = MFCreateMediaType(&pcmType);
            if (SUCCEEDED(hr)) hr = pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
            if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
            if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000);
            if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
            if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, pcmType.Get());
        }

        HWAVEOUT output = nullptr;
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = 192000;
        if (SUCCEEDED(hr)) hr = waveOutOpen(&output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);

        if (FAILED(hr)) {
            Log(L"No se pudo iniciar la captura de audio. HRESULT=" + std::to_wstring(static_cast<unsigned long>(hr)));
            PostMessageW(notifyWindow, WM_APP_AUDIO_LOST, 0, 0);
            if (output) waveOutClose(output);
            reader.Reset();
            if (source) source->Shutdown();
            running_.store(false);
            if (SUCCEEDED(comHr)) CoUninitialize();
            return;
        }

        std::deque<std::unique_ptr<QueuedAudio>> queue;
        auto releaseFront = [&](bool wait) {
            if (queue.empty()) return;
            auto& item = queue.front();
            if (wait) {
                while (!(item->header.dwFlags & WHDR_DONE) && !stopRequested_.load()) Sleep(2);
            }
            if (item->header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(output, &item->header, sizeof(WAVEHDR));
            queue.pop_front();
        };

        while (!stopRequested_.load()) {
            DWORD streamIndex = 0, flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, &streamIndex, &flags, &timestamp, &sample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
            if (!sample) { Sleep(1); continue; }

            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;
            BYTE* data = nullptr;
            DWORD maxLength = 0, currentLength = 0;
            if (FAILED(buffer->Lock(&data, &maxLength, &currentLength))) continue;
            if (!data) { buffer->Unlock(); continue; }
            if (currentLength >= format.nBlockAlign) {
                auto item = std::make_unique<QueuedAudio>();
                const DWORD usableLength = currentLength - (currentLength % format.nBlockAlign);
                item->bytes.resize(usableLength);
                std::memcpy(item->bytes.data(), data, usableLength);
                buffer->Unlock();

                const int volume = g_muted.load() ? 0 : g_volume.load();
                auto* samples = reinterpret_cast<int16_t*>(item->bytes.data());
                const size_t sampleCount = item->bytes.size() / sizeof(int16_t);
                for (size_t i = 0; i < sampleCount; ++i) {
                    const int scaled = static_cast<int>(samples[i]) * volume / 100;
                    samples[i] = static_cast<int16_t>(std::clamp(scaled, -32768, 32767));
                }
                item->header.lpData = item->bytes.data();
                item->header.dwBufferLength = static_cast<DWORD>(item->bytes.size());
                if (waveOutPrepareHeader(output, &item->header, sizeof(WAVEHDR)) == MMSYSERR_NOERROR &&
                    waveOutWrite(output, &item->header, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                    queue.push_back(std::move(item));
                } else if (item->header.dwFlags & WHDR_PREPARED) {
                    waveOutUnprepareHeader(output, &item->header, sizeof(WAVEHDR));
                }
                while (queue.size() >= 5 && !stopRequested_.load()) releaseFront(true);
            } else {
                buffer->Unlock();
            }
        }

        waveOutReset(output);
        while (!queue.empty()) releaseFront(true);
        waveOutClose(output);
        reader.Reset();
        if (source) source->Shutdown();
        if (!stopRequested_.load()) PostMessageW(notifyWindow, WM_APP_AUDIO_LOST, 0, 0);
        running_.store(false);
        if (SUCCEEDED(comHr)) CoUninitialize();
    }

    std::thread worker_;
    std::atomic_bool stopRequested_{ false };
    std::atomic_bool running_{ false };
};

VideoCapture g_videoCapture;
AudioCapture g_audioCapture;

void ApplyPipZOrder() {
    if (g_pipHwnd) SetWindowPos(g_pipHwnd, g_pipAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void LoadProfile(const ProfileEntry& profile) {
    if (g_windowMode != 0) SetWindowMode(0);
    SaveSettings();
    if (g_videoCapture.IsRunning()) g_videoCapture.Stop();
    if (g_audioCapture.IsRunning()) g_audioCapture.Stop();
    g_activeProfilePath = profile.path;
    g_activeProfileName = profile.name;
    LoadSettingsFrom(g_activeProfilePath);
    RequestMenuRebuild();
    SetWindowTextW(g_hwnd, (L"PlayView | " + g_activeProfileName).c_str());
    SendMessageW(g_volumeSlider, TBM_SETPOS, TRUE, g_settings.volume);
    
    SetWindowTextW(g_muteButton, g_settings.muted ? L"Activar" : L"Silenciar");
    ApplyLoadedWindowPreferences();
    RefreshDevices(true);
    Log(L"Perfil cargado: " + profile.name);
}

void DeleteProfile(const ProfileEntry& profile) {
    if (profile.path == g_activeProfilePath) {
        MessageBoxW(g_hwnd, L"No puedes eliminar el perfil que está en uso. Cambia primero a otro perfil.", L"PlayView", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::wstring question = L"¿Eliminar el perfil '" + profile.name + L"'? Los accesos directos existentes dejarán de funcionar.";
    if (MessageBoxW(g_hwnd, question.c_str(), L"Eliminar perfil", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    std::error_code ec;
    std::filesystem::remove(profile.path, ec);
    if (!profile.iconPath.empty()) std::filesystem::remove(profile.iconPath, ec);
    RequestMenuRebuild();
}

void ChangeProfileIcon(const ProfileEntry& profile) {
    std::filesystem::path iconSource;
    if (!PickIconFile(iconSource)) return;
    const auto destination = g_profileIconsDirectory / (profile.path.stem().wstring() + L".ico");
    if (!SaveProfileIcon(iconSource, destination)) {
        MessageBoxW(g_hwnd, L"No se pudo copiar el icono seleccionado.", L"PlayView", MB_OK | MB_ICONERROR);
        return;
    }
    WritePrivateProfileStringW(L"Profile", L"IconPath", destination.c_str(), profile.path.c_str());
    RequestMenuRebuild();
}


void SetControlFont(HWND control) {
    if (control && g_uiFont) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
}

void UpdateVolumeTooltip() {
    if (!g_volumeTooltip || !g_volumeSlider) return;
    g_volumeTooltipText = std::to_wstring(
        static_cast<int>(SendMessageW(g_volumeSlider, TBM_GETPOS, 0, 0))) + L"%";
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
    tool.hwnd = g_hwnd;
    tool.uId = reinterpret_cast<UINT_PTR>(g_volumeSlider);
    tool.lpszText = const_cast<LPWSTR>(g_volumeTooltipText.c_str());
    SendMessageW(g_volumeTooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));

    POINT cursor{};
    GetCursorPos(&cursor);
    SendMessageW(g_volumeTooltip, TTM_TRACKPOSITION, 0,
        MAKELPARAM(static_cast<SHORT>(cursor.x), static_cast<SHORT>(cursor.y - 26)));
    SendMessageW(g_volumeTooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
    g_volumeTooltipVisible = true;
}

void HideVolumeTooltip() {
    if (!g_volumeTooltip || !g_volumeTooltipVisible || !g_volumeSlider) return;
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
    tool.hwnd = g_hwnd;
    tool.uId = reinterpret_cast<UINT_PTR>(g_volumeSlider);
    tool.lpszText = const_cast<LPWSTR>(g_volumeTooltipText.c_str());
    SendMessageW(g_volumeTooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    g_volumeTooltipVisible = false;
}

LRESULT CALLBACK VolumeSliderSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR subclassId, DWORD_PTR referenceData) {
    (void)wParam;
    (void)lParam;
    (void)referenceData;
    switch (message) {
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tracking);
        UpdateVolumeTooltip();
        break;
    }
    case WM_MOUSELEAVE:
        HideVolumeTooltip();
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, VolumeSliderSubclass, subclassId);
        break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

HWND AddControl(DWORD exStyle, const wchar_t* className, const wchar_t* text, DWORD style,
                int x, int y, int width, int height, int id) {
    HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, g_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SetControlFont(control);
    return control;
}

void PopulateDeviceCombo(HWND combo, const std::vector<DeviceInfo>& devices,
                         const std::wstring& selectedId, const std::wstring& savedName,
                         const wchar_t* emptyText) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    if (selectedId.empty()) {
        const LRESULT placeholder = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(emptyText));
        SendMessageW(combo, CB_SETITEMDATA, placeholder, static_cast<LPARAM>(-1));
    }
    int selectedIndex = -1;
    for (size_t i = 0; i < devices.size(); ++i) {
        const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(devices[i].name.c_str()));
        SendMessageW(combo, CB_SETITEMDATA, index, static_cast<LPARAM>(i));
        if (!selectedId.empty() && devices[i].id == selectedId) selectedIndex = static_cast<int>(index);
    }
    if (!selectedId.empty() && selectedIndex < 0) {
        const std::wstring missing = L"No encontrado: " + (savedName.empty() ? selectedId : savedName);
        selectedIndex = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(missing.c_str())));
        SendMessageW(combo, CB_SETITEMDATA, selectedIndex, static_cast<LPARAM>(-1));
    }
    if (selectedIndex >= 0) SendMessageW(combo, CB_SETCURSEL, selectedIndex, 0);
    else SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void PopulateResolutionCombo() {
    SendMessageW(g_resolutionCombo, CB_RESETCONTENT, 0, 0);
    const LRESULT automatic = SendMessageW(g_resolutionCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automática"));
    SendMessageW(g_resolutionCombo, CB_SETITEMDATA, automatic, 0);
    int selected = 0;
    for (size_t i = 0; i < g_videoModes.size(); ++i) {
        const auto& mode = g_videoModes[i];
        const std::wstring label = std::to_wstring(mode.width) + L" × " + std::to_wstring(mode.height);
        const LRESULT index = SendMessageW(g_resolutionCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(g_resolutionCombo, CB_SETITEMDATA, index, static_cast<LPARAM>(i + 1));
        if (g_settings.width == static_cast<int>(mode.width) && g_settings.height == static_cast<int>(mode.height)) selected = static_cast<int>(index);
    }
    SendMessageW(g_resolutionCombo, CB_SETCURSEL, selected, 0);
}

void RefreshDevices(bool initial) {
    g_videoDevices = EnumerateDevices(true);
    g_audioDevices = EnumerateDevices(false);
    PopulateDeviceCombo(g_videoCombo, g_videoDevices, g_settings.videoId, g_settings.videoName, L"Selecciona una capturadora...");
    PopulateDeviceCombo(g_audioCombo, g_audioDevices, g_settings.audioId, g_settings.audioName, L"Selecciona el audio de la capturadora...");

    const bool videoPresent = !g_settings.videoId.empty() && ContainsDevice(g_videoDevices, g_settings.videoId);
    const bool audioPresent = !g_settings.audioId.empty() && ContainsDevice(g_audioDevices, g_settings.audioId);
    bool startVideoNow = false;
    bool startAudioNow = false;

    if (!g_settings.videoId.empty()) {
        if (videoPresent) {
            if (initial) {
                g_videoStatus = L"Capturadora encontrada. Iniciando señal...";
                startVideoNow = true;
            }
        } else {
            g_videoStatus = L"No se encontró la capturadora guardada: " + (g_settings.videoName.empty() ? L"dispositivo desconocido" : g_settings.videoName);
            g_nextVideoRetry = GetTickCount64() + 2500;
        }
    } else {
        g_videoStatus = g_videoDevices.empty() ? L"No se detectaron capturadoras." : L"Selecciona tu capturadora para comenzar.";
    }

    if (!g_settings.audioId.empty()) {
        if (audioPresent) {
            if (initial) {
                g_audioStatus = L"Audio conectado";
                startAudioNow = true;
            }
        } else {
            g_audioStatus = L"No se encontró el audio guardado";
            g_nextAudioRetry = GetTickCount64() + 2500;
        }
    } else {
        g_audioStatus = L"Audio sin configurar";
    }

    // Enumerate capture modes before opening the selected camera. Some UVC
    // devices allow only one active source instance at a time.
    if (videoPresent && (initial || !g_videoCapture.IsRunning())) {
        g_videoModes = EnumerateVideoModes(g_settings.videoId);
        PopulateResolutionCombo();
    } else if (!videoPresent) {
        g_videoModes.clear();
        PopulateResolutionCombo();
    }

    if (startVideoNow) {
        g_videoStartTick = GetTickCount64();
        g_videoHasFrame.store(false);
        g_videoCapture.Start(g_hwnd, g_settings.videoId, g_settings.width, g_settings.height);
    }
    if (startAudioNow) g_audioCapture.Start(g_hwnd, g_settings.audioId);
    UpdateStatusText();
}

void StartSelectedVideo(bool showErrorIfMissing = true) {
    g_videoCapture.Stop();
    g_videoHasFrame.store(false);
    g_videoStartTick = GetTickCount64();
    g_frameMessagePending.store(false);
    {
        std::lock_guard<std::mutex> lock(g_frameMutex);
        g_latestFrame = {};
    }
    if (g_settings.videoId.empty()) {
        g_videoStatus = L"Selecciona una capturadora para comenzar.";
    } else if (!ContainsDevice(g_videoDevices, g_settings.videoId)) {
        g_videoStatus = L"No se encontró la capturadora guardada: " + (g_settings.videoName.empty() ? L"dispositivo desconocido" : g_settings.videoName);
        if (showErrorIfMissing) MessageBoxW(g_hwnd, g_videoStatus.c_str(), L"PlayView", MB_OK | MB_ICONWARNING);
        g_nextVideoRetry = GetTickCount64() + 3000;
    } else {
        g_videoStatus = L"Conectando con " + g_settings.videoName + L"...";
        g_videoCapture.Start(g_hwnd, g_settings.videoId, g_settings.width, g_settings.height);
    }
    UpdateStatusText();
    InvalidateVideoSurface(g_hwnd);
}

void StartSelectedAudio(bool showErrorIfMissing = false) {
    g_audioCapture.Stop();
    if (g_settings.audioId.empty()) {
        g_audioStatus = L"Audio sin configurar";
    } else if (!ContainsDevice(g_audioDevices, g_settings.audioId)) {
        g_audioStatus = L"No se encontró el dispositivo de audio guardado";
        if (showErrorIfMissing) MessageBoxW(g_hwnd, g_audioStatus.c_str(), L"PlayView", MB_OK | MB_ICONWARNING);
        g_nextAudioRetry = GetTickCount64() + 3000;
    } else {
        g_audioStatus = L"Audio conectado";
        g_audioCapture.Start(g_hwnd, g_settings.audioId);
    }
    UpdateStatusText();
}

void HandleVideoSelection() {
    const LRESULT selected = SendMessageW(g_videoCombo, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) return;
    const LRESULT itemData = SendMessageW(g_videoCombo, CB_GETITEMDATA, selected, 0);
    if (itemData < 0 || static_cast<size_t>(itemData) >= g_videoDevices.size()) return;
    const auto& device = g_videoDevices[static_cast<size_t>(itemData)];
    g_settings.videoId = device.id;
    g_settings.videoName = device.name;
    g_settings.width = 0;
    g_settings.height = 0;
    g_videoModes = EnumerateVideoModes(g_settings.videoId);
    PopulateResolutionCombo();
    SaveSettings();
    StartSelectedVideo(false);
}

void HandleAudioSelection() {
    const LRESULT selected = SendMessageW(g_audioCombo, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) return;
    const LRESULT itemData = SendMessageW(g_audioCombo, CB_GETITEMDATA, selected, 0);
    if (itemData < 0 || static_cast<size_t>(itemData) >= g_audioDevices.size()) return;
    const auto& device = g_audioDevices[static_cast<size_t>(itemData)];
    g_settings.audioId = device.id;
    g_settings.audioName = device.name;
    SaveSettings();
    StartSelectedAudio(false);
}

void HandleResolutionSelection() {
    const LRESULT selected = SendMessageW(g_resolutionCombo, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) return;
    const LRESULT modeIndex = SendMessageW(g_resolutionCombo, CB_GETITEMDATA, selected, 0);
    if (modeIndex <= 0 || static_cast<size_t>(modeIndex - 1) >= g_videoModes.size()) {
        g_settings.width = 0;
        g_settings.height = 0;
    } else {
        const auto& mode = g_videoModes[static_cast<size_t>(modeIndex - 1)];
        g_settings.width = static_cast<int>(mode.width);
        g_settings.height = static_cast<int>(mode.height);
    }
    SaveSettings();
    StartSelectedVideo(false);
}

void SetWindowMode(int mode) {
    // Only regular windowed mode (0) and fullscreen (2) remain supported.
    mode = (mode == 2) ? 2 : 0;
    if (!g_hwnd || g_isChangingWindowMode || mode == g_windowMode) return;
    g_isChangingWindowMode = true;

    if (g_windowMode == 0 && mode == 2 && !g_preserveRestoreRect) {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(g_hwnd, &placement)) {
            // Preserve the complete placement, not just rcNormalPosition.
            // This keeps Windows' maximized/restored state synchronized with
            // the dimensions and position when leaving fullscreen.
            g_preFullscreenPlacement = placement;
            g_preFullscreenPlacement.length = sizeof(g_preFullscreenPlacement);
            g_hasPreFullscreenPlacement = true;
            g_restoreRect = placement.rcNormalPosition;
            g_windowMaximized = placement.showCmd == SW_SHOWMAXIMIZED || IsZoomed(g_hwnd) != FALSE;
        } else {
            g_hasPreFullscreenPlacement = false;
            GetWindowRect(g_hwnd, &g_restoreRect);
            g_windowMaximized = IsZoomed(g_hwnd) != FALSE;
        }
    }

    LONG_PTR style = GetWindowLongPtrW(g_hwnd, GWL_STYLE);
    if (mode == 0) {
        // Restore the regular overlapped-window frame first. Do not apply
        // rcNormalPosition through SetWindowPos: its coordinates belong to
        // WINDOWPLACEMENT's workspace coordinate system.
        style &= ~static_cast<LONG_PTR>(WS_POPUP);
        style |= WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, style);
        if (g_appMenu) SetMenu(g_hwnd, g_appMenu);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
            SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        // Set the logical mode before applying the placement. Any synchronous
        // WM_SIZE messages generated by SetWindowPlacement are ignored while
        // g_isChangingWindowMode is true, and Windows receives one authoritative
        // instruction for both geometry and show state.
        g_windowMode = 0;
        g_settings.windowMode = 0;

        WINDOWPLACEMENT restore{};
        restore.length = sizeof(restore);
        if (g_hasPreFullscreenPlacement) {
            restore = g_preFullscreenPlacement;
            restore.length = sizeof(restore);
        } else {
            restore.flags = 0;
            restore.showCmd = g_windowMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
            restore.rcNormalPosition = g_restoreRect;
        }
        restore.showCmd = g_windowMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        if (!SetWindowPlacement(g_hwnd, &restore)) {
            // Last-resort recovery: restore the normal rectangle, then let
            // ShowWindow establish the matching maximized/restored state.
            SetWindowPos(g_hwnd, nullptr, g_restoreRect.left, g_restoreRect.top,
                g_restoreRect.right - g_restoreRect.left,
                g_restoreRect.bottom - g_restoreRect.top,
                SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            ShowWindow(g_hwnd, g_windowMaximized ? SW_MAXIMIZE : SW_RESTORE);
        }
        DrawMenuBar(g_hwnd);
        g_hasPreFullscreenPlacement = false;
    } else {
        // Capture placement immediately before removing the frame, even when
        // fullscreen was requested through the menu rather than the F11 key.
        SetMenu(g_hwnd, nullptr);
        style &= ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW);
        style |= WS_POPUP | WS_CLIPCHILDREN;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, style);
        MONITORINFO monitorInfo{ sizeof(monitorInfo) };
        GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
        const RECT target = monitorInfo.rcMonitor;
        SetWindowPos(g_hwnd, HWND_TOP, target.left, target.top, target.right - target.left,
            target.bottom - target.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        g_windowMode = 2;
        g_settings.windowMode = 2;
    }

    g_isChangingWindowMode = false;
    UpdateOverlayVisibility(true);
    SaveSettings();
    InvalidateVideoSurface(g_hwnd);
}

void ApplyLoadedWindowPreferences() {
    if (!g_hwnd) return;
    if (g_settings.windowMode != 0) {
        g_preserveRestoreRect = true;
        SetWindowMode(g_settings.windowMode);
        g_preserveRestoreRect = false;
        UpdateOverlayVisibility(true);
        return;
    }

    g_isChangingWindowMode = true;
    LONG_PTR style = GetWindowLongPtrW(g_hwnd, GWL_STYLE);
    style &= ~static_cast<LONG_PTR>(WS_POPUP);
    style |= WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    SetWindowLongPtrW(g_hwnd, GWL_STYLE, style);
    if (g_appMenu) SetMenu(g_hwnd, g_appMenu);
    SetWindowPos(g_hwnd, nullptr, g_restoreRect.left, g_restoreRect.top,
        g_restoreRect.right - g_restoreRect.left, g_restoreRect.bottom - g_restoreRect.top,
        SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    g_windowMode = 0;
    g_settings.windowMode = 0;
    g_isChangingWindowMode = false;
    ShowWindow(g_hwnd, g_windowMaximized ? SW_MAXIMIZE : SW_RESTORE);
    DrawMenuBar(g_hwnd);
    UpdateOverlayVisibility(true);
    InvalidateVideoSurface(g_hwnd);
}

void CreateAppMenu(HWND hwnd) {
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU view = CreatePopupMenu();
    HMENU help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING | (g_settings.showToolbar ? MF_CHECKED : MF_UNCHECKED),
        IDM_SHOW_TOOLBAR, L"Mostrar barra de opciones");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"Salir");
    AppendMenuW(view, MF_STRING, IDM_VIEW_WINDOW, L"Ventana");
    AppendMenuW(view, MF_STRING, IDM_VIEW_FULLSCREEN, L"Pantalla completa\tF11");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, IDM_VIEW_PIP, L"Picture in Picture\tCtrl+Mayús+P");
    AppendMenuW(view, MF_STRING | (g_pipAlwaysOnTop ? MF_CHECKED : MF_UNCHECKED), IDM_PIP_TOPMOST, L"PiP siempre visible");
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"Acerca de PlayView");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"Archivo");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"Ver");
    RebuildProfilesMenu(menu);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Ayuda");

    HMENU oldMenu = g_appMenu;
    g_appMenu = menu;
    SetMenu(hwnd, g_windowMode == 0 ? g_appMenu : nullptr);
    DrawMenuBar(hwnd);
    if (oldMenu) DestroyMenu(oldMenu);
}

void RequestMenuRebuild() {
    if (g_hwnd && IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_REBUILD_MENU, 0, 0);
}

void LayoutControls(int width, int /*height*/) {
    const int row1 = 8;
    const int comboH = 28;
    struct Placement { HWND window; int x; int y; int width; int height; };
    std::vector<Placement> placements;
    placements.reserve(9);
    auto add = [&](HWND window, int x, int y, int w, int h) {
        if (window) placements.push_back({ window, x, y, w, h });
    };

    const int videoComboW = std::max(180, width / 3);
    const int audioLabelX = 100 + videoComboW + 12;
    const int audioComboX = audioLabelX + 48;
    const int audioComboW = std::max(150, width - audioComboX - 315);
    const int resLabelX = audioComboX + audioComboW + 10;

    add(g_videoCombo, 100, row1, videoComboW, comboH);
    add(g_audioCombo, audioComboX, row1, audioComboW, comboH);
    add(g_resolutionCombo, resLabelX + 76, row1, 100, comboH);
    add(GetDlgItem(g_hwnd, IDC_RELOAD), width - 108, row1, 96, 28);
    add(g_volumeSlider, 128, 48, 154, 32);
    add(g_muteButton, 292, 49, 82, 28);
    add(GetDlgItem(g_hwnd, IDC_SYNC), 382, 49, 120, 28);
    add(GetDlgItem(g_hwnd, IDC_PIP), 510, 49, 82, 28);

    // Position all interactive controls as one batch to avoid intermediate
    // frame clears during resize and while switching display modes.
    HDWP batch = BeginDeferWindowPos(static_cast<int>(placements.size()));
    bool positioned = batch != nullptr;
    if (positioned) {
        for (const auto& item : placements) {
            HDWP next = DeferWindowPos(batch, item.window, nullptr,
                item.x, item.y, item.width, item.height,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
            if (!next) { positioned = false; break; }
            batch = next;
        }
        if (positioned) positioned = EndDeferWindowPos(batch) != FALSE;
    }
    if (!positioned) {
        for (const auto& item : placements)
            MoveWindow(item.window, item.x, item.y, item.width, item.height, FALSE);
    }

    for (const auto& item : placements) {
        if (IsWindowVisible(item.window))
            RedrawWindow(item.window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
    InvalidateVideoSurface(g_hwnd);
}

void DestroyPaintBuffer(PaintBuffer& buffer) {
    if (buffer.dc && buffer.previousBitmap) SelectObject(buffer.dc, buffer.previousBitmap);
    if (buffer.bitmap) DeleteObject(buffer.bitmap);
    if (buffer.dc) DeleteDC(buffer.dc);
    if (buffer.backgroundBrush) DeleteObject(buffer.backgroundBrush);
    buffer = {};
}

bool EnsurePaintBuffer(HDC reference, PaintBuffer& buffer, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (!buffer.dc) buffer.dc = CreateCompatibleDC(reference);
    if (!buffer.dc) return false;
    if (buffer.bitmap && buffer.width == width && buffer.height == height)
        return buffer.backgroundBrush != nullptr;

    if (buffer.bitmap) {
        if (buffer.previousBitmap) SelectObject(buffer.dc, buffer.previousBitmap);
        DeleteObject(buffer.bitmap);
        buffer.bitmap = nullptr;
        buffer.previousBitmap = nullptr;
    }
    buffer.bitmap = CreateCompatibleBitmap(reference, width, height);
    if (!buffer.bitmap) return false;
    buffer.previousBitmap = SelectObject(buffer.dc, buffer.bitmap);
    if (!buffer.previousBitmap || buffer.previousBitmap == HGDI_ERROR) {
        DeleteObject(buffer.bitmap);
        buffer.bitmap = nullptr;
        buffer.previousBitmap = nullptr;
        return false;
    }
    if (!buffer.backgroundBrush) buffer.backgroundBrush = CreateSolidBrush(RGB(7, 10, 16));
    buffer.width = width;
    buffer.height = height;
    return buffer.backgroundBrush != nullptr;
}

void DrawPreview(HDC target, HWND window, PaintBuffer& buffer, bool compact = false) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (!EnsurePaintBuffer(target, buffer, width, height)) return;

    FillRect(buffer.dc, &client, buffer.backgroundBrush);
    RECT area = client; // The toolbar is layered above the video, never reserved from its area.
    bool hasFrame = false;
    {
        std::lock_guard<std::mutex> lock(g_frameMutex);
        if (g_latestFrame.width > 0 && g_latestFrame.height > 0 && !g_latestFrame.pixels.empty()) {
            hasFrame = true;
            const int viewWidth = area.right - area.left;
            const int viewHeight = area.bottom - area.top;
            if (viewWidth > 0 && viewHeight > 0) {
                const BYTE* framePixels = g_latestFrame.pixels.data();
                if (compact && g_pipHover) {
                    // Reuse a scratch buffer so hover does not allocate a new
                    // full-frame vector on every paint. If memory is tight,
                    // keep the live picture rather than failing the paint.
                    try {
                        g_pipDimFrameScratch.resize(g_latestFrame.pixels.size());
                        std::copy(g_latestFrame.pixels.begin(), g_latestFrame.pixels.end(),
                            g_pipDimFrameScratch.begin());
                        for (size_t i = 0; i + 3 < g_pipDimFrameScratch.size(); i += 4) {
                            g_pipDimFrameScratch[i] = static_cast<BYTE>((g_pipDimFrameScratch[i] * 45) / 100);
                            g_pipDimFrameScratch[i + 1] = static_cast<BYTE>((g_pipDimFrameScratch[i + 1] * 45) / 100);
                            g_pipDimFrameScratch[i + 2] = static_cast<BYTE>((g_pipDimFrameScratch[i + 2] * 45) / 100);
                        }
                        framePixels = g_pipDimFrameScratch.data();
                    } catch (...) {
                        framePixels = g_latestFrame.pixels.data();
                    }
                }
                const double scale = std::min(static_cast<double>(viewWidth) / g_latestFrame.width,
                                              static_cast<double>(viewHeight) / g_latestFrame.height);
                const int drawWidth = std::max(1, static_cast<int>(g_latestFrame.width * scale));
                const int drawHeight = std::max(1, static_cast<int>(g_latestFrame.height * scale));
                const int x = area.left + (viewWidth - drawWidth) / 2;
                const int y = area.top + (viewHeight - drawHeight) / 2;
                BITMAPINFO bitmapInfo{};
                bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bitmapInfo.bmiHeader.biWidth = g_latestFrame.width;
                bitmapInfo.bmiHeader.biHeight = -g_latestFrame.height;
                bitmapInfo.bmiHeader.biPlanes = 1;
                bitmapInfo.bmiHeader.biBitCount = 32;
                bitmapInfo.bmiHeader.biCompression = BI_RGB;
                SetStretchBltMode(buffer.dc, COLORONCOLOR);
                StretchDIBits(buffer.dc, x, y, drawWidth, drawHeight, 0, 0,
                    g_latestFrame.width, g_latestFrame.height, framePixels,
                    &bitmapInfo, DIB_RGB_COLORS, SRCCOPY);
            }
        }
    }
    if (!hasFrame) {
        SetBkMode(buffer.dc, TRANSPARENT);
        SetTextColor(buffer.dc, RGB(190, 205, 220));
        HFONT oldFont = static_cast<HFONT>(SelectObject(buffer.dc, g_uiFont));
        RECT textRect = area;
        DrawTextW(buffer.dc, g_videoStatus.c_str(), -1, &textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (oldFont) SelectObject(buffer.dc, oldFont);
    }

    // Toolbar captions are part of the video back buffer rather than opaque
    // STATIC child windows. The live picture therefore remains visible around
    // the controls without the previous gray rectangular background.
    if (!compact && g_overlayVisible) {
        HFONT oldFont = static_cast<HFONT>(SelectObject(buffer.dc, g_uiFont));
        const int oldBkMode = SetBkMode(buffer.dc, TRANSPARENT);
        auto drawLabel = [&](const std::wstring& text, RECT rect) {
            RECT shadow = rect;
            OffsetRect(&shadow, 1, 1);
            SetTextColor(buffer.dc, RGB(0, 0, 0));
            DrawTextW(buffer.dc, text.c_str(), -1, &shadow,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SetTextColor(buffer.dc, RGB(245, 248, 252));
            DrawTextW(buffer.dc, text.c_str(), -1, &rect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        };

        const int width = area.right - area.left;
        const int videoComboW = std::max(180, width / 3);
        const int audioLabelX = 100 + videoComboW + 12;
        const int audioComboX = audioLabelX + 48;
        const int audioComboW = std::max(150, width - audioComboX - 315);
        const int resLabelX = audioComboX + audioComboW + 10;

        drawLabel(L"Capturadora", RECT{ 12, 8, 96, 36 });
        drawLabel(L"Audio", RECT{ audioLabelX, 8, audioLabelX + 46, 36 });
        drawLabel(L"Resolución", RECT{ resLabelX, 8, resLabelX + 74, 36 });
        drawLabel(L"Volumen: " + std::to_wstring(g_settings.volume) + L"%",
            RECT{ 12, 48, 124, 80 });
        if (!g_statusText.empty()) {
            drawLabel(g_statusText, RECT{ 600, 48, std::max(720, width - 12), 80 });
        }
        SetBkMode(buffer.dc, oldBkMode);
        if (oldFont) SelectObject(buffer.dc, oldFont);
    }

    // PiP hover controls are drawn into the same back buffer so the user sees
    // one atomic presentation rather than a video blit followed by an overlay blit.
    if (compact && g_pipHover) DrawPipHoverActions(buffer.dc, window);

    // Present the completed frame in one operation to prevent visible clears
    // and partial GDI redraws in the main preview and the PiP window.
    BitBlt(target, 0, 0, width, height, buffer.dc, 0, 0, SRCCOPY);
}

void InvalidateVideoSurface(HWND target) {
    if (!target || !IsWindow(target)) return;
    RECT area{};
    GetClientRect(target, &area);
    if (area.top < area.bottom) InvalidateRect(target, &area, FALSE);
}

bool IsAnyToolbarComboDropped() {
    const HWND combos[] = { g_videoCombo, g_audioCombo, g_resolutionCombo };
    for (HWND combo : combos) {
        if (combo && IsWindow(combo) && SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0))
            return true;
    }
    return false;
}

void ShowToolbarControls(bool visible) {
    if (!g_hwnd) return;
    const int ids[] = {
        IDC_VIDEO_DEVICE, IDC_AUDIO_DEVICE, IDC_RESOLUTION, IDC_RELOAD,
        IDC_VOLUME, IDC_MUTE, IDC_SYNC, IDC_PIP
    };
    for (int id : ids) {
        HWND control = GetDlgItem(g_hwnd, id);
        if (!control) continue;
        if (visible) {
            SetWindowPos(control, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOREDRAW);
            ShowWindow(control, SW_SHOWNOACTIVATE);
            RedrawWindow(control, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        } else {
            ShowWindow(control, SW_HIDE);
        }
    }
    g_overlayVisible = visible;
    InvalidateVideoSurface(g_hwnd);
}

void UpdateOverlayVisibility(bool force) {
    if (!g_hwnd) return;

    if (g_windowMode == 0) {
        if (g_appMenu && GetMenu(g_hwnd) != g_appMenu) {
            SetMenu(g_hwnd, g_appMenu);
            DrawMenuBar(g_hwnd);
        }
    } else if (GetMenu(g_hwnd)) {
        SetMenu(g_hwnd, nullptr);
        DrawMenuBar(g_hwnd);
    }

    // The menu option controls only normal windowed mode. Checked means the
    // toolbar stays fixed. Fullscreen always uses top-edge hover, regardless
    // of the saved windowed-mode preference.
    const bool useHoverReveal = (g_windowMode == 2) || !g_settings.showToolbar;
    if (!useHoverReveal) {
        KillTimer(g_hwnd, TIMER_OVERLAY);
        if (!g_overlayVisible || force) ShowToolbarControls(true);
        return;
    }

    SetTimer(g_hwnd, TIMER_OVERLAY, 70, nullptr);
    bool inTopZone = false;
    POINT cursor{};
    if (GetCursorPos(&cursor)) {
        POINT clientCursor = cursor;
        if (ScreenToClient(g_hwnd, &clientCursor)) {
            RECT client{};
            GetClientRect(g_hwnd, &client);
            inTopZone = clientCursor.x >= 0 && clientCursor.x < client.right &&
                clientCursor.y >= 0 && clientCursor.y <= 104;
        }
    }

    // Keep the bar open while a combo's native popup list is active, including
    // the lower entries that extend beyond the toolbar's visible top area.
    const bool comboIsOpen = IsAnyToolbarComboDropped();
    const bool shouldShow = inTopZone || comboIsOpen;
    if (force || shouldShow != g_overlayVisible) ShowToolbarControls(shouldShow);
}

struct PipActionRects {
    RECT returnToMain{};
    RECT closePip{};
};

PipActionRects GetPipActionRects(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int minDimension = static_cast<int>(std::min(client.right, client.bottom));
    const int buttonSize = std::clamp(minDimension / 5, 48, 64);
    const int gap = std::max(12, buttonSize / 4);
    const int totalWidth = buttonSize * 2 + gap;
    const int left = (client.right - totalWidth) / 2;
    const int top = (client.bottom - buttonSize) / 2;
    PipActionRects result;
    result.returnToMain = { left, top, left + buttonSize, top + buttonSize };
    result.closePip = { left + buttonSize + gap, top, left + buttonSize * 2 + gap, top + buttonSize };
    return result;
}

bool PointInPipActionButton(HWND hwnd, POINT point) {
    if (!g_pipHover) return false;
    const PipActionRects buttons = GetPipActionRects(hwnd);
    return PtInRect(&buttons.returnToMain, point) || PtInRect(&buttons.closePip, point);
}

void UpdatePipHover(HWND hwnd, bool hovered) {
    if (g_pipHover == hovered) return;
    g_pipHover = hovered;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void UpdatePipHoverFromCursor(HWND hwnd) {
    POINT cursor{};
    RECT windowRect{};
    const bool inside = GetCursorPos(&cursor) && GetWindowRect(hwnd, &windowRect) &&
        PtInRect(&windowRect, cursor) != FALSE;
    // WM_MOUSELEAVE and WM_NCMOUSELEAVE can be raised when the pointer merely
    // crosses between client and non-client hit-test areas. Do not remove the
    // hover overlay unless the pointer has actually left the whole PiP window.
    UpdatePipHover(hwnd, inside);
}

void DrawPipHoverActions(HDC dc, HWND hwnd) {
    const PipActionRects buttons = GetPipActionRects(hwnd);
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    HPEN whitePen = CreatePen(PS_SOLID, 3, RGB(242, 247, 255));
    HPEN cyanPen = CreatePen(PS_SOLID, 3, RGB(83, 200, 242));
    HPEN redPen = CreatePen(PS_SOLID, 3, RGB(245, 105, 105));
    HBRUSH buttonBrush = CreateSolidBrush(RGB(34, 42, 54));
    HGDIOBJ oldPen = SelectObject(dc, whitePen);
    HGDIOBJ oldBrush = SelectObject(dc, buttonBrush);

    auto drawButtonCircle = [&](const RECT& r) {
        Ellipse(dc, r.left, r.top, r.right, r.bottom);
    };
    drawButtonCircle(buttons.returnToMain);
    drawButtonCircle(buttons.closePip);

    const int cx1 = (buttons.returnToMain.left + buttons.returnToMain.right) / 2;
    const int cy1 = (buttons.returnToMain.top + buttons.returnToMain.bottom) / 2;
    const int cx2 = (buttons.closePip.left + buttons.closePip.right) / 2;
    const int cy2 = (buttons.closePip.top + buttons.closePip.bottom) / 2;
    const int icon = std::max(18, static_cast<int>(buttons.returnToMain.right - buttons.returnToMain.left) / 2);

    // First control: a window with an outward arrow (return to main app).
    SelectObject(dc, cyanPen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, cx1 - icon / 2, cy1 - icon / 2, cx1 + icon / 2, cy1 + icon / 2);
    MoveToEx(dc, cx1 - 2, cy1 + icon / 3, nullptr);
    LineTo(dc, cx1 + icon / 3, cy1 - icon / 3);
    LineTo(dc, cx1 + icon / 3 - 1, cy1 - 3);
    MoveToEx(dc, cx1 + icon / 3, cy1 - icon / 3, nullptr);
    LineTo(dc, cx1 + 3, cy1 - icon / 3 + 1);

    // Second control: a clearly recognizable close (X) button.
    SelectObject(dc, redPen);
    MoveToEx(dc, cx2 - icon / 3, cy2 - icon / 3, nullptr);
    LineTo(dc, cx2 + icon / 3, cy2 + icon / 3);
    MoveToEx(dc, cx2 + icon / 3, cy2 - icon / 3, nullptr);
    LineTo(dc, cx2 - icon / 3, cy2 + icon / 3);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    SetBkMode(dc, oldMode);
    DeleteObject(whitePen);
    DeleteObject(cyanPen);
    DeleteObject(redPen);
    DeleteObject(buttonBrush);
}

void ReturnFromPipToMain(HWND pipWindow) {
    ShowWindow(pipWindow, SW_HIDE);
    g_pipHover = false;
    if (g_hwnd && IsWindow(g_hwnd)) {
        ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(g_hwnd);
        SetFocus(g_hwnd);
    }
}

LRESULT CALLBACK PipWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCALCSIZE:
        // Draw only the video surface. The frame remains borderless; the
        // non-client hit-test still provides invisible resize grips.
        return 0;
    case WM_NCPAINT:
    case WM_NCACTIVATE:
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        DrawPreview(dc, hwnd, g_pipPaintBuffer, true);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 260;
        info->ptMinTrackSize.y = 180;
        return 0;
    }
    case WM_MOUSEACTIVATE:
        // Clicking PiP must not steal foreground from the application behind it.
        return MA_NOACTIVATE;
    case WM_SETCURSOR: {
        const LRESULT hitTest = LOWORD(lParam);
        if (hitTest == HTCLIENT) {
            // In particular, the two hover actions always use the arrow cursor.
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        if (hitTest == HTCAPTION) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        }
        // Let Windows select the correct resize cursor for HTLEFT/HTTOP/etc.
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tracking);
        UpdatePipHover(hwnd, true);
        return 0;
    }
    case WM_MOUSELEAVE:
        UpdatePipHoverFromCursor(hwnd);
        return 0;
    case WM_NCMOUSEMOVE: {
        TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE | TME_NONCLIENT, hwnd, 0 };
        TrackMouseEvent(&tracking);
        UpdatePipHover(hwnd, true);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    case WM_NCMOUSELEAVE:
        UpdatePipHoverFromCursor(hwnd);
        return 0;
    case WM_NCHITTEST: {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &point);
        RECT client{};
        GetClientRect(hwnd, &client);
        constexpr int grip = 9;
        const bool left = point.x < grip;
        const bool right = point.x >= client.right - grip;
        const bool top = point.y < grip;
        const bool bottom = point.y >= client.bottom - grip;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        if (PointInPipActionButton(hwnd, point)) return HTCLIENT;
        return HTCAPTION;
    }
    case WM_LBUTTONUP: {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const PipActionRects buttons = GetPipActionRects(hwnd);
        if (g_pipHover && PtInRect(&buttons.returnToMain, point)) {
            ReturnFromPipToMain(hwnd);
            return 0;
        }
        if (g_pipHover && PtInRect(&buttons.closePip, point)) {
            ShowWindow(hwnd, SW_HIDE);
            g_pipHover = false;
            return 0;
        }
        break;
    }
    case WM_NCLBUTTONDBLCLK:
        ReturnFromPipToMain(hwnd);
        return 0;
    case WM_CONTEXTMENU: {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (lParam == -1) GetCursorPos(&point);
        HMENU context = CreatePopupMenu();
        AppendMenuW(context, MF_STRING, IDM_PIP_CLOSE, L"Ocultar Picture in Picture");
        AppendMenuW(context, MF_STRING | (g_pipAlwaysOnTop ? MF_CHECKED : MF_UNCHECKED), IDM_PIP_TOPMOST, L"Siempre visible");
        TrackPopupMenu(context, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
        DestroyMenu(context);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_PIP_CLOSE) { ShowWindow(hwnd, SW_HIDE); g_pipHover = false; return 0; }
        if (LOWORD(wParam) == IDM_PIP_TOPMOST) { TogglePipTopmost(); return 0; }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { ShowWindow(hwnd, SW_HIDE); g_pipHover = false; return 0; }
        break;
    case WM_SHOWWINDOW:
        if (!wParam) g_pipHover = false;
        break;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        g_pipHover = false;
        return 0;
    case WM_DESTROY:
        DestroyPaintBuffer(g_pipPaintBuffer);
        if (g_pipHwnd == hwnd) g_pipHwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void TogglePictureInPicture() {
    if (g_pipHwnd && IsWindowVisible(g_pipHwnd)) {
        ShowWindow(g_pipHwnd, SW_HIDE);
        if (g_hwnd) SetFocus(g_hwnd);
        return;
    }
    if (!g_pipHwnd) {
        DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        if (g_pipAlwaysOnTop) exStyle |= WS_EX_TOPMOST;
        g_pipHwnd = CreateWindowExW(exStyle, kPipWindowClass, L"PlayView PiP",
            WS_POPUP | WS_THICKFRAME | WS_VISIBLE, 120, 120, 520, 310,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_pipHwnd) {
            MessageBoxW(g_hwnd, L"No se pudo crear la ventana Picture in Picture.", L"PlayView", MB_OK | MB_ICONERROR);
            return;
        }
    }
    if (g_pipAlwaysOnTop) SetWindowPos(g_pipHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    else SetWindowPos(g_pipHwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ShowWindow(g_pipHwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_pipHwnd);
}

void TogglePipTopmost() {
    g_pipAlwaysOnTop = !g_pipAlwaysOnTop;
    if (g_pipHwnd) SetWindowPos(g_pipHwnd, g_pipAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    RequestMenuRebuild();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        g_uiFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_videoCombo = AddControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 0, 0, IDC_VIDEO_DEVICE);
        g_audioCombo = AddControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 0, 0, IDC_AUDIO_DEVICE);
        SendMessageW(g_videoCombo, CB_SETMINVISIBLE, 8, 0);
        SendMessageW(g_audioCombo, CB_SETMINVISIBLE, 8, 0);
        g_resolutionCombo = AddControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 0, 0, IDC_RESOLUTION);
        AddControl(0, L"BUTTON", L"Recargar (F5)", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_RELOAD);
        AddControl(0, L"BUTTON", L"PiP", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_PIP);
        g_volumeSlider = AddControl(0, TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, 0, 0, 0, 0, IDC_VOLUME);
        if (g_volumeSlider) SetWindowSubclass(g_volumeSlider, VolumeSliderSubclass, 1, 0);
        g_volumeTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (g_volumeTooltip && g_volumeSlider) {
            TOOLINFOW tool{};
            tool.cbSize = sizeof(tool);
            tool.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
            tool.hwnd = hwnd;
            tool.uId = reinterpret_cast<UINT_PTR>(g_volumeSlider);
            tool.lpszText = const_cast<LPWSTR>(L"0%");
            SendMessageW(g_volumeTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
        SendMessageW(g_volumeSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(g_volumeSlider, TBM_SETPOS, TRUE, g_settings.volume);
        
        g_muteButton = AddControl(0, L"BUTTON", g_settings.muted ? L"Activar" : L"Silenciar", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_MUTE);
        AddControl(0, L"BUTTON", L"Resincronizar", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_SYNC);
        CreateAppMenu(hwnd);
        RefreshDevices(true);
        SetTimer(hwnd, TIMER_RECONNECT, 2500, nullptr);
        return 0;
    }
    case WM_SIZE:
        if (!g_isChangingWindowMode && g_windowMode == 0) {
            if (wParam == SIZE_MAXIMIZED) g_windowMaximized = true;
            else if (wParam == SIZE_RESTORED && !IsIconic(hwnd))
                g_windowMaximized = IsZoomed(hwnd) != FALSE;
        }
        LayoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_EXITSIZEMOVE:
        CaptureNormalWindowPlacement();
        SaveSettings();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 950;
        info->ptMinTrackSize.y = 440;
        return 0;
    }
    case WM_COMMAND: {
        const UINT commandId = LOWORD(wParam);
        if (commandId == IDM_PROFILE_SAVE) { SaveCurrentAsProfile(); return 0; }
        if (commandId == IDM_PROFILE_DEFAULT) {
            if (g_windowMode != 0) SetWindowMode(0);
            SaveSettings();
            g_videoCapture.Stop();
            g_audioCapture.Stop();
            g_activeProfilePath.clear();
            g_activeProfileName.clear();
            LoadSettings();
            ApplyPipZOrder();
            RequestMenuRebuild();
            ApplyLoadedWindowPreferences();
            SetWindowTextW(g_hwnd, L"PlayView");
            SendMessageW(g_volumeSlider, TBM_SETPOS, TRUE, g_settings.volume);
            SetWindowTextW(g_muteButton, g_settings.muted ? L"Activar" : L"Silenciar");
            InvalidateVideoSurface(hwnd);
            RefreshDevices(true);
            return 0;
        }
        if (commandId >= IDM_PROFILE_LOAD_BASE && commandId < IDM_PROFILE_LOAD_BASE + ID_PROFILE_MENU_LIMIT) {
            const size_t index = commandId - IDM_PROFILE_LOAD_BASE;
            if (index < g_profileMenuEntries.size()) LoadProfile(g_profileMenuEntries[index]);
            return 0;
        }
        if (commandId >= IDM_PROFILE_SHORTCUT_BASE && commandId < IDM_PROFILE_SHORTCUT_BASE + ID_PROFILE_MENU_LIMIT) {
            const size_t index = commandId - IDM_PROFILE_SHORTCUT_BASE;
            if (index < g_profileMenuEntries.size()) {
                if (CreateProfileShortcut(g_profileMenuEntries[index])) MessageBoxW(hwnd, L"Acceso directo creado en el escritorio.", L"PlayView", MB_OK | MB_ICONINFORMATION);
                else MessageBoxW(hwnd, L"No se pudo crear el acceso directo.", L"PlayView", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }
        if (commandId >= IDM_PROFILE_DELETE_BASE && commandId < IDM_PROFILE_DELETE_BASE + ID_PROFILE_MENU_LIMIT) {
            const size_t index = commandId - IDM_PROFILE_DELETE_BASE;
            if (index < g_profileMenuEntries.size()) DeleteProfile(g_profileMenuEntries[index]);
            return 0;
        }
        if (commandId >= IDM_PROFILE_ICON_BASE && commandId < IDM_PROFILE_ICON_BASE + ID_PROFILE_MENU_LIMIT) {
            const size_t index = commandId - IDM_PROFILE_ICON_BASE;
            if (index < g_profileMenuEntries.size()) ChangeProfileIcon(g_profileMenuEntries[index]);
            return 0;
        }
        switch (commandId) {
        case IDC_VIDEO_DEVICE:
            if (HIWORD(wParam) == CBN_SELCHANGE) HandleVideoSelection();
            return 0;
        case IDC_AUDIO_DEVICE:
            if (HIWORD(wParam) == CBN_SELCHANGE) HandleAudioSelection();
            return 0;
        case IDC_RESOLUTION:
            if (HIWORD(wParam) == CBN_SELCHANGE) HandleResolutionSelection();
            return 0;
        case IDC_RELOAD:
            RefreshDevices(false);
            StartSelectedVideo(true);
            StartSelectedAudio(true);
            return 0;
        case IDC_SYNC:
            RefreshDevices(false);
            StartSelectedVideo(false);
            StartSelectedAudio(false);
            g_videoStatus = L"Capturas reiniciadas para resincronización";
            UpdateStatusText();
            return 0;
        case IDC_MUTE:
            g_settings.muted = !g_settings.muted;
            g_muted.store(g_settings.muted);
            SetWindowTextW(g_muteButton, g_settings.muted ? L"Activar" : L"Silenciar");
            SaveSettings();
            return 0;
        case IDC_PIP: TogglePictureInPicture(); return 0;
        case IDM_VIEW_PIP: TogglePictureInPicture(); return 0;
        case IDM_PIP_TOPMOST: TogglePipTopmost(); return 0;
        case IDM_SHOW_TOOLBAR:
            g_settings.showToolbar = !g_settings.showToolbar;
            UpdateOverlayVisibility(true);
            SaveSettings();
            RequestMenuRebuild();
            return 0;
        case IDM_VIEW_WINDOW: SetWindowMode(0); return 0;
        case IDM_VIEW_FULLSCREEN: SetWindowMode(g_windowMode == 2 ? 0 : 2); return 0;
        case IDM_EXIT: PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0;
        case IDM_ABOUT:
            MessageBoxW(hwnd, L"PlayView 0.2.5\nMonitor ligero para capturadoras de video.\n\nCaptura mediante Windows Media Foundation.",
                L"Acerca de PlayView", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == g_volumeSlider) {
            g_settings.volume = static_cast<int>(SendMessageW(g_volumeSlider, TBM_GETPOS, 0, 0));
            g_volume.store(g_settings.volume);
            if (g_volumeTooltipVisible) UpdateVolumeTooltip();
            SaveSettings();
            InvalidateVideoSurface(hwnd);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_F11) { SetWindowMode(g_windowMode == 2 ? 0 : 2); return 0; }
        if (wParam == VK_F5) {
            RefreshDevices(false);
            StartSelectedVideo(true);
            StartSelectedAudio(true);
            return 0;
        }
        if (wParam == VK_ESCAPE && g_windowMode == 2) { SetWindowMode(0); return 0; }
        break;
    case WM_APP_FRAME:
        g_frameMessagePending.store(false);
        InvalidateVideoSurface(hwnd);
        if (g_pipHwnd && IsWindowVisible(g_pipHwnd)) InvalidateVideoSurface(g_pipHwnd);
        return 0;
    case WM_APP_REBUILD_MENU:
        CreateAppMenu(hwnd);
        return 0;
    case WM_APP_VIDEO_READY:
        g_videoStatus = L"Captura activa";
        UpdateStatusText();
        return 0;
    case WM_APP_VIDEO_LOST:
        g_videoStatus = L"Se perdió la señal o la capturadora se desconectó. Buscando el mismo dispositivo...";
        g_videoHasFrame.store(false);
        g_nextVideoRetry = GetTickCount64() + 2500;
        {
            std::lock_guard<std::mutex> lock(g_frameMutex);
            g_latestFrame = {};
        }
        UpdateStatusText();
        InvalidateVideoSurface(hwnd);
        if (g_pipHwnd && IsWindowVisible(g_pipHwnd)) InvalidateVideoSurface(g_pipHwnd);
        return 0;
    case WM_APP_AUDIO_LOST:
        g_audioStatus = L"Audio desconectado. Buscando el mismo dispositivo...";
        g_nextAudioRetry = GetTickCount64() + 2500;
        UpdateStatusText();
        return 0;
    case WM_TIMER:
        if (wParam == TIMER_OVERLAY) {
            UpdateOverlayVisibility(false);
            return 0;
        }
        if (wParam == TIMER_RECONNECT) {
            const ULONGLONG now = GetTickCount64();
            const bool waitingForFirstFrame = g_videoCapture.IsRunning() && !g_videoHasFrame.load() &&
                g_videoStartTick > 0 && now - g_videoStartTick > 5000;
            const bool framesStopped = g_videoCapture.IsRunning() && g_videoHasFrame.load() &&
                g_lastVideoFrameTick.load() > 0 && now - g_lastVideoFrameTick.load() > 5000;
            if ((waitingForFirstFrame && g_videoStatus.find(L"Esperando fotogramas") == std::wstring::npos) ||
                (framesStopped && g_videoStatus.find(L"dejó de enviar fotogramas") == std::wstring::npos)) {
                g_videoStatus = waitingForFirstFrame
                    ? L"Esperando fotogramas. Comprueba la entrada HDMI de la capturadora."
                    : L"La capturadora dejó de enviar fotogramas. Pulsa F5 para recargar.";
                UpdateStatusText();
                InvalidateVideoSurface(hwnd);
            }
            if (!g_settings.videoId.empty() && !g_videoCapture.IsRunning() && now >= g_nextVideoRetry) {
                g_videoDevices = EnumerateDevices(true);
                PopulateDeviceCombo(g_videoCombo, g_videoDevices, g_settings.videoId, g_settings.videoName, L"Selecciona una capturadora...");
                if (ContainsDevice(g_videoDevices, g_settings.videoId)) {
                    g_videoStatus = L"Capturadora recuperada. Reconectando...";
                    g_videoStartTick = now;
                    g_videoHasFrame.store(false);
                    g_videoCapture.Start(hwnd, g_settings.videoId, g_settings.width, g_settings.height);
                } else {
                    g_videoStatus = L"No se encontró la capturadora guardada: " + (g_settings.videoName.empty() ? L"dispositivo desconocido" : g_settings.videoName);
                    g_nextVideoRetry = now + 4000;
                }
                UpdateStatusText();
            }
            if (!g_settings.audioId.empty() && !g_audioCapture.IsRunning() && now >= g_nextAudioRetry) {
                g_audioDevices = EnumerateDevices(false);
                PopulateDeviceCombo(g_audioCombo, g_audioDevices, g_settings.audioId, g_settings.audioName, L"Selecciona el audio de la capturadora...");
                if (ContainsDevice(g_audioDevices, g_settings.audioId)) {
                    g_audioCapture.Start(hwnd, g_settings.audioId);
                    g_audioStatus = L"Audio reconectado";
                } else {
                    g_audioStatus = L"No se encontró el audio guardado";
                    g_nextAudioRetry = now + 4000;
                }
                UpdateStatusText();
            }
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        DrawPreview(dc, hwnd, g_mainPaintBuffer, false);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CONTEXTMENU: {
        if (g_windowMode == 0) break;
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (lParam == -1) GetCursorPos(&point);
        HMENU context = CreatePopupMenu();
        AppendMenuW(context, MF_STRING, IDM_VIEW_WINDOW, L"Ventana");
        AppendMenuW(context, MF_STRING, IDM_VIEW_FULLSCREEN, L"Pantalla completa");
        AppendMenuW(context, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(context, MF_STRING, IDM_VIEW_PIP, L"Picture in Picture");
        AppendMenuW(context, MF_STRING, IDM_EXIT, L"Salir");
        TrackPopupMenu(context, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
        DestroyMenu(context);
        return 0;
    }
    case WM_CLOSE:
        KillTimer(hwnd, TIMER_RECONNECT);
        if (g_pipHwnd) DestroyWindow(g_pipHwnd);
        g_videoCapture.Stop();
        g_audioCapture.Stop();
        SaveSettings();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        SaveSettings();
        KillTimer(hwnd, TIMER_OVERLAY);
        KillTimer(hwnd, TIMER_RECONNECT);
        if (g_pipHwnd && IsWindow(g_pipHwnd)) DestroyWindow(g_pipHwnd);
        g_videoCapture.Stop();
        g_audioCapture.Stop();
        DestroyPaintBuffer(g_mainPaintBuffer);
        HideVolumeTooltip();
        if (g_volumeTooltip) { DestroyWindow(g_volumeTooltip); g_volumeTooltip = nullptr; }
        if (g_appMenu) { DestroyMenu(g_appMenu); g_appMenu = nullptr; }
        if (g_uiFont) { DeleteObject(g_uiFont); g_uiFont = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    InitializePaths();
    LoadSettings();
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments) {
        for (int i = 1; i + 1 < argumentCount; ++i) {
            if (_wcsicmp(arguments[i], L"--profile") == 0) {
                std::filesystem::path requestedProfile = arguments[i + 1];
                std::error_code ec;
                if (std::filesystem::exists(requestedProfile, ec)) {
                    g_activeProfilePath = requestedProfile;
                    wchar_t name[32768]{};
                    GetPrivateProfileStringW(L"Profile", L"Name", requestedProfile.stem().c_str(), name,
                        static_cast<DWORD>(std::size(name)), requestedProfile.c_str());
                    g_activeProfileName = name;
                    LoadSettingsFrom(g_activeProfilePath);
                }
                break;
            }
        }
        LocalFree(arguments);
    }
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT mfHr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(mfHr)) {
        MessageBoxW(nullptr, L"No se pudo iniciar Windows Media Foundation. Comprueba que Windows esté actualizado.",
            L"PlayView", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 1;
    }

    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_BAR_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&controls);

    WNDCLASSEXW pipClass{};
    pipClass.cbSize = sizeof(pipClass);
    pipClass.style = CS_DBLCLKS;
    pipClass.lpfnWndProc = PipWindowProc;
    pipClass.hInstance = instance;
    pipClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    pipClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pipClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    pipClass.lpszClassName = kPipWindowClass;
    RegisterClassExW(&pipClass);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) {
        MFShutdown();
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, kWindowClass, kAppName,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        MFShutdown();
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 1;
    }
    g_windowMode = 0;
    if (!g_activeProfileName.empty()) SetWindowTextW(hwnd, (L"PlayView | " + g_activeProfileName).c_str());

    // WM_SIZE can run during CreateWindowEx before saved placement is applied.
    // Restore the persisted flag here so that the initial size notifications
    // cannot overwrite the user's last maximized state.
    g_windowMaximized = g_settings.windowMaximized;
    if (!MonitorFromRect(&g_restoreRect, MONITOR_DEFAULTTONULL)) {
        g_restoreRect = { 100, 100, 1280, 860 };
        g_windowMaximized = false;
    }
    WINDOWPLACEMENT startupPlacement{};
    startupPlacement.length = sizeof(startupPlacement);
    startupPlacement.showCmd = (g_settings.windowMode == 0 && g_windowMaximized)
        ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    startupPlacement.rcNormalPosition = g_restoreRect;
    SetWindowPlacement(hwnd, &startupPlacement);
    const int startupShow = (g_settings.windowMode == 0 && g_windowMaximized)
        ? SW_SHOWMAXIMIZED
        : (showCommand == SW_SHOWMINIMIZED ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL);
    ShowWindow(hwnd, startupShow);
    UpdateWindow(hwnd);
    if (g_settings.windowMode != 0) {
        g_preserveRestoreRect = true;
        SetWindowMode(g_settings.windowMode);
        g_preserveRestoreRect = false;
    } else {
        UpdateOverlayVisibility(true);
    }

    ACCEL acceleratorEntries[] = {
        { FVIRTKEY | FNOINVERT, VK_F5, IDC_RELOAD },
        { FVIRTKEY | FNOINVERT, VK_F11, IDM_VIEW_FULLSCREEN },
        { FVIRTKEY | FNOINVERT, VK_ESCAPE, IDM_VIEW_WINDOW },
        { FVIRTKEY | FCONTROL | FSHIFT | FNOINVERT, 'P', IDM_VIEW_PIP }
    };
    HACCEL accelerators = CreateAcceleratorTableW(acceleratorEntries, static_cast<int>(std::size(acceleratorEntries)));

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!accelerators || !TranslateAcceleratorW(hwnd, accelerators, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (accelerators) DestroyAcceleratorTable(accelerators);

    g_videoCapture.Stop();
    g_audioCapture.Stop();
    MFShutdown();
    if (SUCCEEDED(comHr)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
