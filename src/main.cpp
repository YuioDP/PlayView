#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <mfapi.h>
#include <mfidl.h>
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
constexpr UINT_PTR TIMER_RECONNECT = 1;

constexpr int IDC_VIDEO_DEVICE = 1001;
constexpr int IDC_AUDIO_DEVICE = 1002;
constexpr int IDC_RESOLUTION = 1003;
constexpr int IDC_RELOAD = 1004;
constexpr int IDC_VOLUME = 1005;
constexpr int IDC_MUTE = 1006;
constexpr int IDC_STATUS = 1007;
constexpr int IDC_PIP = 1009;
constexpr int IDC_SYNC = 1008;
constexpr int IDC_VIDEO_LABEL = 1010;
constexpr int IDC_AUDIO_LABEL = 1011;
constexpr int IDC_RES_LABEL = 1012;
constexpr int IDC_VOLUME_LABEL = 1013;
constexpr int IDM_VIEW_WINDOW = 2001;
constexpr int IDM_VIEW_BORDERLESS = 2002;
constexpr int IDM_VIEW_FULLSCREEN = 2003;
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
    int windowMode = 0; // 0 windowed, 1 borderless, 2 fullscreen
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

HWND g_hwnd = nullptr;
HWND g_pipHwnd = nullptr;
HWND g_videoCombo = nullptr;
HWND g_audioCombo = nullptr;
HWND g_resolutionCombo = nullptr;
HWND g_volumeSlider = nullptr;
HWND g_muteButton = nullptr;
HWND g_status = nullptr;
HFONT g_uiFont = nullptr;

Settings g_settings;
std::vector<DeviceInfo> g_videoDevices;
std::vector<DeviceInfo> g_audioDevices;
std::vector<VideoMode> g_videoModes;
std::mutex g_frameMutex;
FrameData g_latestFrame;
std::atomic_bool g_frameMessagePending{ false };
std::atomic_int g_volume{ 80 };
std::atomic_bool g_muted{ false };
std::atomic_bool g_videoHasFrame{ false };
std::atomic<ULONGLONG> g_lastVideoFrameTick{ 0 };
ULONGLONG g_videoStartTick = 0;
std::wstring g_videoStatus = L"Selecciona una capturadora para comenzar.";
std::wstring g_audioStatus = L"Audio sin configurar.";
ULONGLONG g_nextVideoRetry = 0;
ULONGLONG g_nextAudioRetry = 0;
RECT g_restoreRect{ 0, 0, 1180, 760 };
int g_windowMode = 0;
bool g_isChangingWindowMode = false;
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
    g_settings.windowMode = std::clamp(ReadIniInt(L"Window", L"Mode", 0), 0, 2);
    g_volume.store(g_settings.volume);
    g_muted.store(g_settings.muted);
    g_settingsPath = previous;
}

void LoadSettings() {
    LoadSettingsFrom(g_settingsPath);
}

void SaveSettingsTo(const std::filesystem::path& path) {
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
    g_settingsPath = previous;
}

void SaveSettings() {
    SaveSettingsTo(CurrentSettingsFile());
}

std::vector<ProfileEntry> EnumerateProfiles() {
    std::vector<ProfileEntry> profiles;
    std::error_code ec;
    if (!std::filesystem::exists(g_profilesDirectory, ec)) return profiles;
    for (const auto& item : std::filesystem::directory_iterator(g_profilesDirectory, ec)) {
        if (ec || !item.is_regular_file()) continue;
        if (_wcsicmp(item.path().extension().c_str(), L".ini") != 0) continue;
        ProfileEntry profile;
        profile.path = item.path();
        wchar_t value[32768]{};
        GetPrivateProfileStringW(L"Profile", L"Name", item.path().stem().c_str(), value,
            static_cast<DWORD>(std::size(value)), item.path().c_str());
        profile.name = value;
        wchar_t iconValue[32768]{};
        GetPrivateProfileStringW(L"Profile", L"IconPath", L"", iconValue,
            static_cast<DWORD>(std::size(iconValue)), item.path().c_str());
        if (iconValue[0]) profile.iconPath = iconValue;
        if (profile.name.empty()) profile.name = item.path().stem().wstring();
        profiles.push_back(std::move(profile));
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
    std::filesystem::path profilePath = file;
    if (profilePath.extension().empty()) profilePath += L".ini";
    std::error_code pathError;
    const auto profileRoot = std::filesystem::weakly_canonical(g_profilesDirectory, pathError);
    const auto chosenRoot = std::filesystem::weakly_canonical(profilePath.parent_path(), pathError);
    if (pathError || _wcsicmp(chosenRoot.c_str(), profileRoot.c_str()) != 0) {
        MessageBoxW(g_hwnd, L"Los perfiles deben guardarse dentro de la carpeta de datos de PlayView.", L"PlayView", MB_OK | MB_ICONWARNING);
        return;
    }
    const std::wstring profileName = profilePath.stem().wstring();

    std::filesystem::path iconSource;
    if (MessageBoxW(g_hwnd, L"¿Deseas asignar un logo personalizado a este perfil? Se admiten .ico, .png, .jpg, .jpeg y .bmp. Se convertirán al formato de icono de Windows.",
        L"Logo del perfil", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        if (!PickIconFile(iconSource)) iconSource.clear();
    }

    std::filesystem::path iconDestination;
    if (!iconSource.empty()) {
        iconDestination = g_profileIconsDirectory / (profilePath.stem().wstring() + L".ico");
        if (!SaveProfileIcon(iconSource, iconDestination)) {
            MessageBoxW(g_hwnd, L"No se pudo copiar el icono. Se utilizará el icono predeterminado.", L"PlayView", MB_OK | MB_ICONWARNING);
            iconDestination.clear();
        }
    }

    SaveSettingsTo(profilePath);
    WritePrivateProfileStringW(L"Profile", L"Name", profileName.c_str(), profilePath.c_str());
    WritePrivateProfileStringW(L"Profile", L"IconPath", iconDestination.empty() ? L"" : iconDestination.c_str(), profilePath.c_str());
    ProfileEntry profile{ profileName, profilePath, iconDestination };
    CreateAppMenu(g_hwnd);

    if (MessageBoxW(g_hwnd, L"El perfil se guardó. ¿Deseas crear ahora un acceso directo en el escritorio con su nombre y logo?",
        L"Perfil creado", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        if (CreateProfileShortcut(profile)) MessageBoxW(g_hwnd, L"Acceso directo creado en el escritorio.", L"PlayView", MB_OK | MB_ICONINFORMATION);
        else MessageBoxW(g_hwnd, L"No se pudo crear el acceso directo.", L"PlayView", MB_OK | MB_ICONWARNING);
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
    if (!g_status) return;
    std::wstring combined = g_videoStatus;
    if (!g_audioStatus.empty()) combined += L"    |    " + g_audioStatus;
    SetWindowTextW(g_status, combined.c_str());
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
            BYTE* data = nullptr;
            DWORD maxLength = 0, currentLength = 0;
            hr = buffer->Lock(&data, &maxLength, &currentLength);
            if (FAILED(hr)) continue;
            if (!data) { buffer->Unlock(); continue; }

            const size_t rowBytes = static_cast<size_t>(frameWidth) * 4;
            const size_t absStride = static_cast<size_t>(stride < 0 ? -stride : stride);
            const size_t requiredBytes = absStride * frameHeight;
            if (currentLength >= requiredBytes && absStride >= rowBytes) {
                {
                    std::lock_guard<std::mutex> lock(g_frameMutex);
                    g_latestFrame.width = static_cast<int>(frameWidth);
                    g_latestFrame.height = static_cast<int>(frameHeight);
                    g_latestFrame.pixels.resize(rowBytes * frameHeight);
                    for (UINT32 row = 0; row < frameHeight; ++row) {
                        const UINT32 sourceRow = stride < 0 ? (frameHeight - 1 - row) : row;
                        std::memcpy(g_latestFrame.pixels.data() + static_cast<size_t>(row) * rowBytes,
                                    data + static_cast<size_t>(sourceRow) * absStride, rowBytes);
                    }
                }
                g_videoHasFrame.store(true);
                g_lastVideoFrameTick.store(GetTickCount64());
                if (!signaledReady) {
                    signaledReady = true;
                    PostMessageW(notifyWindow, WM_APP_VIDEO_READY, 0, 0);
                }
                if (!g_frameMessagePending.exchange(true)) PostMessageW(notifyWindow, WM_APP_FRAME, 0, 0);
            }
            buffer->Unlock();
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
    SaveSettings();
    if (g_videoCapture.IsRunning()) g_videoCapture.Stop();
    if (g_audioCapture.IsRunning()) g_audioCapture.Stop();
    g_activeProfilePath = profile.path;
    g_activeProfileName = profile.name;
    LoadSettingsFrom(g_activeProfilePath);
    ApplyPipZOrder();
    CreateAppMenu(g_hwnd);
    g_windowMode = 0;
    SetWindowTextW(g_hwnd, (L"PlayView | " + g_activeProfileName).c_str());
    SendMessageW(g_volumeSlider, TBM_SETPOS, TRUE, g_settings.volume);
    SetWindowTextW(GetDlgItem(g_hwnd, IDC_VOLUME_LABEL), (L"Volumen: " + std::to_wstring(g_settings.volume) + L"%").c_str());
    SetWindowTextW(g_muteButton, g_settings.muted ? L"Activar" : L"Silenciar");
    RefreshDevices(true);
    if (g_settings.windowMode != 0) SetWindowMode(g_settings.windowMode);
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
    CreateAppMenu(g_hwnd);
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
    CreateAppMenu(g_hwnd);
}


void SetControlFont(HWND control) {
    if (control && g_uiFont) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
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
    InvalidateRect(g_hwnd, nullptr, FALSE);
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
    mode = std::clamp(mode, 0, 2);
    if (!g_hwnd || g_isChangingWindowMode || mode == g_windowMode) return;
    g_isChangingWindowMode = true;
    if (g_windowMode == 0) GetWindowRect(g_hwnd, &g_restoreRect);

    LONG_PTR style = GetWindowLongPtrW(g_hwnd, GWL_STYLE);
    if (mode == 0) {
        style &= ~static_cast<LONG_PTR>(WS_POPUP);
        style |= WS_OVERLAPPEDWINDOW;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, style);
        SetWindowPos(g_hwnd, nullptr, g_restoreRect.left, g_restoreRect.top,
            g_restoreRect.right - g_restoreRect.left, g_restoreRect.bottom - g_restoreRect.top,
            SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        style &= ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW);
        style |= WS_POPUP;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, style);
        if (mode == 1) {
            SetWindowPos(g_hwnd, nullptr, g_restoreRect.left, g_restoreRect.top,
                g_restoreRect.right - g_restoreRect.left, g_restoreRect.bottom - g_restoreRect.top,
                SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        } else {
            MONITORINFO monitorInfo{ sizeof(monitorInfo) };
            GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
            const RECT target = monitorInfo.rcMonitor;
            SetWindowPos(g_hwnd, HWND_TOP, target.left, target.top, target.right - target.left,
                target.bottom - target.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
    }
    g_windowMode = mode;
    g_settings.windowMode = mode;
    SaveSettings();
    g_isChangingWindowMode = false;
}

void CreateAppMenu(HWND hwnd) {
    HMENU oldMenu = GetMenu(hwnd);
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU view = CreatePopupMenu();
    HMENU help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"Salir");
    AppendMenuW(view, MF_STRING, IDM_VIEW_WINDOW, L"Ventana");
    AppendMenuW(view, MF_STRING, IDM_VIEW_BORDERLESS, L"Ventana sin bordes");
    AppendMenuW(view, MF_STRING, IDM_VIEW_FULLSCREEN, L"Pantalla completa\tF11");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, IDM_VIEW_PIP, L"Picture in Picture\tCtrl+Mayús+P");
    AppendMenuW(view, MF_STRING | (g_pipAlwaysOnTop ? MF_CHECKED : MF_UNCHECKED), IDM_PIP_TOPMOST, L"PiP siempre visible");
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"Acerca de PlayView");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"Archivo");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"Ver");
    RebuildProfilesMenu(menu);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Ayuda");
    SetMenu(hwnd, menu);
    DrawMenuBar(hwnd);
    if (oldMenu) DestroyMenu(oldMenu);
}

void LayoutControls(int width, int /*height*/) {
    const int margin = 12;
    const int row1 = 8;
    const int labelW = 75;
    const int comboH = 28;
    MoveWindow(g_videoCombo ? GetDlgItem(g_hwnd, IDC_VIDEO_LABEL) : nullptr, margin, row1 + 4, labelW, 22, TRUE);
    if (g_videoCombo) MoveWindow(g_videoCombo, 82, row1, std::max(180, width / 3), comboH, TRUE);
    const int audioLabelX = 82 + std::max(180, width / 3) + 12;
    MoveWindow(GetDlgItem(g_hwnd, IDC_AUDIO_LABEL), audioLabelX, row1 + 4, 48, 22, TRUE);
    const int audioComboX = audioLabelX + 48;
    const int audioComboW = std::max(150, width - audioComboX - 315);
    if (g_audioCombo) MoveWindow(g_audioCombo, audioComboX, row1, audioComboW, comboH, TRUE);
    const int resLabelX = audioComboX + audioComboW + 10;
    MoveWindow(GetDlgItem(g_hwnd, IDC_RES_LABEL), resLabelX, row1 + 4, 75, 22, TRUE);
    if (g_resolutionCombo) MoveWindow(g_resolutionCombo, resLabelX + 76, row1, 100, comboH, TRUE);
    if (GetDlgItem(g_hwnd, IDC_RELOAD)) MoveWindow(GetDlgItem(g_hwnd, IDC_RELOAD), width - 108, row1, 96, 28, TRUE);

    MoveWindow(GetDlgItem(g_hwnd, IDC_VOLUME_LABEL), margin, 52, 90, 24, TRUE);
    if (g_volumeSlider) MoveWindow(g_volumeSlider, 104, 48, 154, 32, TRUE);
    if (g_muteButton) MoveWindow(g_muteButton, 270, 49, 82, 28, TRUE);
    if (GetDlgItem(g_hwnd, IDC_SYNC)) MoveWindow(GetDlgItem(g_hwnd, IDC_SYNC), 360, 49, 120, 28, TRUE);
    if (GetDlgItem(g_hwnd, IDC_PIP)) MoveWindow(GetDlgItem(g_hwnd, IDC_PIP), 488, 49, 82, 28, TRUE);
    if (g_status) MoveWindow(g_status, 580, 50, std::max(120, width - 595), 26, TRUE);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void DrawPreview(HDC dc, const RECT& client, bool compact = false) {
    RECT area = compact ? client : RECT{ 0, 94, client.right, std::max(95L, client.bottom - 38) };
    HBRUSH black = CreateSolidBrush(RGB(7, 10, 16));
    FillRect(dc, &area, black);
    DeleteObject(black);

    bool hasFrame = false;
    {
        std::lock_guard<std::mutex> lock(g_frameMutex);
        if (g_latestFrame.width > 0 && g_latestFrame.height > 0 && !g_latestFrame.pixels.empty()) {
            hasFrame = true;
            const int viewWidth = area.right - area.left;
            const int viewHeight = area.bottom - area.top;
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
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, x, y, drawWidth, drawHeight, 0, 0, g_latestFrame.width, g_latestFrame.height,
                g_latestFrame.pixels.data(), &bitmapInfo, DIB_RGB_COLORS, SRCCOPY);
        }
    }
    if (!hasFrame) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(190, 205, 220));
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, g_uiFont));
        RECT textRect = area;
        DrawTextW(dc, g_videoStatus.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
    }
}

LRESULT CALLBACK PipWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        DrawPreview(dc, client, true);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(hwnd, message, wParam, lParam);
        if (hit == HTCLIENT) return HTCAPTION;
        return hit;
    }
    case WM_NCLBUTTONDBLCLK:
        if (g_hwnd) ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
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
        if (LOWORD(wParam) == IDM_PIP_CLOSE) { ShowWindow(hwnd, SW_HIDE); return 0; }
        if (LOWORD(wParam) == IDM_PIP_TOPMOST) { TogglePipTopmost(); return 0; }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { ShowWindow(hwnd, SW_HIDE); return 0; }
        break;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
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
        DWORD exStyle = WS_EX_TOOLWINDOW;
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
    CreateAppMenu(g_hwnd);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        g_uiFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        AddControl(0, L"STATIC", L"Capturadora", SS_LEFT, 0, 0, 0, 0, IDC_VIDEO_LABEL);
        g_videoCombo = AddControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 0, 0, IDC_VIDEO_DEVICE);
        AddControl(0, L"STATIC", L"Audio", SS_LEFT, 0, 0, 0, 0, IDC_AUDIO_LABEL);
        g_audioCombo = AddControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 0, 0, IDC_AUDIO_DEVICE);
        SendMessageW(g_videoCombo, CB_SETMINVISIBLE, 8, 0);
        SendMessageW(g_audioCombo, CB_SETMINVISIBLE, 8, 0);
        AddControl(0, L"STATIC", L"Resolución", SS_LEFT, 0, 0, 0, 0, IDC_RES_LABEL);
        g_resolutionCombo = AddControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 0, 0, IDC_RESOLUTION);
        AddControl(0, L"BUTTON", L"Recargar (F5)", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_RELOAD);
        AddControl(0, L"BUTTON", L"PiP", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_PIP);
        AddControl(0, L"STATIC", L"Volumen", SS_LEFT, 0, 0, 0, 0, IDC_VOLUME_LABEL);
        g_volumeSlider = AddControl(0, TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, 0, 0, 0, 0, IDC_VOLUME);
        SendMessageW(g_volumeSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(g_volumeSlider, TBM_SETPOS, TRUE, g_settings.volume);
        SetWindowTextW(GetDlgItem(hwnd, IDC_VOLUME_LABEL), (L"Volumen: " + std::to_wstring(g_settings.volume) + L"%").c_str());
        g_muteButton = AddControl(0, L"BUTTON", g_settings.muted ? L"Activar" : L"Silenciar", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_MUTE);
        AddControl(0, L"BUTTON", L"Resincronizar", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_SYNC);
        g_status = AddControl(0, L"STATIC", L"Iniciando PlayView...", SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, IDC_STATUS);
        CreateAppMenu(hwnd);
        RefreshDevices(true);
        SetTimer(hwnd, TIMER_RECONNECT, 2500, nullptr);
        return 0;
    }
    case WM_SIZE:
        LayoutControls(LOWORD(lParam), HIWORD(lParam));
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
            SaveSettings();
            g_videoCapture.Stop();
            g_audioCapture.Stop();
            g_activeProfilePath.clear();
            g_activeProfileName.clear();
            LoadSettings();
            ApplyPipZOrder();
            CreateAppMenu(g_hwnd);
            g_windowMode = 0;
            SetWindowTextW(g_hwnd, L"PlayView");
            SendMessageW(g_volumeSlider, TBM_SETPOS, TRUE, g_settings.volume);
            SetWindowTextW(GetDlgItem(g_hwnd, IDC_VOLUME_LABEL), (L"Volumen: " + std::to_wstring(g_settings.volume) + L"%").c_str());
            SetWindowTextW(g_muteButton, g_settings.muted ? L"Activar" : L"Silenciar");
            RefreshDevices(true);
            if (g_settings.windowMode != 0) SetWindowMode(g_settings.windowMode);
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
        case IDM_VIEW_WINDOW: SetWindowMode(0); return 0;
        case IDM_VIEW_BORDERLESS: SetWindowMode(1); return 0;
        case IDM_VIEW_FULLSCREEN: SetWindowMode(g_windowMode == 2 ? 0 : 2); return 0;
        case IDM_EXIT: DestroyWindow(hwnd); return 0;
        case IDM_ABOUT:
            MessageBoxW(hwnd, L"PlayView 0.2.1\nMonitor ligero para capturadoras de video.\n\nCaptura mediante Windows Media Foundation.",
                L"Acerca de PlayView", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == g_volumeSlider) {
            g_settings.volume = static_cast<int>(SendMessageW(g_volumeSlider, TBM_GETPOS, 0, 0));
            g_volume.store(g_settings.volume);
            SetWindowTextW(GetDlgItem(hwnd, IDC_VOLUME_LABEL), (L"Volumen: " + std::to_wstring(g_settings.volume) + L"%").c_str());
            SaveSettings();
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
        if (wParam == VK_ESCAPE && g_windowMode != 0) { SetWindowMode(0); return 0; }
        break;
    case WM_APP_FRAME:
        g_frameMessagePending.store(false);
        InvalidateRect(hwnd, nullptr, FALSE);
        if (g_pipHwnd && IsWindowVisible(g_pipHwnd)) InvalidateRect(g_pipHwnd, nullptr, FALSE);
        return 0;
    case WM_APP_VIDEO_READY:
        g_videoStatus = L"Captura activa";
        UpdateStatusText();
        return 0;
    case WM_APP_VIDEO_LOST:
        g_videoStatus = L"Se perdió la señal o la capturadora se desconectó. Buscando el mismo dispositivo...";
        g_videoHasFrame.store(false);
        g_nextVideoRetry = GetTickCount64() + 2500;
        UpdateStatusText();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_APP_AUDIO_LOST:
        g_audioStatus = L"Audio desconectado. Buscando el mismo dispositivo...";
        g_nextAudioRetry = GetTickCount64() + 2500;
        UpdateStatusText();
        return 0;
    case WM_TIMER:
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
                InvalidateRect(hwnd, nullptr, FALSE);
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
        RECT client{};
        GetClientRect(hwnd, &client);
        DrawPreview(dc, client);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        KillTimer(hwnd, TIMER_RECONNECT);
        if (g_pipHwnd) DestroyWindow(g_pipHwnd);
        g_videoCapture.Stop();
        g_audioCapture.Stop();
        SaveSettings();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
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

    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_BAR_CLASSES };
    InitCommonControlsEx(&controls);

    WNDCLASSEXW pipClass{};
    pipClass.cbSize = sizeof(pipClass);
    pipClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    pipClass.lpfnWndProc = PipWindowProc;
    pipClass.hInstance = instance;
    pipClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    pipClass.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    pipClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    pipClass.lpszClassName = kPipWindowClass;
    RegisterClassExW(&pipClass);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
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

    HWND hwnd = CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        MFShutdown();
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 1;
    }
    g_windowMode = 0;
    if (!g_activeProfileName.empty()) SetWindowTextW(hwnd, (L"PlayView | " + g_activeProfileName).c_str());
    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);
    if (g_settings.windowMode != 0) SetWindowMode(g_settings.windowMode);

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
