#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite.h>
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
#include <condition_variable>
#include <atomic>
#include <deque>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iterator>
#include <memory>
#include <utility>
#include <array>
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
constexpr wchar_t kAboutWindowClass[] = L"PlayViewAboutWindow";
constexpr UINT WM_APP_FRAME = WM_APP + 1;
constexpr UINT WM_APP_VIDEO_READY = WM_APP + 2;
constexpr UINT WM_APP_VIDEO_LOST = WM_APP + 3;
constexpr UINT WM_APP_AUDIO_LOST = WM_APP + 4;
constexpr UINT WM_APP_REBUILD_MENU = WM_APP + 5;
constexpr UINT WM_APP_CLEAR_FRAME = WM_APP + 6;
constexpr UINT WM_APP_VIDEO_MODES = WM_APP + 7;
constexpr UINT WM_APP_OVERLAY_RECHECK = WM_APP + 8;
constexpr UINT_PTR TIMER_RECONNECT = 1;
constexpr UINT_PTR TIMER_STATS = 3;

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
constexpr int IDM_LANGUAGE_AUTOMATIC = 2700;
constexpr int IDM_LANGUAGE_SPANISH = 2701;
constexpr int IDM_LANGUAGE_ENGLISH = 2702;
constexpr int IDM_SHOW_PERFORMANCE_STATS = 2703;
constexpr int IDC_ABOUT_OK = 2800;
constexpr int ID_PROFILE_MENU_LIMIT = 100;

enum class LanguagePreference : int { Automatic = 0, Spanish = 1, English = 2 };
enum class UiLanguage { Spanish, English };

enum class TextId {
    VideoSelect, AudioUnconfigured,
    SettingsMenu, LanguageMenu, LanguageAutomatic, LanguageSpanish, LanguageEnglish, PerformanceStatsMenu,
    FileMenu, ViewMenu, HelpMenu, ShowToolbar, Exit,
    Windowed, Fullscreen, PipMenu, PipAlwaysOnTop, About,
    Profiles, SaveCurrentProfile, UseMainSettings, NoSavedProfiles,
    LoadProfile, CreateDesktopShortcut, ChangeProfileIcon, DeleteProfile,
    ImageFileFilter, SelectProfileLogo, ProfileFileFilter, SavePlayViewProfile,
    ProfilesMustStayInAppData, InvalidProfileName, AskProfileLogo, ProfileLogoTitle,
    IconCopyFailedDefault, ProfileSavedAskShortcut, ProfileCreated,
    ShortcutCreated, ShortcutCreationFailed, ProfileSaveFileError,
    ProfileSaveRuntimeError, ProfileSaveUnexpectedError,
    CannotDeleteActiveProfile, DeleteProfileQuestion, DeleteProfileTitle,
    IconCopyFailed, NotFoundPrefix, AutomaticResolution,
    SelectCaptureDevice, SelectCaptureAudio,
    VideoFoundStarting, VideoMissingPrefix, UnknownDevice, NoCaptureDevices,
    AudioConnected, AudioMissing, VideoConnecting, VideoReconnecting,
    VideoActive, VideoSignalLostRetry, AudioDisconnectedRetry,
    VideoWaitingFrames, CheckHdmiInput, VideoStoppedFrames, PressF5ToReload,
    CapturesResynced, ReloadButton, PipButton, MuteButton, UnmuteButton,
    ResyncButton, CaptureDeviceLabel, AudioLabel, ResolutionLabel, VolumeLabel,
    HidePip, AlwaysOnTop, PipCreateFailed,
    AboutBody, AboutTitle, MediaFoundationStartupError, RendererUnavailable,
    ShowPip, WindowTitleProfile
};

LanguagePreference g_languagePreference = LanguagePreference::Automatic;
UiLanguage g_uiLanguage = UiLanguage::Spanish;

const wchar_t* Tr(TextId id) {
    const bool en = g_uiLanguage == UiLanguage::English;
    switch (id) {
    case TextId::VideoSelect: return en ? L"Select a capture device to begin." : L"Selecciona una capturadora para comenzar.";
    case TextId::AudioUnconfigured: return en ? L"Audio not configured." : L"Audio sin configurar.";
    case TextId::SettingsMenu: return en ? L"Settings" : L"Ajustes";
    case TextId::LanguageMenu: return en ? L"Language" : L"Idioma";
    case TextId::LanguageAutomatic: return en ? L"Automatic (Windows display language)" : L"Automático (idioma de Windows)";
    case TextId::LanguageSpanish: return L"Español";
    case TextId::LanguageEnglish: return L"English";
    case TextId::PerformanceStatsMenu: return en ? L"Show performance statistics" : L"Mostrar estadísticas de rendimiento";
    case TextId::FileMenu: return en ? L"File" : L"Archivo";
    case TextId::ViewMenu: return en ? L"View" : L"Ver";
    case TextId::HelpMenu: return en ? L"Help" : L"Ayuda";
    case TextId::ShowToolbar: return en ? L"Show options bar" : L"Mostrar barra de opciones";
    case TextId::Exit: return en ? L"Exit" : L"Salir";
    case TextId::Windowed: return en ? L"Windowed" : L"Ventana";
    case TextId::Fullscreen: return en ? L"Fullscreen\tF11" : L"Pantalla completa\tF11";
    case TextId::PipMenu: return en ? L"Picture in Picture\tCtrl+Shift+P" : L"Imagen en imagen\tCtrl+Mayús+P";
    case TextId::PipAlwaysOnTop: return en ? L"PiP always on top" : L"PiP siempre visible";
    case TextId::About: return en ? L"About PlayView" : L"Acerca de PlayView";
    case TextId::Profiles: return en ? L"Profiles" : L"Perfiles";
    case TextId::SaveCurrentProfile: return en ? L"Save current settings as a profile..." : L"Guardar configuración actual como perfil...";
    case TextId::UseMainSettings: return en ? L"Use main settings" : L"Usar configuración principal";
    case TextId::NoSavedProfiles: return en ? L"No saved profiles" : L"No hay perfiles guardados";
    case TextId::LoadProfile: return en ? L"Load profile" : L"Cargar perfil";
    case TextId::CreateDesktopShortcut: return en ? L"Create desktop shortcut" : L"Crear acceso directo en el escritorio";
    case TextId::ChangeProfileIcon: return en ? L"Change profile icon" : L"Cambiar logo del perfil";
    case TextId::DeleteProfile: return en ? L"Delete profile" : L"Eliminar perfil";
    case TextId::ImageFileFilter: return en ? L"Images and icons (*.ico;*.png;*.jpg;*.jpeg;*.bmp)\0*.ico;*.png;*.jpg;*.jpeg;*.bmp\0All files (*.*)\0*.*\0" : L"Imágenes e iconos (*.ico;*.png;*.jpg;*.jpeg;*.bmp)\0*.ico;*.png;*.jpg;*.jpeg;*.bmp\0Todos los archivos (*.*)\0*.*\0";
    case TextId::SelectProfileLogo: return en ? L"Select profile logo" : L"Seleccionar logo del perfil";
    case TextId::ProfileFileFilter: return en ? L"PlayView profile (*.ini)\0*.ini\0" : L"Perfil PlayView (*.ini)\0*.ini\0";
    case TextId::SavePlayViewProfile: return en ? L"Save PlayView profile" : L"Guardar perfil de PlayView";
    case TextId::ProfilesMustStayInAppData: return en ? L"Profiles must be saved inside the PlayView data folder." : L"Los perfiles deben guardarse dentro de la carpeta de datos de PlayView.";
    case TextId::InvalidProfileName: return en ? L"The profile name is not valid." : L"El nombre del perfil no es válido.";
    case TextId::AskProfileLogo: return en ? L"Would you like to assign a custom logo to this profile? Supported formats: .ico, .png, .jpg, .jpeg and .bmp." : L"¿Deseas asignar un logo personalizado a este perfil? Se admiten .ico, .png, .jpg, .jpeg y .bmp.";
    case TextId::ProfileLogoTitle: return en ? L"Profile logo" : L"Logo del perfil";
    case TextId::IconCopyFailedDefault: return en ? L"The icon could not be copied. The default icon will be used." : L"No se pudo copiar el icono. Se utilizará el icono predeterminado.";
    case TextId::ProfileSavedAskShortcut: return en ? L"The profile was saved. Would you like to create a desktop shortcut with its name and logo?" : L"El perfil se guardó. ¿Deseas crear ahora un acceso directo en el escritorio con su nombre y logo?";
    case TextId::ProfileCreated: return en ? L"Profile created" : L"Perfil creado";
    case TextId::ShortcutCreated: return en ? L"Desktop shortcut created." : L"Acceso directo creado en el escritorio.";
    case TextId::ShortcutCreationFailed: return en ? L"Could not create the shortcut." : L"No se pudo crear el acceso directo.";
    case TextId::ProfileSaveFileError: return en ? L"A file error occurred while saving the profile. See playview.log for details." : L"Ocurrió un problema al guardar el perfil. Consulta playview.log para más detalles.";
    case TextId::ProfileSaveRuntimeError: return en ? L"An error occurred while saving the profile. The application will remain open." : L"Ocurrió un error al guardar el perfil. La aplicación continuará abierta.";
    case TextId::ProfileSaveUnexpectedError: return en ? L"An unexpected error occurred while saving the profile. The application will remain open." : L"Ocurrió un error inesperado al guardar el perfil. La aplicación continuará abierta.";
    case TextId::CannotDeleteActiveProfile: return en ? L"You cannot delete the profile currently in use. Switch to another profile first." : L"No puedes eliminar el perfil que está en uso. Cambia primero a otro perfil.";
    case TextId::DeleteProfileQuestion: return en ? L"Delete profile '" : L"¿Eliminar el perfil '";
    case TextId::DeleteProfileTitle: return en ? L"Delete profile" : L"Eliminar perfil";
    case TextId::IconCopyFailed: return en ? L"The selected icon could not be copied." : L"No se pudo copiar el icono seleccionado.";
    case TextId::NotFoundPrefix: return en ? L"Not found: " : L"No encontrado: ";
    case TextId::AutomaticResolution: return en ? L"Automatic" : L"Automática";
    case TextId::SelectCaptureDevice: return en ? L"Select a capture device..." : L"Selecciona una capturadora...";
    case TextId::SelectCaptureAudio: return en ? L"Select capture card audio..." : L"Selecciona el audio de la capturadora...";
    case TextId::VideoFoundStarting: return en ? L"Capture device found. Starting video..." : L"Capturadora encontrada. Iniciando señal...";
    case TextId::VideoMissingPrefix: return en ? L"Saved capture device not found: " : L"No se encontró la capturadora guardada: ";
    case TextId::UnknownDevice: return en ? L"unknown device" : L"dispositivo desconocido";
    case TextId::NoCaptureDevices: return en ? L"No capture devices detected." : L"No se detectaron capturadoras.";
    case TextId::AudioConnected: return en ? L"Audio connected" : L"Audio conectado";
    case TextId::AudioMissing: return en ? L"Saved audio device not found" : L"No se encontró el audio guardado";
    case TextId::VideoConnecting: return en ? L"Connecting to " : L"Conectando con ";
    case TextId::VideoReconnecting: return en ? L"Capture device recovered. Reconnecting..." : L"Capturadora recuperada. Reconectando...";
    case TextId::VideoActive: return en ? L"Capture active" : L"Captura activa";
    case TextId::VideoSignalLostRetry: return en ? L"Signal lost or capture device disconnected. Looking for the same device..." : L"Se perdió la señal o la capturadora se desconectó. Buscando el mismo dispositivo...";
    case TextId::AudioDisconnectedRetry: return en ? L"Audio disconnected. Looking for the same device..." : L"Audio desconectado. Buscando el mismo dispositivo...";
    case TextId::VideoWaitingFrames: return en ? L"Waiting for video frames. Check the capture card HDMI input." : L"Esperando fotogramas. Comprueba la entrada HDMI de la capturadora.";
    case TextId::CheckHdmiInput: return en ? L"Check the capture card HDMI input." : L"Comprueba la entrada HDMI de la capturadora.";
    case TextId::VideoStoppedFrames: return en ? L"The capture device stopped sending frames. Press F5 to refresh." : L"La capturadora dejó de enviar fotogramas. Pulsa F5 para recargar.";
    case TextId::PressF5ToReload: return en ? L"Press F5 to refresh." : L"Pulsa F5 para recargar.";
    case TextId::CapturesResynced: return en ? L"Captures restarted for resynchronization" : L"Capturas reiniciadas para resincronización";
    case TextId::ReloadButton: return en ? L"Refresh (F5)" : L"Recargar (F5)";
    case TextId::PipButton: return L"PiP";
    case TextId::MuteButton: return en ? L"Mute" : L"Silenciar";
    case TextId::UnmuteButton: return en ? L"Unmute" : L"Activar";
    case TextId::ResyncButton: return en ? L"Resync" : L"Resincronizar";
    case TextId::CaptureDeviceLabel: return en ? L"Capture" : L"Capturadora";
    case TextId::AudioLabel: return L"Audio";
    case TextId::ResolutionLabel: return en ? L"Resolution" : L"Resolución";
    case TextId::VolumeLabel: return en ? L"Volume: " : L"Volumen: ";
    case TextId::HidePip: return en ? L"Hide Picture in Picture" : L"Ocultar imagen en imagen";
    case TextId::AlwaysOnTop: return en ? L"Always on top" : L"Siempre visible";
    case TextId::PipCreateFailed: return en ? L"Could not create the Picture in Picture window." : L"No se pudo crear la ventana de imagen en imagen.";
    case TextId::AboutBody: return en ? L"PlayView 1.2.0\nA lightweight monitor for video capture devices.\n\nCapture powered by Windows Media Foundation and Direct3D 11." : L"PlayView 1.2.0\nMonitor ligero para capturadoras de video.\n\nCaptura mediante Windows Media Foundation y Direct3D 11.";
    case TextId::RendererUnavailable: return en ? L"The Direct3D 11 renderer could not be initialized. Check playview.log for details." : L"No se pudo iniciar el renderizador Direct3D 11. Consulta playview.log para más detalles.";
    case TextId::AboutTitle: return en ? L"About PlayView" : L"Acerca de PlayView";
    case TextId::MediaFoundationStartupError: return en ? L"Could not initialize Windows Media Foundation. Make sure Windows is up to date." : L"No se pudo iniciar Windows Media Foundation. Comprueba que Windows esté actualizado.";
    case TextId::ShowPip: return en ? L"Show Picture in Picture" : L"Mostrar imagen en imagen";
    case TextId::WindowTitleProfile: return en ? L"PlayView | " : L"PlayView | ";
    }
    return L"";
}

UiLanguage ResolveUiLanguage(LanguagePreference preference) {
    if (preference == LanguagePreference::Spanish) return UiLanguage::Spanish;
    if (preference == LanguagePreference::English) return UiLanguage::English;
    LANGID systemLanguage = GetUserDefaultUILanguage();
    if (systemLanguage == 0) systemLanguage = GetUserDefaultLangID();
    const LANGID primary = PRIMARYLANGID(systemLanguage);
    if (primary == LANG_SPANISH) return UiLanguage::Spanish;
    if (primary == LANG_ENGLISH) return UiLanguage::English;
    // Spanish is the fallback when Windows reports an unsupported language.
    return UiLanguage::Spanish;
}

struct DeviceInfo {
    std::wstring name;
    std::wstring id;
};

struct VideoMode {
    UINT32 width = 0;
    UINT32 height = 0;
};

struct VideoModesUpdate {
    std::wstring deviceId;
    std::vector<VideoMode> modes;
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
    bool showPerformanceStats = false;
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

// Three-slot lock-free frame exchange. The capture worker writes into a FREE
// slot while the UI thread uploads the newest READY slot to the GPU. No frame
// mutex is held during scaling, drawing, or presentation.
enum class FrameSlotState : int { Free = 0, Writing = 1, Ready = 2, Reading = 3 };
struct FrameSlot {
    std::vector<BYTE> pixels;
    int width = 0;
    int height = 0;
    std::atomic_uint64_t sequence{ 0 };
    std::atomic<int> state{ static_cast<int>(FrameSlotState::Free) };
};
constexpr size_t kFrameSlotCount = 3;

// GDI compatibility renderer backbuffers. They are only used if Direct3D 11
// cannot initialize on the current PC or its swap-chain path fails. The
// captured frame is cached on the UI thread so the main window and PiP can
// both paint the same frame without consuming it twice.
struct GdiPaintBuffer {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    HBRUSH backgroundBrush = nullptr;
    int width = 0;
    int height = 0;
    std::vector<BYTE> dimPixels;
};
GdiPaintBuffer g_mainGdiBuffer;
GdiPaintBuffer g_pipGdiBuffer;
std::vector<BYTE> g_compatibilityFramePixels;
int g_compatibilityFrameWidth = 0;
int g_compatibilityFrameHeight = 0;
uint64_t g_compatibilityFrameSequence = 0;
uint64_t g_compatibilityLastPresentedSequence = 0;

HWND g_hwnd = nullptr;
HWND g_pipHwnd = nullptr;
HWND g_aboutHwnd = nullptr;
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

Settings g_settings;
std::vector<DeviceInfo> g_videoDevices;
std::vector<DeviceInfo> g_audioDevices;
std::vector<VideoMode> g_videoModes;
std::array<FrameSlot, kFrameSlotCount> g_frameSlots;
std::atomic_uint64_t g_frameSequence{ 0 };
std::atomic_uint64_t g_captureFramesInterval{ 0 };
std::atomic_uint64_t g_renderFramesInterval{ 0 };
std::atomic_uint64_t g_droppedFramesInterval{ 0 };
std::atomic_uint g_nominalVideoFps{ 60 };
int g_captureFps = 0;
int g_renderFps = 0;
int g_droppedFps = 0;
ULONGLONG g_statsLastTick = 0;
std::atomic_bool g_frameMessagePending{ false };
std::atomic_int g_volume{ 80 };
std::atomic_bool g_muted{ false };
std::atomic_bool g_videoHasFrame{ false };
std::atomic<ULONGLONG> g_lastVideoFrameTick{ 0 };
ULONGLONG g_videoStartTick = 0;
std::wstring g_videoStatus;
std::wstring g_audioStatus;
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
bool g_suppressToolbarRevealUntilLeave = false;
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

FrameSlot* AcquireFrameSlotForWrite() {
    for (auto& slot : g_frameSlots) {
        int expected = static_cast<int>(FrameSlotState::Free);
        if (slot.state.compare_exchange_strong(expected, static_cast<int>(FrameSlotState::Writing),
                std::memory_order_acq_rel)) return &slot;
    }
    // If the UI is briefly behind, replace the oldest queued frame rather
    // than blocking capture. This is preferable to displaying stale frames.
    FrameSlot* oldest = nullptr;
    for (auto& slot : g_frameSlots) {
        if (slot.state.load(std::memory_order_acquire) == static_cast<int>(FrameSlotState::Ready) &&
            (!oldest || slot.sequence.load(std::memory_order_relaxed) < oldest->sequence.load(std::memory_order_relaxed))) oldest = &slot;
    }
    if (oldest) {
        int expected = static_cast<int>(FrameSlotState::Ready);
        if (oldest->state.compare_exchange_strong(expected, static_cast<int>(FrameSlotState::Writing),
                std::memory_order_acq_rel)) {
            g_droppedFramesInterval.fetch_add(1, std::memory_order_relaxed);
            return oldest;
        }
    }
    g_droppedFramesInterval.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
}

FrameSlot* AcquireLatestFrameForRead() {
    for (int attempt = 0; attempt < 3; ++attempt) {
        FrameSlot* newest = nullptr;
        for (auto& slot : g_frameSlots) {
            if (slot.state.load(std::memory_order_acquire) == static_cast<int>(FrameSlotState::Ready) &&
                (!newest || slot.sequence.load(std::memory_order_relaxed) > newest->sequence.load(std::memory_order_relaxed))) newest = &slot;
        }
        if (!newest) return nullptr;
        int expected = static_cast<int>(FrameSlotState::Ready);
        if (!newest->state.compare_exchange_strong(expected, static_cast<int>(FrameSlotState::Reading),
                std::memory_order_acq_rel)) continue;
        // Discard older frames that accumulated while the UI was busy.
        for (auto& slot : g_frameSlots) {
            if (&slot == newest) continue;
            expected = static_cast<int>(FrameSlotState::Ready);
            if (slot.state.compare_exchange_strong(expected, static_cast<int>(FrameSlotState::Free),
                    std::memory_order_acq_rel))
                g_droppedFramesInterval.fetch_add(1, std::memory_order_relaxed);
        }
        return newest;
    }
    return nullptr;
}

void ReleaseFrameSlot(FrameSlot* slot) {
    if (slot) slot->state.store(static_cast<int>(FrameSlotState::Free), std::memory_order_release);
}

void ClearQueuedFrames() {
    for (auto& slot : g_frameSlots) {
        int expected = static_cast<int>(FrameSlotState::Ready);
        slot.state.compare_exchange_strong(expected, static_cast<int>(FrameSlotState::Free),
            std::memory_order_acq_rel);
    }
}

bool HasQueuedReadyFrame() {
    for (const auto& slot : g_frameSlots) {
        if (slot.state.load(std::memory_order_acquire) == static_cast<int>(FrameSlotState::Ready))
            return true;
    }
    return false;
}

// Coalesce capture notifications: at most one WM_APP_FRAME may be queued.
// The UI handler keeps this flag set while it renders, then checks the slots
// again and schedules one follow-up if newer frames arrived in the meantime.
// This prevents a slow Present/D2D call from flooding the Win32 message queue
// and starving input, menus, window management, and shutdown messages.
void QueueVideoFrameMessage(HWND notifyWindow) {
    if (!notifyWindow || !IsWindow(notifyWindow)) return;
    bool expected = false;
    if (!g_frameMessagePending.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel, std::memory_order_acquire)) return;
    if (!PostMessageW(notifyWindow, WM_APP_FRAME, 0, 0)) {
        // The target window may be closing or its thread queue may be full.
        // Always release the token so a later frame can retry notification.
        g_frameMessagePending.store(false, std::memory_order_release);
    }
}

int CalculateFramesPerSecond(uint64_t frameCount, ULONGLONG elapsedMs) {
    if (elapsedMs == 0) return 0;
    const uint64_t rate = (frameCount * 1000ULL + elapsedMs / 2ULL) / elapsedMs;
    const uint64_t nominal = std::clamp<uint64_t>(g_nominalVideoFps.load(std::memory_order_relaxed), 1ULL, 240ULL);
    // A capture stream cannot present more unique frames than its negotiated
    // frame rate. This also prevents timer stalls or mode transitions from
    // producing impossible spikes such as 300 or 9000 FPS.
    return static_cast<int>(std::min<uint64_t>(rate, nominal));
}

void ResetPerformanceStats() {
    g_captureFramesInterval.exchange(0, std::memory_order_relaxed);
    g_renderFramesInterval.exchange(0, std::memory_order_relaxed);
    g_droppedFramesInterval.exchange(0, std::memory_order_relaxed);
    g_captureFps = g_renderFps = g_droppedFps = 0;
    g_statsLastTick = GetTickCount64();
}

void LoadLanguagePreference() {
    const int saved = GetPrivateProfileIntW(L"General", L"Language", 0, g_settingsPath.c_str());
    g_languagePreference = (saved >= 0 && saved <= 2)
        ? static_cast<LanguagePreference>(saved) : LanguagePreference::Automatic;
    g_uiLanguage = ResolveUiLanguage(g_languagePreference);
}

void SaveLanguagePreference() {
    const std::wstring value = std::to_wstring(static_cast<int>(g_languagePreference));
    WritePrivateProfileStringW(L"General", L"Language", value.c_str(), g_settingsPath.c_str());
}

void RefreshDevices(bool initial = false);
void SetWindowMode(int mode);
void CreateAppMenu(HWND hwnd);
void TogglePipTopmost();
void UpdateOverlayVisibility(bool force = false);
void InvalidateVideoSurface(HWND target);
void CaptureNormalWindowPlacement();
void RequestMenuRebuild();
void ApplyLanguageChange(LanguagePreference preference);
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
    g_settings.showPerformanceStats = ReadIniInt(L"Diagnostics", L"ShowPerformanceStats", 0) != 0;
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
    WriteIniString(L"Diagnostics", L"ShowPerformanceStats", g_settings.showPerformanceStats ? L"1" : L"0");
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
    dialog.lpstrFilter = Tr(TextId::ImageFileFilter);
    dialog.lpstrFile = file;
    dialog.nMaxFile = static_cast<DWORD>(std::size(file));
    dialog.lpstrTitle = Tr(TextId::SelectProfileLogo);
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
    AppendMenuW(profilesMenu, MF_STRING, IDM_PROFILE_SAVE, Tr(TextId::SaveCurrentProfile));
    AppendMenuW(profilesMenu, MF_STRING, IDM_PROFILE_DEFAULT, Tr(TextId::UseMainSettings));
    AppendMenuW(profilesMenu, MF_SEPARATOR, 0, nullptr);
    HMENU loadMenu = CreatePopupMenu();
    HMENU shortcutMenu = CreatePopupMenu();
    HMENU deleteMenu = CreatePopupMenu();
    HMENU iconMenu = CreatePopupMenu();
    if (g_profileMenuEntries.empty()) {
        AppendMenuW(loadMenu, MF_STRING | MF_GRAYED, 0, Tr(TextId::NoSavedProfiles));
        AppendMenuW(shortcutMenu, MF_STRING | MF_GRAYED, 0, Tr(TextId::NoSavedProfiles));
        AppendMenuW(deleteMenu, MF_STRING | MF_GRAYED, 0, Tr(TextId::NoSavedProfiles));
        AppendMenuW(iconMenu, MF_STRING | MF_GRAYED, 0, Tr(TextId::NoSavedProfiles));
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
    AppendMenuW(profilesMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(loadMenu), Tr(TextId::LoadProfile));
    AppendMenuW(profilesMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(shortcutMenu), Tr(TextId::CreateDesktopShortcut));
    AppendMenuW(profilesMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(iconMenu), Tr(TextId::ChangeProfileIcon));
    AppendMenuW(profilesMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(deleteMenu), Tr(TextId::DeleteProfile));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(profilesMenu), Tr(TextId::Profiles));
}

void SaveCurrentAsProfile() {
    try {
        wchar_t file[MAX_PATH]{};
        wcscpy_s(file, std::size(file), g_uiLanguage == UiLanguage::English ? L"New profile.ini" : L"Nuevo perfil.ini");
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = g_hwnd;
        dialog.lpstrFilter = Tr(TextId::ProfileFileFilter);
        dialog.lpstrFile = file;
        dialog.nMaxFile = static_cast<DWORD>(std::size(file));
        dialog.lpstrInitialDir = g_profilesDirectory.c_str();
        dialog.lpstrTitle = Tr(TextId::SavePlayViewProfile);
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
            MessageBoxW(g_hwnd, Tr(TextId::ProfilesMustStayInAppData), L"PlayView", MB_OK | MB_ICONWARNING);
            return;
        }

        const std::wstring profileName = profilePath.stem().wstring();
        if (profileName.empty()) {
            MessageBoxW(g_hwnd, Tr(TextId::InvalidProfileName), L"PlayView", MB_OK | MB_ICONWARNING);
            return;
        }

        std::filesystem::path iconSource;
        if (MessageBoxW(g_hwnd,
            Tr(TextId::AskProfileLogo), Tr(TextId::ProfileLogoTitle), MB_YESNO | MB_ICONQUESTION) == IDYES) {
            if (!PickIconFile(iconSource)) iconSource.clear();
        }

        std::filesystem::path iconDestination;
        if (!iconSource.empty()) {
            iconDestination = g_profileIconsDirectory / (profilePath.stem().wstring() + L".ico");
            if (!SaveProfileIcon(iconSource, iconDestination)) {
                MessageBoxW(g_hwnd, Tr(TextId::IconCopyFailedDefault), L"PlayView", MB_OK | MB_ICONWARNING);
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
            Tr(TextId::ProfileSavedAskShortcut), Tr(TextId::ProfileCreated), MB_YESNO | MB_ICONQUESTION) == IDYES) {
            if (CreateProfileShortcut(profile))
                MessageBoxW(g_hwnd, Tr(TextId::ShortcutCreated), L"PlayView", MB_OK | MB_ICONINFORMATION);
            else
                MessageBoxW(g_hwnd, Tr(TextId::ShortcutCreationFailed), L"PlayView", MB_OK | MB_ICONWARNING);
        }
    } catch (const std::filesystem::filesystem_error& error) {
        (void)error;
        Log(L"Error de archivos al guardar perfil.");
        MessageBoxW(g_hwnd, Tr(TextId::ProfileSaveFileError), L"PlayView", MB_OK | MB_ICONERROR);
    } catch (const std::exception&) {
        Log(L"Error de ejecución al guardar un perfil.");
        MessageBoxW(g_hwnd, Tr(TextId::ProfileSaveRuntimeError), L"PlayView", MB_OK | MB_ICONERROR);
    } catch (...) {
        Log(L"Error inesperado al guardar un perfil.");
        MessageBoxW(g_hwnd, Tr(TextId::ProfileSaveUnexpectedError), L"PlayView", MB_OK | MB_ICONERROR);
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

void UpdateStatusText() {
    g_statusText = g_videoStatus;
    if (!g_audioStatus.empty()) g_statusText += L"    |    " + g_audioStatus;
    // Status and toolbar labels are painted into the same video back buffer.
    // This avoids the opaque gray STATIC-control strip around the toolbar.
    InvalidateVideoSurface(g_hwnd);
}

class VideoCapture {
public:
    VideoCapture() = default;
    ~VideoCapture() { Shutdown(); }

    bool IsRunning() const { return requestedActive_.load(std::memory_order_acquire); }

    // Start/Stop are intentionally non-blocking. A single controller thread
    // owns the Media Foundation source reader and serializes device changes.
    // The UI never joins a thread that may be blocked inside a capture driver.
    void Start(HWND notifyWindow, const std::wstring& deviceId, int width, int height) {
        if (deviceId.empty()) { Stop(); return; }
        {
            std::lock_guard<std::mutex> lock(controlMutex_);
            if (shutdownRequested_) return;
            pendingRequest_ = { notifyWindow, deviceId, width, height };
            hasPendingRequest_ = true;
            requestedActive_.store(true, std::memory_order_release);
            stopRequested_.store(true, std::memory_order_release);
            if (!controller_.joinable()) {
                try {
                    controller_ = std::thread([this]() { ControlLoop(); });
                } catch (...) {
                    hasPendingRequest_ = false;
                    requestedActive_.store(false, std::memory_order_release);
                    Log(L"No se pudo crear el hilo de control de video.");
                    return;
                }
            } else {
                CancelSynchronousIo(controller_.native_handle());
            }
        }
        controlCv_.notify_one();
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(controlMutex_);
            hasPendingRequest_ = false;
            requestedActive_.store(false, std::memory_order_release);
            stopRequested_.store(true, std::memory_order_release);
            if (controller_.joinable()) CancelSynchronousIo(controller_.native_handle());
        }
        controlCv_.notify_one();
    }

    // Called once after the message loop exits and before MFShutdown().
    // Unlike Stop(), this waits for the worker, but never blocks normal UI use.
    void ShutdownAndWait() { Shutdown(); }

private:
    struct CaptureRequest {
        HWND notifyWindow = nullptr;
        std::wstring deviceId;
        int width = 0;
        int height = 0;
    };

    void Shutdown() {
        {
            std::lock_guard<std::mutex> lock(controlMutex_);
            if (shutdownRequested_) return;
            shutdownRequested_ = true;
            hasPendingRequest_ = false;
            requestedActive_.store(false, std::memory_order_release);
            stopRequested_.store(true, std::memory_order_release);
            if (controller_.joinable()) CancelSynchronousIo(controller_.native_handle());
        }
        controlCv_.notify_one();
        if (controller_.joinable()) controller_.join();
    }

    void ControlLoop() {
        for (;;) {
            CaptureRequest request;
            {
                std::unique_lock<std::mutex> lock(controlMutex_);
                controlCv_.wait(lock, [this]() { return shutdownRequested_ || hasPendingRequest_; });
                if (shutdownRequested_) break;
                request = std::move(pendingRequest_);
                pendingRequest_ = {};
                hasPendingRequest_ = false;
                stopRequested_.store(false, std::memory_order_release);
                running_.store(true, std::memory_order_release);
            }

            Run(request.notifyWindow, request.deviceId, request.width, request.height);
            running_.store(false, std::memory_order_release);

            {
                std::lock_guard<std::mutex> lock(controlMutex_);
                if (!hasPendingRequest_) requestedActive_.store(false, std::memory_order_release);
            }
        }
        running_.store(false, std::memory_order_release);
        requestedActive_.store(false, std::memory_order_release);
    }

    void Run(HWND notifyWindow, const std::wstring& deviceId, int requestedWidth, int requestedHeight) {
        const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ComPtr<IMFMediaSource> source;
        ComPtr<IMFSourceReader> reader;
        HRESULT hr = CreateMediaSource(deviceId, true, &source);
        if (SUCCEEDED(hr)) {
            ComPtr<IMFAttributes> readerAttributes;
            hr = MFCreateAttributes(&readerAttributes, 2);
            if (SUCCEEDED(hr)) hr = readerAttributes->SetUINT32(MF_LOW_LATENCY, TRUE);
            // Keep the same Source Reader configuration that was proven with
            // UVC capture devices in PlayView 1.1.1. Do not force hardware
            // transforms: some capture-card drivers expose audio normally but
            // fail or stall their video stream when that flag is enabled.
            // Keep the advanced Source Reader processor enabled for real-time
            // format conversion. It supports the conversion/deinterlace path
            // and can use a registered hardware video processor when available.
            if (SUCCEEDED(hr)) hr = readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
            if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source.Get(), readerAttributes.Get(), &reader);
        }

        if (stopRequested_.load(std::memory_order_acquire)) {
            reader.Reset();
            if (source) source->Shutdown();
            if (SUCCEEDED(comHr)) CoUninitialize();
            return;
        }

        if (SUCCEEDED(hr)) {
            ComPtr<IMFMediaType> requestedType;
            hr = MFCreateMediaType(&requestedType);
            if (SUCCEEDED(hr)) hr = requestedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(hr)) hr = requestedType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            if (SUCCEEDED(hr) && requestedWidth > 0 && requestedHeight > 0)
                hr = MFSetAttributeSize(requestedType.Get(), MF_MT_FRAME_SIZE, requestedWidth, requestedHeight);
            if (SUCCEEDED(hr)) hr = requestedType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            if (SUCCEEDED(hr)) {
                const HRESULT requestedHr = reader->SetCurrentMediaType(
                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, requestedType.Get());
                if (FAILED(requestedHr) && requestedWidth > 0 && requestedHeight > 0) {
                    Log(L"Requested capture size could not be negotiated; retrying automatic size. HRESULT=" +
                        std::to_wstring(static_cast<unsigned long>(requestedHr)));
                    ComPtr<IMFMediaType> fallbackType;
                    HRESULT fallbackHr = MFCreateMediaType(&fallbackType);
                    if (SUCCEEDED(fallbackHr)) fallbackHr = fallbackType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                    if (SUCCEEDED(fallbackHr)) fallbackHr = fallbackType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
                    if (SUCCEEDED(fallbackHr)) fallbackHr = fallbackType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                    if (SUCCEEDED(fallbackHr)) fallbackHr = reader->SetCurrentMediaType(
                        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, fallbackType.Get());
                    hr = fallbackHr;
                } else {
                    hr = requestedHr;
                }
            }
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
            UINT32 fpsNumerator = 0, fpsDenominator = 0;
            if (SUCCEEDED(hr) && SUCCEEDED(MFGetAttributeRatio(currentType.Get(), MF_MT_FRAME_RATE,
                    &fpsNumerator, &fpsDenominator)) && fpsNumerator > 0 && fpsDenominator > 0) {
                const UINT fps = static_cast<UINT>((fpsNumerator + fpsDenominator / 2) / fpsDenominator);
                g_nominalVideoFps.store(std::clamp<UINT>(fps, 1U, 240U), std::memory_order_relaxed);
            }
        }

        if (FAILED(hr) || frameWidth == 0 || frameHeight == 0) {
            if (!stopRequested_.load()) {
                Log(L"No se pudo iniciar la captura de video. HRESULT=" + std::to_wstring(static_cast<unsigned long>(hr)));
                PostMessageW(notifyWindow, WM_APP_VIDEO_LOST, 0, 0);
            }
            reader.Reset();
            if (source) source->Shutdown();
            if (SUCCEEDED(comHr)) CoUninitialize();
            return;
        }

        bool signaledReady = false;
        // Keep the last Source Reader diagnostics outside the loop so they
        // remain available when ReadSample exits with an error.
        DWORD lastStreamIndex = 0;
        DWORD lastReadFlags = 0;
        unsigned int framesBeforeModeQuery = 0;
        bool modesPublished = false;
        while (!stopRequested_.load()) {
            DWORD streamIndex = 0, flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &streamIndex, &flags, &timestamp, &sample);
            lastStreamIndex = streamIndex;
            lastReadFlags = flags;
            if (stopRequested_.load(std::memory_order_acquire)) break;
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
                    UINT32 fpsNumerator = 0, fpsDenominator = 0;
                    if (SUCCEEDED(MFGetAttributeRatio(changedType.Get(), MF_MT_FRAME_RATE,
                            &fpsNumerator, &fpsDenominator)) && fpsNumerator > 0 && fpsDenominator > 0) {
                        const UINT fps = static_cast<UINT>((fpsNumerator + fpsDenominator / 2) / fpsDenominator);
                        g_nominalVideoFps.store(std::clamp<UINT>(fps, 1U, 240U), std::memory_order_relaxed);
                    }
                }
            }
            if (!sample) { Sleep(1); continue; }

            ComPtr<IMFMediaBuffer> buffer;
            // Use the contiguous sample buffer for compatibility with UVC
            // capture drivers. The direct GetBufferByIndex path introduced in
            // 1.2.0 caused some devices to stop yielding readable video frames.
            hr = sample->ConvertToContiguousBuffer(&buffer);
            if (FAILED(hr)) {
                Log(L"ConvertToContiguousBuffer failed; HRESULT=" +
                    std::to_wstring(static_cast<unsigned long>(hr)));
                continue;
            }

            FrameSlot* frameSlot = AcquireFrameSlotForWrite();
            if (!frameSlot) continue;
            const size_t rowBytes = static_cast<size_t>(frameWidth) * 4;
            const size_t frameBytes = rowBytes * frameHeight;
            bool copiedFrame = false;
            try {
                if (frameSlot->pixels.size() != frameBytes) frameSlot->pixels.resize(frameBytes);
            } catch (...) {
                ReleaseFrameSlot(frameSlot);
                g_droppedFramesInterval.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            // Prefer IMF2DBuffer::Lock2D. It supplies the actual first scanline
            // and signed pitch, avoiding assumptions about the driver's layout.
            ComPtr<IMF2DBuffer> buffer2D;
            if (SUCCEEDED(buffer.As(&buffer2D))) {
                BYTE* scanline0 = nullptr;
                LONG pitch = 0;
                if (SUCCEEDED(buffer2D->Lock2D(&scanline0, &pitch))) {
                    const size_t absPitch = static_cast<size_t>(pitch < 0 ? -static_cast<int64_t>(pitch) : pitch);
                    if (scanline0 && absPitch >= rowBytes && rowBytes <= MAXDWORD) {
                        copiedFrame = SUCCEEDED(MFCopyImage(frameSlot->pixels.data(),
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
                            std::memcpy(frameSlot->pixels.data() + static_cast<size_t>(row) * rowBytes,
                                data + static_cast<size_t>(sourceRow) * absStride, rowBytes);
                        }
                        copiedFrame = true;
                    }
                    buffer->Unlock();
                }
            }

            if (copiedFrame && stopRequested_.load(std::memory_order_acquire)) {
                ReleaseFrameSlot(frameSlot);
                break;
            }

            if (copiedFrame) {
                frameSlot->width = static_cast<int>(frameWidth);
                frameSlot->height = static_cast<int>(frameHeight);
                frameSlot->sequence.store(g_frameSequence.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                frameSlot->state.store(static_cast<int>(FrameSlotState::Ready), std::memory_order_release);
                g_captureFramesInterval.fetch_add(1, std::memory_order_relaxed);
                g_videoHasFrame.store(true);
                g_lastVideoFrameTick.store(GetTickCount64());
                if (!signaledReady) {
                    signaledReady = true;
                    PostMessageW(notifyWindow, WM_APP_VIDEO_READY, 0, 0);
                }
                QueueVideoFrameMessage(notifyWindow);
                if (!modesPublished && ++framesBeforeModeQuery >= 24 && !stopRequested_.load()) {
                    modesPublished = true;
                    auto update = std::make_unique<VideoModesUpdate>();
                    update->deviceId = deviceId;
                    for (DWORD index = 0; index < 32 && !stopRequested_.load(); ++index) {
                        ComPtr<IMFMediaType> nativeType;
                        const HRESULT modeHr = reader->GetNativeMediaType(
                            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), index, &nativeType);
                        if (modeHr == MF_E_NO_MORE_TYPES) break;
                        if (FAILED(modeHr)) continue;
                        UINT32 modeWidth = 0, modeHeight = 0;
                        if (SUCCEEDED(MFGetAttributeSize(nativeType.Get(), MF_MT_FRAME_SIZE, &modeWidth, &modeHeight)) &&
                            modeWidth > 0 && modeHeight > 0) {
                            const bool duplicate = std::any_of(update->modes.begin(), update->modes.end(),
                                [&](const VideoMode& mode) { return mode.width == modeWidth && mode.height == modeHeight; });
                            if (!duplicate) update->modes.push_back({ modeWidth, modeHeight });
                        }
                    }
                    std::sort(update->modes.begin(), update->modes.end(), [](const VideoMode& a, const VideoMode& b) {
                        const uint64_t areaA = static_cast<uint64_t>(a.width) * a.height;
                        const uint64_t areaB = static_cast<uint64_t>(b.width) * b.height;
                        if (areaA != areaB) return areaA > areaB;
                        return a.width > b.width;
                    });
                    if (!update->modes.empty() && !stopRequested_.load()) {
                        auto* rawUpdate = update.release();
                        if (!PostMessageW(notifyWindow, WM_APP_VIDEO_MODES, 0, reinterpret_cast<LPARAM>(rawUpdate)))
                            delete rawUpdate;
                    }
                }
            } else {
                ReleaseFrameSlot(frameSlot);
            }
        }

        reader.Reset();
        if (source) source->Shutdown();
        if (!stopRequested_.load()) {
            std::wostringstream reason;
            reason << L"La captura de video se interrumpio o perdio el dispositivo."
                << L" HRESULT=0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr)
                << L" flags=0x" << static_cast<unsigned long>(lastReadFlags)
                << L" stream=" << std::dec << lastStreamIndex;
            Log(reason.str());
            PostMessageW(notifyWindow, WM_APP_VIDEO_LOST, 0, 0);
        }
        if (SUCCEEDED(comHr)) CoUninitialize();
    }

    std::thread controller_;
    std::mutex controlMutex_;
    std::condition_variable controlCv_;
    CaptureRequest pendingRequest_{};
    bool hasPendingRequest_ = false;
    bool shutdownRequested_ = false;
    std::atomic_bool stopRequested_{ true };
    std::atomic_bool requestedActive_{ false };
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

        if (stopRequested_.load(std::memory_order_acquire)) {
            reader.Reset();
            if (source) source->Shutdown();
            if (SUCCEEDED(comHr)) CoUninitialize();
            return;
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
    SetWindowTextW(g_hwnd, (std::wstring(Tr(TextId::WindowTitleProfile)) + g_activeProfileName).c_str());
    SendMessageW(g_volumeSlider, TBM_SETPOS, TRUE, g_settings.volume);
    
    SetWindowTextW(g_muteButton, g_settings.muted ? Tr(TextId::UnmuteButton) : Tr(TextId::MuteButton));
    ApplyLoadedWindowPreferences();
    RefreshDevices(true);
    Log(L"Perfil cargado: " + profile.name);
}

void DeleteProfile(const ProfileEntry& profile) {
    if (profile.path == g_activeProfilePath) {
        MessageBoxW(g_hwnd, Tr(TextId::CannotDeleteActiveProfile), L"PlayView", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::wstring question = std::wstring(Tr(TextId::DeleteProfileQuestion)) + profile.name + (g_uiLanguage == UiLanguage::English ? L"'? Existing shortcuts will no longer work." : L"'? Los accesos directos existentes dejarán de funcionar.");
    if (MessageBoxW(g_hwnd, question.c_str(), Tr(TextId::DeleteProfileTitle), MB_YESNO | MB_ICONWARNING) != IDYES) return;
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
        MessageBoxW(g_hwnd, Tr(TextId::IconCopyFailed), L"PlayView", MB_OK | MB_ICONERROR);
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
    if (!g_volumeTooltip) {
        g_volumeTooltipVisible = false;
        return;
    }
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
    tool.hwnd = g_hwnd;
    tool.uId = reinterpret_cast<UINT_PTR>(g_volumeSlider);
    tool.lpszText = const_cast<LPWSTR>(g_volumeTooltipText.c_str());
    // TTM_TRACKACTIVATE(FALSE) normally closes a tracking tooltip. TTM_POP
    // and SW_HIDE are also applied because hiding the parent toolbar does not
    // necessarily generate WM_MOUSELEAVE for its child trackbar.
    SendMessageW(g_volumeTooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    SendMessageW(g_volumeTooltip, TTM_POP, 0, 0);
    ShowWindow(g_volumeTooltip, SW_HIDE);
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
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
    case WM_KILLFOCUS:
        HideVolumeTooltip();
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, VolumeSliderSubclass, subclassId);
        break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK ToolbarHoverSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR subclassId, DWORD_PTR referenceData) {
    (void)wParam;
    (void)lParam;
    (void)referenceData;
    switch (message) {
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tracking);
        // Child controls cover the parent window's client area. Re-evaluate
        // the toolbar immediately while the pointer moves over those controls.
        UpdateOverlayVisibility(false);
        break;
    }
    case WM_MOUSELEAVE:
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        UpdateOverlayVisibility(false);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, ToolbarHoverSubclass, subclassId);
        break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

HWND AddControl(DWORD exStyle, const wchar_t* className, const wchar_t* text, DWORD style,
                int x, int y, int width, int height, int id) {
    HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, g_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SetControlFont(control);
    if (control) SetWindowSubclass(control, ToolbarHoverSubclass, static_cast<UINT_PTR>(id), 0);
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
        const std::wstring missing = std::wstring(Tr(TextId::NotFoundPrefix)) + (savedName.empty() ? selectedId : savedName);
        selectedIndex = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(missing.c_str())));
        SendMessageW(combo, CB_SETITEMDATA, selectedIndex, static_cast<LPARAM>(-1));
    }
    if (selectedIndex >= 0) SendMessageW(combo, CB_SETCURSEL, selectedIndex, 0);
    else SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void PopulateResolutionCombo() {
    SendMessageW(g_resolutionCombo, CB_RESETCONTENT, 0, 0);
    const LRESULT automatic = SendMessageW(g_resolutionCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(TextId::AutomaticResolution)));
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
    PopulateDeviceCombo(g_videoCombo, g_videoDevices, g_settings.videoId, g_settings.videoName, Tr(TextId::SelectCaptureDevice));
    PopulateDeviceCombo(g_audioCombo, g_audioDevices, g_settings.audioId, g_settings.audioName, Tr(TextId::SelectCaptureAudio));

    const bool videoPresent = !g_settings.videoId.empty() && ContainsDevice(g_videoDevices, g_settings.videoId);
    const bool audioPresent = !g_settings.audioId.empty() && ContainsDevice(g_audioDevices, g_settings.audioId);
    bool startVideoNow = false;
    bool startAudioNow = false;

    if (!g_settings.videoId.empty()) {
        if (videoPresent) {
            if (initial) {
                g_videoStatus = Tr(TextId::VideoFoundStarting);
                startVideoNow = true;
            }
        } else {
            g_videoStatus = std::wstring(Tr(TextId::VideoMissingPrefix)) + (g_settings.videoName.empty() ? Tr(TextId::UnknownDevice) : g_settings.videoName);
            g_nextVideoRetry = GetTickCount64() + 2500;
        }
    } else {
        g_videoStatus = g_videoDevices.empty() ? Tr(TextId::NoCaptureDevices) : Tr(TextId::VideoSelect);
    }

    if (!g_settings.audioId.empty()) {
        if (audioPresent) {
            if (initial) {
                g_audioStatus = Tr(TextId::AudioConnected);
                startAudioNow = true;
            }
        } else {
            g_audioStatus = Tr(TextId::AudioMissing);
            g_nextAudioRetry = GetTickCount64() + 2500;
        }
    } else {
        g_audioStatus = Tr(TextId::AudioUnconfigured);
    }

    // Resolution modes are discovered by the selected device's own Source
    // Reader on its capture worker. This avoids opening the camera twice and
    // keeps device setup out of the UI thread.
    g_videoModes.clear();
    PopulateResolutionCombo();

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
    g_nominalVideoFps.store(60, std::memory_order_relaxed);
    g_lastVideoFrameTick.store(0, std::memory_order_relaxed);
    ResetPerformanceStats();
    // Do not clear g_frameMessagePending here. If a frame notification is
    // already queued, that handler will consume any frame from the new capture
    // session too. Clearing it here could enqueue duplicate render messages.
    ClearQueuedFrames();
    PostMessageW(g_hwnd, WM_APP_CLEAR_FRAME, 0, 0);
    if (g_settings.videoId.empty()) {
        g_videoStatus = Tr(TextId::VideoSelect);
    } else if (!ContainsDevice(g_videoDevices, g_settings.videoId)) {
        g_videoStatus = std::wstring(Tr(TextId::VideoMissingPrefix)) + (g_settings.videoName.empty() ? Tr(TextId::UnknownDevice) : g_settings.videoName);
        if (showErrorIfMissing) MessageBoxW(g_hwnd, g_videoStatus.c_str(), L"PlayView", MB_OK | MB_ICONWARNING);
        g_nextVideoRetry = GetTickCount64() + 3000;
    } else {
        g_videoStatus = std::wstring(Tr(TextId::VideoConnecting)) + g_settings.videoName + L"...";
        g_videoCapture.Start(g_hwnd, g_settings.videoId, g_settings.width, g_settings.height);
    }
    UpdateStatusText();
    InvalidateVideoSurface(g_hwnd);
}

void StartSelectedAudio(bool showErrorIfMissing = false) {
    g_audioCapture.Stop();
    if (g_settings.audioId.empty()) {
        g_audioStatus = Tr(TextId::AudioUnconfigured);
    } else if (!ContainsDevice(g_audioDevices, g_settings.audioId)) {
        g_audioStatus = Tr(TextId::AudioMissing);
        if (showErrorIfMissing) MessageBoxW(g_hwnd, g_audioStatus.c_str(), L"PlayView", MB_OK | MB_ICONWARNING);
        g_nextAudioRetry = GetTickCount64() + 3000;
    } else {
        g_audioStatus = Tr(TextId::AudioConnected);
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
    g_videoModes.clear();
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
    if (g_windowMode == 2) g_suppressToolbarRevealUntilLeave = false;
    UpdateOverlayVisibility(true);
    ResetPerformanceStats();
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
    HMENU settings = CreatePopupMenu();
    HMENU language = CreatePopupMenu();
    HMENU help = CreatePopupMenu();

    AppendMenuW(file, MF_STRING | (g_settings.showToolbar ? MF_CHECKED : MF_UNCHECKED),
        IDM_SHOW_TOOLBAR, Tr(TextId::ShowToolbar));
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, Tr(TextId::Exit));

    AppendMenuW(view, MF_STRING, IDM_VIEW_WINDOW, Tr(TextId::Windowed));
    AppendMenuW(view, MF_STRING, IDM_VIEW_FULLSCREEN, Tr(TextId::Fullscreen));
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, IDM_VIEW_PIP, Tr(TextId::PipMenu));
    AppendMenuW(view, MF_STRING | (g_pipAlwaysOnTop ? MF_CHECKED : MF_UNCHECKED),
        IDM_PIP_TOPMOST, Tr(TextId::PipAlwaysOnTop));
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING | (g_settings.showPerformanceStats ? MF_CHECKED : MF_UNCHECKED),
        IDM_SHOW_PERFORMANCE_STATS, Tr(TextId::PerformanceStatsMenu));

    AppendMenuW(language, MF_STRING | (g_languagePreference == LanguagePreference::Automatic ? MF_CHECKED : MF_UNCHECKED),
        IDM_LANGUAGE_AUTOMATIC, Tr(TextId::LanguageAutomatic));
    AppendMenuW(language, MF_STRING | (g_languagePreference == LanguagePreference::Spanish ? MF_CHECKED : MF_UNCHECKED),
        IDM_LANGUAGE_SPANISH, Tr(TextId::LanguageSpanish));
    AppendMenuW(language, MF_STRING | (g_languagePreference == LanguagePreference::English ? MF_CHECKED : MF_UNCHECKED),
        IDM_LANGUAGE_ENGLISH, Tr(TextId::LanguageEnglish));

    AppendMenuW(settings, MF_POPUP, reinterpret_cast<UINT_PTR>(language), Tr(TextId::LanguageMenu));
    AppendMenuW(help, MF_STRING, IDM_ABOUT, Tr(TextId::About));

    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), Tr(TextId::FileMenu));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), Tr(TextId::ViewMenu));
    RebuildProfilesMenu(menu);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), Tr(TextId::SettingsMenu));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), Tr(TextId::HelpMenu));

    HMENU oldMenu = g_appMenu;
    g_appMenu = menu;
    SetMenu(hwnd, g_windowMode == 0 ? g_appMenu : nullptr);
    DrawMenuBar(hwnd);
    if (oldMenu) DestroyMenu(oldMenu);
}

void RequestMenuRebuild() {
    if (g_hwnd && IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_REBUILD_MENU, 0, 0);
}

void RefreshLocalizedRuntimeStatus() {
    if (g_settings.videoId.empty()) {
        g_videoStatus = g_videoDevices.empty() ? Tr(TextId::NoCaptureDevices) : Tr(TextId::VideoSelect);
    } else if (!ContainsDevice(g_videoDevices, g_settings.videoId)) {
        g_videoStatus = std::wstring(Tr(TextId::VideoMissingPrefix)) +
            (g_settings.videoName.empty() ? Tr(TextId::UnknownDevice) : g_settings.videoName);
    } else if (g_videoCapture.IsRunning()) {
        g_videoStatus = g_videoHasFrame.load() ? Tr(TextId::VideoActive) : Tr(TextId::VideoFoundStarting);
    } else {
        g_videoStatus = Tr(TextId::VideoSelect);
    }

    if (g_settings.audioId.empty()) g_audioStatus = Tr(TextId::AudioUnconfigured);
    else if (ContainsDevice(g_audioDevices, g_settings.audioId) && g_audioCapture.IsRunning())
        g_audioStatus = Tr(TextId::AudioConnected);
    else g_audioStatus = Tr(TextId::AudioMissing);
    UpdateStatusText();
}

void ApplyLanguageChange(LanguagePreference preference) {
    g_languagePreference = preference;
    g_uiLanguage = ResolveUiLanguage(g_languagePreference);
    SaveLanguagePreference();
    if (!g_hwnd || !IsWindow(g_hwnd)) return;

    PopulateDeviceCombo(g_videoCombo, g_videoDevices, g_settings.videoId, g_settings.videoName, Tr(TextId::SelectCaptureDevice));
    PopulateDeviceCombo(g_audioCombo, g_audioDevices, g_settings.audioId, g_settings.audioName, Tr(TextId::SelectCaptureAudio));
    PopulateResolutionCombo();
    if (HWND reload = GetDlgItem(g_hwnd, IDC_RELOAD)) SetWindowTextW(reload, Tr(TextId::ReloadButton));
    if (HWND pip = GetDlgItem(g_hwnd, IDC_PIP)) SetWindowTextW(pip, Tr(TextId::PipButton));
    if (HWND sync = GetDlgItem(g_hwnd, IDC_SYNC)) SetWindowTextW(sync, Tr(TextId::ResyncButton));
    if (g_muteButton) SetWindowTextW(g_muteButton, g_settings.muted ? Tr(TextId::UnmuteButton) : Tr(TextId::MuteButton));
    if (!g_activeProfileName.empty())
        SetWindowTextW(g_hwnd, (std::wstring(Tr(TextId::WindowTitleProfile)) + g_activeProfileName).c_str());
    else SetWindowTextW(g_hwnd, L"PlayView");
    RefreshLocalizedRuntimeStatus();
    CreateAppMenu(g_hwnd);
    UpdateOverlayVisibility(true);
    InvalidateVideoSurface(g_hwnd);
    if (g_pipHwnd && IsWindow(g_pipHwnd)) InvalidateVideoSurface(g_pipHwnd);
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

class VideoRenderer {
    struct WindowTarget {
        HWND hwnd = nullptr;
        ComPtr<IDXGISwapChain3> swapChain;
        // Keep views for both flip-model back buffers. Recreating COM views on
        // every paint adds avoidable CPU overhead and can stall the UI thread.
        // Three flip-model back buffers reduce stalls while DWM is composing
        // windowed presentation and keep a fresh frame available for each vblank.
        std::array<ComPtr<ID3D11RenderTargetView>, 3> rtvs;
        std::array<ComPtr<ID2D1Bitmap1>, 3> d2dTargets;
        UINT width = 0, height = 0;
    };
public:
    bool Render(HWND hwnd, bool compact) {
        if (disabled_) return false;
        if (!EnsureDevice()) { DisableAfterFailure(); return false; }
        WindowTarget& t = compact ? pipTarget_ : mainTarget_;
        if (!EnsureTarget(t, hwnd)) { DisableAfterFailure(); return false; }
        // Do not consume a CPU frame until both the D3D device and the target
        // window are ready. If either setup step fails, GDI can still present
        // the queued frame instead of showing a blank/error screen.
        if (!UploadLatestFrame()) { DisableAfterFailure(); return false; }

        const UINT bufferIndex = t.swapChain->GetCurrentBackBufferIndex();
        if (bufferIndex >= static_cast<UINT>(t.rtvs.size()) ||
            !t.rtvs[bufferIndex] || !t.d2dTargets[bufferIndex]) {
            SetFailure(L"DXGI current back-buffer/view lookup", E_FAIL);
            DisableAfterFailure();
            return false;
        }

        const float clear[4] = { 0.027f, 0.039f, 0.063f, 1.0f };
        ID3D11RenderTargetView* rtv = t.rtvs[bufferIndex].Get();
        context_->OMSetRenderTargets(1, &rtv, nullptr);
        context_->ClearRenderTargetView(rtv, clear);

        if (frameView_ && frameWidth_ > 0 && frameHeight_ > 0) {
            RECT rc{}; GetClientRect(hwnd, &rc);
            const float cw = static_cast<float>(std::max<LONG>(1, rc.right));
            const float ch = static_cast<float>(std::max<LONG>(1, rc.bottom));
            const float srcAspect = static_cast<float>(frameWidth_) / static_cast<float>(frameHeight_);
            const float dstAspect = cw / ch;
            D3D11_VIEWPORT vp{};
            if (dstAspect > srcAspect) {
                vp.Height = ch; vp.Width = ch * srcAspect; vp.TopLeftX = (cw - vp.Width) * 0.5f;
            } else {
                vp.Width = cw; vp.Height = cw / srcAspect; vp.TopLeftY = (ch - vp.Height) * 0.5f;
            }
            vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
            context_->RSSetViewports(1, &vp);
            context_->RSSetState(rasterizer_.Get());
            context_->IASetInputLayout(nullptr);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->VSSetShader(vs_.Get(), nullptr, 0);
            context_->PSSetShader(ps_.Get(), nullptr, 0);
            ID3D11ShaderResourceView* srv = frameView_.Get();
            context_->PSSetShaderResources(0, 1, &srv);
            ID3D11SamplerState* sampler = sampler_.Get();
            context_->PSSetSamplers(0, 1, &sampler);
            context_->Draw(3, 0);
            ID3D11ShaderResourceView* nullSrv = nullptr;
            context_->PSSetShaderResources(0, 1, &nullSrv);
        }

        // Direct2D draws the controls and text onto the same DXGI back buffer.
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        d2d_->SetTarget(t.d2dTargets[bufferIndex].Get());
        d2d_->BeginDraw();
        d2d_->SetTransform(D2D1::Matrix3x2F::Identity());
        if (!frameView_) DrawNoFrame(hwnd);
        if (!compact && g_overlayVisible) DrawToolbar(hwnd);
        if (!compact && g_settings.showPerformanceStats) DrawStats(hwnd);
        if (compact && g_pipHover) DrawPipOverlay(hwnd);
        const HRESULT drawHr = d2d_->EndDraw();
        d2d_->SetTarget(nullptr);
        if (FAILED(drawHr)) {
            SetFailure(L"Direct2D EndDraw", drawHr);
            DisableAfterFailure();
            return false;
        }

        // Never let a busy DWM/DXGI presentation queue block the UI thread.
        // DXGI_PRESENT_DO_NOT_WAIT returns DXGI_ERROR_WAS_STILL_DRAWING when
        // the compositor has not released a back buffer yet. We skip that
        // frame and let the coalesced notification present the newest one.
        const HRESULT presentHr = t.swapChain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
        if (presentHr == DXGI_ERROR_WAS_STILL_DRAWING) {
            // Keep the uploaded frame available for a later paint/retry. Count
            // it as dropped only if a newer frame supersedes it (see upload).
            return true;
        }
        if (SUCCEEDED(presentHr) && lastUploadedSequence_ > lastCountedSequence_) {
            g_renderFramesInterval.fetch_add(1, std::memory_order_relaxed);
            lastCountedSequence_ = lastUploadedSequence_;
        }
        if (FAILED(presentHr)) {
            HRESULT removedReason = device_ ? device_->GetDeviceRemovedReason() : E_FAIL;
            SetFailure(L"DXGI swap-chain Present", presentHr);
            if (removedReason != S_OK) {
                Log(L"D3D11 device removed reason HRESULT=" + std::to_wstring(static_cast<unsigned long>(removedReason)));
            }
            DisableAfterFailure();
            ResetDevice();
            return false;
        }
        return true;
    }
    void ClearFrame() {
        frameView_.Reset();
        frameTexture_.Reset();
        frameWidth_ = frameHeight_ = 0;
        frameHasMipmaps_ = false;
        frameMipmapsRequested_ = false;
        lastUploadedSequence_ = 0;
        lastCountedSequence_ = 0;
    }
    void DestroyTarget(HWND hwnd) {
        if (mainTarget_.hwnd == hwnd) ResetTarget(mainTarget_);
        if (pipTarget_.hwnd == hwnd) ResetTarget(pipTarget_);
    }
    void Destroy() {
        ResetTarget(mainTarget_); ResetTarget(pipTarget_); ClearFrame();
        d2d_.Reset(); d2dDevice_.Reset(); d2dFactory_.Reset(); dwrite_.Reset();
        white_.Reset(); shadow_.Reset(); dim_.Reset(); button_.Reset(); cyan_.Reset(); red_.Reset();
        textFormat_.Reset(); statusFormat_.Reset(); sampler_.Reset(); ps_.Reset(); vs_.Reset();
        context_.Reset(); device_.Reset(); factory_.Reset(); initialized_ = false;
        disabled_ = false; failureLogged_ = false; failureStage_.clear(); failureHr_ = S_OK;
    }
private:
    void SetFailure(const wchar_t* stage, HRESULT hr) {
        failureStage_ = stage ? stage : L"unknown renderer stage";
        failureHr_ = hr;
    }
    void DisableAfterFailure() {
        disabled_ = true;
        if (failureLogged_) return;
        failureLogged_ = true;
        std::wostringstream message;
        message << L"Direct3D 11 renderer disabled at " << failureStage_
            << L"; HRESULT=0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(failureHr_);
        Log(message.str());
    }
    bool EnsureDevice() {
        if (initialized_) return true;
        // Keep the runtime's normal thread-safety support enabled. This is
        // slightly more conservative for hybrid-GPU and D2D/DXGI drivers than
        // D3D11_CREATE_DEVICE_SINGLETHREADED and avoids driver-specific issues.
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL obtained{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device_, &obtained, &context_);
        if (hr == E_INVALIDARG) {
            device_.Reset(); context_.Reset();
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                levels + 1, static_cast<UINT>(std::size(levels) - 1), D3D11_SDK_VERSION, &device_, &obtained, &context_);
        }
        if (FAILED(hr)) {
            device_.Reset(); context_.Reset();
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                levels + 1, static_cast<UINT>(std::size(levels) - 1), D3D11_SDK_VERSION, &device_, &obtained, &context_);
        }
        if (FAILED(hr)) { SetFailure(L"D3D11CreateDevice (hardware and WARP)", hr); return false; }
        ComPtr<IDXGIDevice> dxgi;
        hr = device_.As(&dxgi);
        if (FAILED(hr)) { SetFailure(L"QueryInterface IDXGIDevice", hr); return false; }
        // Keep DXGI's default frame-latency queue. Forcing a one-frame device
        // queue without a waitable swap-chain object can stall a composed
        // window while DWM is releasing its previous back buffer, which may
        // halve the effective presentation cadence on some drivers.
        ComPtr<IDXGIAdapter> adapter;
        hr = dxgi->GetAdapter(&adapter);
        if (FAILED(hr)) { SetFailure(L"IDXGIDevice::GetAdapter", hr); return false; }
        hr = adapter->GetParent(IID_PPV_ARGS(&factory_));
        if (FAILED(hr)) { SetFailure(L"IDXGIAdapter::GetParent IDXGIFactory2", hr); return false; }

        const char* shader =
            "struct O{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};"
            "O VSMain(uint id:SV_VertexID){float2 p=id==0?float2(-1,1):(id==1?float2(-1,-3):float2(3,1));"
            "O o;o.p=float4(p,0,1);o.uv=float2((p.x+1)*.5,(1-p.y)*.5);return o;}"
            "Texture2D tex:register(t0);SamplerState smp:register(s0);"
            "float4 PSMain(O i):SV_TARGET{return tex.Sample(smp,i.uv);}";
        ComPtr<ID3DBlob> vsBlob, psBlob, errors;
        hr = D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "VSMain", "vs_4_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errors);
        if (SUCCEEDED(hr)) hr = D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr,
            "PSMain", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errors);
        if (SUCCEEDED(hr)) hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
        if (SUCCEEDED(hr)) hr = device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_);
        if (FAILED(hr)) { SetFailure(L"D3DCompile/CreateShader", hr); return false; }
        D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr = device_->CreateSamplerState(&sd, &sampler_);
        if (FAILED(hr)) { SetFailure(L"ID3D11Device::CreateSamplerState", hr); return false; }
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        hr = device_->CreateRasterizerState(&rd, &rasterizer_);
        if (FAILED(hr)) { SetFailure(L"ID3D11Device::CreateRasterizerState", hr); return false; }
        D2D1_FACTORY_OPTIONS options{};
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
            &options, reinterpret_cast<void**>(d2dFactory_.GetAddressOf()));
        if (SUCCEEDED(hr)) hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
        if (SUCCEEDED(hr)) hr = d2dFactory_->CreateDevice(dxgi.Get(), &d2dDevice_);
        if (SUCCEEDED(hr)) hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_);
        if (SUCCEEDED(hr)) hr = dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"", &textFormat_);
        if (SUCCEEDED(hr)) hr = dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 17.0f, L"", &statusFormat_);
        if (SUCCEEDED(hr)) hr = statusFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        if (SUCCEEDED(hr)) hr = statusFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (SUCCEEDED(hr)) hr = d2d_->CreateSolidColorBrush(D2D1::ColorF(0.96f,0.97f,0.99f,1), &white_);
        if (SUCCEEDED(hr)) hr = d2d_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0.92f), &shadow_);
        if (SUCCEEDED(hr)) hr = d2d_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0.56f), &dim_);
        if (SUCCEEDED(hr)) hr = d2d_->CreateSolidColorBrush(D2D1::ColorF(0.12f,0.16f,0.22f,0.92f), &button_);
        if (SUCCEEDED(hr)) hr = d2d_->CreateSolidColorBrush(D2D1::ColorF(0.32f,0.78f,0.95f,1), &cyan_);
        if (SUCCEEDED(hr)) hr = d2d_->CreateSolidColorBrush(D2D1::ColorF(0.96f,0.36f,0.36f,1), &red_);
        if (FAILED(hr)) { SetFailure(L"Direct2D/DirectWrite device and overlay initialization", hr); return false; }
        initialized_ = true; return true;
    }
    bool EnsureTarget(WindowTarget& t, HWND hwnd) {
        RECT rc{}; GetClientRect(hwnd, &rc);
        const UINT w = static_cast<UINT>(std::max<LONG>(1, rc.right));
        const UINT h = static_cast<UINT>(std::max<LONG>(1, rc.bottom));
        if (!t.swapChain) {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = w;
            desc.Height = h;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = static_cast<UINT>(t.rtvs.size()); // 3 buffers for composed-window pacing
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            ComPtr<IDXGISwapChain1> swapChain1;
            HRESULT hr = factory_->CreateSwapChainForHwnd(device_.Get(), hwnd, &desc, nullptr, nullptr, &swapChain1);
            if (FAILED(hr)) {
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                hr = factory_->CreateSwapChainForHwnd(device_.Get(), hwnd, &desc, nullptr, nullptr, &swapChain1);
            }
            if (FAILED(hr)) { SetFailure(L"IDXGIFactory2::CreateSwapChainForHwnd", hr); return false; }
            hr = swapChain1.As(&t.swapChain);
            if (FAILED(hr)) { SetFailure(L"QueryInterface IDXGISwapChain3", hr); return false; }
            factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
            t.hwnd = hwnd;
            t.width = w;
            t.height = h;
        } else if (t.width != w || t.height != h) {
            // All references to swap-chain buffers must be released before
            // ResizeBuffers. Recreate the views only when the client size changes.
            d2d_->SetTarget(nullptr);
            for (auto& target : t.d2dTargets) target.Reset();
            for (auto& view : t.rtvs) view.Reset();
            HRESULT hr = t.swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(hr)) { SetFailure(L"IDXGISwapChain::ResizeBuffers", hr); return false; }
            t.width = w;
            t.height = h;
        }

        for (UINT index = 0; index < static_cast<UINT>(t.rtvs.size()); ++index) {
            if (t.rtvs[index] && t.d2dTargets[index]) continue;
            ComPtr<ID3D11Texture2D> backBuffer;
            HRESULT hr = t.swapChain->GetBuffer(index, IID_PPV_ARGS(&backBuffer));
            if (SUCCEEDED(hr))
                hr = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &t.rtvs[index]);
            ComPtr<IDXGISurface> surface;
            if (SUCCEEDED(hr)) hr = backBuffer.As(&surface);
            if (SUCCEEDED(hr)) {
                const auto props = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                    96.0f, 96.0f);
                hr = d2d_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &t.d2dTargets[index]);
            }
            if (FAILED(hr)) {
                t.d2dTargets[index].Reset();
                t.rtvs[index].Reset();
                SetFailure(L"Create swap-chain back-buffer views/Direct2D target", hr);
                return false;
            }
        }
        return true;
    }
    bool ShouldUseMipmaps(int sourceWidth, int sourceHeight) const {
        auto isDownscaled = [sourceWidth, sourceHeight](HWND window) {
            if (!window || !IsWindowVisible(window)) return false;
            RECT rc{};
            if (!GetClientRect(window, &rc)) return false;
            const int width = std::max<LONG>(1, rc.right - rc.left);
            const int height = std::max<LONG>(1, rc.bottom - rc.top);
            // Mipmaps improve substantial reductions. At native or near-native
            // size, avoid their per-frame generation cost altogether.
            return width < static_cast<int>(sourceWidth * 0.90) ||
                height < static_cast<int>(sourceHeight * 0.90);
        };
        return isDownscaled(g_hwnd) || isDownscaled(g_pipHwnd);
    }

    bool UploadLatestFrame() {
        FrameSlot* slot = AcquireLatestFrameForRead();
        if (!slot) return true;
        const int w = slot->width;
        const int h = slot->height;
        const uint64_t incomingSequence = slot->sequence.load(std::memory_order_relaxed);
        // If a previous uploaded frame could not be presented because DXGI was
        // busy, it becomes genuinely dropped only when a newer unique frame
        // replaces it. This avoids inflating DROPPED when a repaint later
        // succeeds with the same frame.
        if (lastUploadedSequence_ > lastCountedSequence_ && incomingSequence > lastUploadedSequence_)
            g_droppedFramesInterval.fetch_add(1, std::memory_order_relaxed);
        if (w <= 0 || h <= 0 || slot->pixels.empty()) {
            ReleaseFrameSlot(slot);
            return true;
        }

        const bool wantMipmaps = ShouldUseMipmaps(w, h);
        if (!frameTexture_ || frameWidth_ != w || frameHeight_ != h ||
            frameMipmapsRequested_ != wantMipmaps) {
            frameView_.Reset();
            frameTexture_.Reset();
            frameHasMipmaps_ = false;
            frameMipmapsRequested_ = wantMipmaps;

            UINT formatSupport = 0;
            const bool canGenerateMipmaps = wantMipmaps &&
                SUCCEEDED(device_->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &formatSupport)) &&
                (formatSupport & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) != 0;

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(w);
            desc.Height = static_cast<UINT>(h);
            desc.MipLevels = canGenerateMipmaps ? 0u : 1u;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = 0;
            if (canGenerateMipmaps) {
                desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
                desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
            }

            ComPtr<ID3D11Texture2D> newTexture;
            ComPtr<ID3D11ShaderResourceView> newView;
            HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &newTexture);
            if (SUCCEEDED(hr))
                hr = device_->CreateShaderResourceView(newTexture.Get(), nullptr, &newView);

            // Some drivers advertise autogen support but reject this particular
            // resource configuration. Fall back to a single-level linear texture.
            if (FAILED(hr) && canGenerateMipmaps) {
                newView.Reset();
                newTexture.Reset();
                desc.MipLevels = 1;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                desc.MiscFlags = 0;
                hr = device_->CreateTexture2D(&desc, nullptr, &newTexture);
                if (SUCCEEDED(hr))
                    hr = device_->CreateShaderResourceView(newTexture.Get(), nullptr, &newView);
            }

            if (FAILED(hr)) {
                SetFailure(L"CreateTexture2D/CreateShaderResourceView", hr);
                Log(L"Could not create the video texture; HRESULT=" + std::to_wstring(static_cast<unsigned long>(hr)));
                slot->state.store(static_cast<int>(FrameSlotState::Ready), std::memory_order_release);
                return false;
            }
            frameTexture_ = std::move(newTexture);
            frameView_ = std::move(newView);
            frameWidth_ = w;
            frameHeight_ = h;
            frameHasMipmaps_ = canGenerateMipmaps && desc.MipLevels == 0;
        }

        context_->UpdateSubresource(frameTexture_.Get(), 0, nullptr, slot->pixels.data(),
            static_cast<UINT>(w * 4), 0);
        if (frameHasMipmaps_) context_->GenerateMips(frameView_.Get());
        lastUploadedSequence_ = incomingSequence;
        ReleaseFrameSlot(slot);
        return true;
    }
    void ResetTarget(WindowTarget& t) {
        if (d2d_) d2d_->SetTarget(nullptr);
        for (auto& target : t.d2dTargets) target.Reset();
        for (auto& view : t.rtvs) view.Reset();
        t.swapChain.Reset();
        t.hwnd = nullptr;
        t.width = t.height = 0;
    }
    void ResetDevice() {
        ResetTarget(mainTarget_); ResetTarget(pipTarget_); ClearFrame();
        d2d_.Reset(); d2dDevice_.Reset(); d2dFactory_.Reset(); dwrite_.Reset();
        white_.Reset(); shadow_.Reset(); dim_.Reset(); button_.Reset(); cyan_.Reset(); red_.Reset();
        textFormat_.Reset(); statusFormat_.Reset(); sampler_.Reset(); ps_.Reset(); vs_.Reset();
        context_.Reset(); device_.Reset(); factory_.Reset(); initialized_ = false;
    }
    void Text(const std::wstring& value, D2D1_RECT_F rect) {
        D2D1_RECT_F shade = rect; shade.left += 1; shade.right += 1; shade.top += 1; shade.bottom += 1;
        d2d_->DrawText(value.c_str(), static_cast<UINT32>(value.size()), textFormat_.Get(), shade,
            shadow_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
        d2d_->DrawText(value.c_str(), static_cast<UINT32>(value.size()), textFormat_.Get(), rect,
            white_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
    }
    void DrawNoFrame(HWND hwnd) {
        RECT rc{}; GetClientRect(hwnd, &rc);
        const std::wstring message = g_videoStatus.empty() ? Tr(TextId::VideoSelect) : g_videoStatus;
        d2d_->DrawText(message.c_str(), static_cast<UINT32>(message.size()), statusFormat_.Get(),
            D2D1::RectF(20, 12, static_cast<float>(std::max<LONG>(20, rc.right - 20)),
                static_cast<float>(std::max<LONG>(12, rc.bottom - 12))), white_.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
    }
    void DrawToolbar(HWND hwnd) {
        RECT rc{}; GetClientRect(hwnd, &rc);
        const int width = rc.right;
        const int videoComboW = std::max(180, width / 3);
        const int audioLabelX = 100 + videoComboW + 12;
        const int audioComboX = audioLabelX + 48;
        const int audioComboW = std::max(150, width - audioComboX - 315);
        const int resLabelX = audioComboX + audioComboW + 10;
        Text(Tr(TextId::CaptureDeviceLabel), D2D1::RectF(12, 8, 96, 36));
        Text(Tr(TextId::AudioLabel), D2D1::RectF(static_cast<float>(audioLabelX), 8, static_cast<float>(audioLabelX + 46), 36));
        Text(Tr(TextId::ResolutionLabel), D2D1::RectF(static_cast<float>(resLabelX), 8, static_cast<float>(resLabelX + 82), 36));
        Text(std::wstring(Tr(TextId::VolumeLabel)) + std::to_wstring(g_settings.volume) + L"%", D2D1::RectF(12, 48, 124, 80));
        if (!g_statusText.empty()) Text(g_statusText, D2D1::RectF(600, 48, static_cast<float>(std::max(720, width - 12)), 80));
    }
    void DrawStats(HWND hwnd) {
        RECT rc{}; GetClientRect(hwnd, &rc);
        std::wstring value = g_uiLanguage == UiLanguage::English
            ? L"CAP " + std::to_wstring(g_captureFps) + L" FPS  |  RENDER " + std::to_wstring(g_renderFps) + L" FPS  |  DROPPED " + std::to_wstring(g_droppedFps)
            : L"CAPTURA " + std::to_wstring(g_captureFps) + L" FPS  |  RENDER " + std::to_wstring(g_renderFps) + L" FPS  |  PERDIDOS " + std::to_wstring(g_droppedFps);
        Text(value, D2D1::RectF(12, 84, static_cast<float>(std::max<LONG>(240, rc.right - 12)), 110));
    }
    void DrawPipOverlay(HWND hwnd) {
        RECT rc{}; GetClientRect(hwnd, &rc);
        d2d_->FillRectangle(D2D1::RectF(0, 0, static_cast<float>(rc.right), static_cast<float>(rc.bottom)), dim_.Get());
        const int minDimension = static_cast<int>(std::min(rc.right, rc.bottom));
        const int buttonSize = std::clamp(minDimension / 5, 48, 64);
        const int gap = std::max(12, buttonSize / 4);
        const int totalWidth = buttonSize * 2 + gap;
        const float left = static_cast<float>((rc.right - totalWidth) / 2);
        const float top = static_cast<float>((rc.bottom - buttonSize) / 2);
        const float cx[2] = { left + buttonSize * 0.5f, left + buttonSize + gap + buttonSize * 0.5f };
        const float cy[2] = { top + buttonSize * 0.5f, top + buttonSize * 0.5f };
        const float r = buttonSize * 0.5f;
        const D2D1_ELLIPSE e1 = D2D1::Ellipse(D2D1::Point2F(cx[0], cy[0]), r, r);
        const D2D1_ELLIPSE e2 = D2D1::Ellipse(D2D1::Point2F(cx[1], cy[1]), r, r);
        d2d_->FillEllipse(e1, button_.Get()); d2d_->FillEllipse(e2, button_.Get());
        d2d_->DrawEllipse(e1, cyan_.Get(), 2.5f); d2d_->DrawEllipse(e2, red_.Get(), 2.5f);
        const float k = std::max(18.0f, r * 0.55f);
        d2d_->DrawRectangle(D2D1::RectF(cx[0]-k*0.55f, cy[0]-k*0.45f, cx[0]+k*0.55f, cy[0]+k*0.45f), cyan_.Get(), 2.4f);
        d2d_->DrawLine(D2D1::Point2F(cx[0]-k*0.05f,cy[0]+k*0.30f), D2D1::Point2F(cx[0]+k*0.45f,cy[0]-k*0.24f), cyan_.Get(), 2.6f);
        d2d_->DrawLine(D2D1::Point2F(cx[0]+k*0.45f,cy[0]-k*0.24f), D2D1::Point2F(cx[0]+k*0.12f,cy[0]-k*0.24f), cyan_.Get(), 2.6f);
        d2d_->DrawLine(D2D1::Point2F(cx[0]+k*0.45f,cy[0]-k*0.24f), D2D1::Point2F(cx[0]+k*0.45f,cy[0]+k*0.08f), cyan_.Get(), 2.6f);
        d2d_->DrawLine(D2D1::Point2F(cx[1]-k*0.42f,cy[1]-k*0.42f), D2D1::Point2F(cx[1]+k*0.42f,cy[1]+k*0.42f), red_.Get(), 3.0f);
        d2d_->DrawLine(D2D1::Point2F(cx[1]+k*0.42f,cy[1]-k*0.42f), D2D1::Point2F(cx[1]-k*0.42f,cy[1]+k*0.42f), red_.Get(), 3.0f);
    }
    bool initialized_ = false;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIFactory2> factory_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    ComPtr<ID3D11Texture2D> frameTexture_;
    ComPtr<ID3D11ShaderResourceView> frameView_;
    bool frameHasMipmaps_ = false;
    bool frameMipmapsRequested_ = false;
    int frameWidth_ = 0, frameHeight_ = 0;
    uint64_t lastUploadedSequence_ = 0;
    uint64_t lastCountedSequence_ = 0;
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2d_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IDWriteTextFormat> textFormat_, statusFormat_;
    ComPtr<ID2D1SolidColorBrush> white_, shadow_, dim_, button_, cyan_, red_;
    WindowTarget mainTarget_, pipTarget_;
    bool disabled_ = false;
    bool failureLogged_ = false;
    std::wstring failureStage_ = L"initialization";
    HRESULT failureHr_ = E_FAIL;
};

VideoRenderer g_videoRenderer;

void DestroyGdiPaintBuffer(GdiPaintBuffer& buffer) {
    if (buffer.dc && buffer.previousBitmap && buffer.previousBitmap != HGDI_ERROR)
        SelectObject(buffer.dc, buffer.previousBitmap);
    if (buffer.bitmap) DeleteObject(buffer.bitmap);
    if (buffer.dc) DeleteDC(buffer.dc);
    if (buffer.backgroundBrush) DeleteObject(buffer.backgroundBrush);
    buffer = {};
}

bool EnsureGdiPaintBuffer(HDC reference, GdiPaintBuffer& buffer, int width, int height) {
    if (!reference || width <= 0 || height <= 0) return false;
    if (!buffer.dc) buffer.dc = CreateCompatibleDC(reference);
    if (!buffer.dc) return false;
    if (buffer.bitmap && buffer.width == width && buffer.height == height)
        return buffer.backgroundBrush != nullptr;

    if (buffer.bitmap) {
        if (buffer.previousBitmap && buffer.previousBitmap != HGDI_ERROR)
            SelectObject(buffer.dc, buffer.previousBitmap);
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

void ClearCompatibilityFrame() {
    g_compatibilityFramePixels.clear();
    g_compatibilityFrameWidth = 0;
    g_compatibilityFrameHeight = 0;
    g_compatibilityFrameSequence = 0;
    g_compatibilityLastPresentedSequence = 0;
}

void PullLatestFrameForCompatibilityRenderer() {
    FrameSlot* slot = AcquireLatestFrameForRead();
    if (!slot) return;
    const int width = slot->width;
    const int height = slot->height;
    const uint64_t sequence = slot->sequence.load(std::memory_order_relaxed);
    if (width > 0 && height > 0 && !slot->pixels.empty()) {
        try {
            g_compatibilityFramePixels.assign(slot->pixels.begin(), slot->pixels.end());
            g_compatibilityFrameWidth = width;
            g_compatibilityFrameHeight = height;
            g_compatibilityFrameSequence = sequence;
        } catch (...) {
            Log(L"GDI compatibility renderer could not allocate its frame cache.");
        }
    }
    ReleaseFrameSlot(slot);
}

void DrawCompatibilityLabel(HDC dc, HFONT font, const std::wstring& text, RECT rect) {
    if (text.empty()) return;
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    RECT shadow = rect;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, text.c_str(), -1, &shadow, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetTextColor(dc, RGB(245, 248, 252));
    DrawTextW(dc, text.c_str(), -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetBkMode(dc, oldMode);
    if (oldFont) SelectObject(dc, oldFont);
}

bool DrawCompatibilityPreview(HDC target, HWND window, bool compact) {
    if (!target || !window) return false;
    PullLatestFrameForCompatibilityRenderer();

    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return true;
    GdiPaintBuffer& buffer = compact ? g_pipGdiBuffer : g_mainGdiBuffer;
    if (!EnsureGdiPaintBuffer(target, buffer, width, height)) {
        Log(L"GDI compatibility renderer could not create a backbuffer.");
        return false;
    }

    FillRect(buffer.dc, &client, buffer.backgroundBrush);
    if (g_compatibilityFrameWidth > 0 && g_compatibilityFrameHeight > 0 &&
        !g_compatibilityFramePixels.empty()) {
        const BYTE* pixels = g_compatibilityFramePixels.data();
        if (compact && g_pipHover) {
            try {
                buffer.dimPixels = g_compatibilityFramePixels;
                for (size_t i = 0; i + 3 < buffer.dimPixels.size(); i += 4) {
                    buffer.dimPixels[i] = static_cast<BYTE>((buffer.dimPixels[i] * 45) / 100);
                    buffer.dimPixels[i + 1] = static_cast<BYTE>((buffer.dimPixels[i + 1] * 45) / 100);
                    buffer.dimPixels[i + 2] = static_cast<BYTE>((buffer.dimPixels[i + 2] * 45) / 100);
                }
                pixels = buffer.dimPixels.data();
            } catch (...) {
                pixels = g_compatibilityFramePixels.data();
            }
        }

        const double scale = std::min(static_cast<double>(width) / g_compatibilityFrameWidth,
                                      static_cast<double>(height) / g_compatibilityFrameHeight);
        const int drawWidth = std::max(1, static_cast<int>(g_compatibilityFrameWidth * scale));
        const int drawHeight = std::max(1, static_cast<int>(g_compatibilityFrameHeight * scale));
        const int x = (width - drawWidth) / 2;
        const int y = (height - drawHeight) / 2;
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = g_compatibilityFrameWidth;
        bitmapInfo.bmiHeader.biHeight = -g_compatibilityFrameHeight;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(buffer.dc, HALFTONE);
        SetBrushOrgEx(buffer.dc, 0, 0, nullptr);
        StretchDIBits(buffer.dc, x, y, drawWidth, drawHeight, 0, 0,
            g_compatibilityFrameWidth, g_compatibilityFrameHeight, pixels,
            &bitmapInfo, DIB_RGB_COLORS, SRCCOPY);
    } else {
        const std::wstring message = g_videoStatus.empty() ? Tr(TextId::VideoSelect) : g_videoStatus;
        RECT statusRect = client;
        DrawCompatibilityLabel(buffer.dc, g_uiFont, message, statusRect);
    }

    if (!compact && g_overlayVisible) {
        const int clientWidth = width;
        const int videoComboW = std::max(180, clientWidth / 3);
        const int audioLabelX = 100 + videoComboW + 12;
        const int audioComboX = audioLabelX + 48;
        const int audioComboW = std::max(150, clientWidth - audioComboX - 315);
        const int resLabelX = audioComboX + audioComboW + 10;
        DrawCompatibilityLabel(buffer.dc, g_uiFont, Tr(TextId::CaptureDeviceLabel), RECT{ 12, 8, 96, 36 });
        DrawCompatibilityLabel(buffer.dc, g_uiFont, Tr(TextId::AudioLabel), RECT{ audioLabelX, 8, audioLabelX + 46, 36 });
        DrawCompatibilityLabel(buffer.dc, g_uiFont, Tr(TextId::ResolutionLabel), RECT{ resLabelX, 8, resLabelX + 82, 36 });
        DrawCompatibilityLabel(buffer.dc, g_uiFont, std::wstring(Tr(TextId::VolumeLabel)) +
            std::to_wstring(g_settings.volume) + L"%", RECT{ 12, 48, 124, 80 });
        if (!g_statusText.empty()) DrawCompatibilityLabel(buffer.dc, g_uiFont, g_statusText,
            RECT{ 600, 48, std::max(720, clientWidth - 12), 80 });
    }

    if (!compact && g_settings.showPerformanceStats) {
        const std::wstring stats = g_uiLanguage == UiLanguage::English
            ? L"CAP " + std::to_wstring(g_captureFps) + L" FPS  |  RENDER " + std::to_wstring(g_renderFps) + L" FPS  |  DROPPED " + std::to_wstring(g_droppedFps)
            : L"CAPTURA " + std::to_wstring(g_captureFps) + L" FPS  |  RENDER " + std::to_wstring(g_renderFps) + L" FPS  |  PERDIDOS " + std::to_wstring(g_droppedFps);
        DrawCompatibilityLabel(buffer.dc, g_uiFont, stats, RECT{ 12, 84, std::max(240, width - 12), 110 });
    }
    if (compact && g_pipHover) DrawPipHoverActions(buffer.dc, window);

    const BOOL copied = BitBlt(target, 0, 0, width, height, buffer.dc, 0, 0, SRCCOPY);
    if (copied && g_compatibilityFrameSequence > g_compatibilityLastPresentedSequence) {
        g_renderFramesInterval.fetch_add(1, std::memory_order_relaxed);
        g_compatibilityLastPresentedSequence = g_compatibilityFrameSequence;
    }
    return copied != FALSE;
}

void DestroyPaintBuffer(HWND hwnd) {
    g_videoRenderer.DestroyTarget(hwnd);
    if (hwnd == g_pipHwnd) DestroyGdiPaintBuffer(g_pipGdiBuffer);
    if (hwnd == g_hwnd) DestroyGdiPaintBuffer(g_mainGdiBuffer);
}
bool DrawPreview(HDC target, HWND window, bool compact = false) {
    if (g_videoRenderer.Render(window, compact)) return true;
    // Do not leave the user with a blank preview if D3D11/DXGI is unavailable
    // or rejected by the installed graphics driver. The GDI path is slower but
    // deliberately retained as a compatibility safety net.
    return DrawCompatibilityPreview(target, window, compact);
}

void DrawRendererFallback(HDC dc, HWND hwnd) {
    RECT rc{}; GetClientRect(hwnd, &rc);
    HBRUSH background = CreateSolidBrush(RGB(7, 10, 16));
    FillRect(dc, &rc, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(225, 232, 240));
    DrawTextW(dc, Tr(TextId::RendererUnavailable), -1, &rc,
        DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
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
    if (!visible) HideVolumeTooltip();
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

    // Windowed mode:
    //   showToolbar=true  => fixed toolbar, never auto-hide.
    //   showToolbar=false => hide by default, reveal only over the top zone.
    // Fullscreen always uses hover reveal and ignores the saved preference.
    const bool fixedWindowedToolbar = (g_windowMode == 0 && g_settings.showToolbar);
    if (fixedWindowedToolbar) {
        g_suppressToolbarRevealUntilLeave = false;
        if (!g_overlayVisible || force) ShowToolbarControls(true);
        return;
    }

    // Visibility is driven directly by WM_MOUSEMOVE/WM_MOUSELEAVE from the
    // parent and toolbar child controls. No polling timer is used.
    bool inTopZone = false;
    POINT cursor{};
    if (GetCursorPos(&cursor)) {
        POINT clientCursor = cursor;
        if (ScreenToClient(g_hwnd, &clientCursor)) {
            RECT client{};
            GetClientRect(g_hwnd, &client);
            constexpr LONG kToolbarRevealHeight = 104;
            inTopZone = clientCursor.x >= 0 && clientCursor.x < client.right &&
                clientCursor.y >= 0 && clientCursor.y <= kToolbarRevealHeight;
        }
    }

    // After the user unchecks the fixed-toolbar option, don't reveal it just
    // because the pointer remains over the File menu. Clear suppression only
    // after the pointer has left the toolbar's client-area reveal zone.
    if (!inTopZone) g_suppressToolbarRevealUntilLeave = false;

    // A dropped combo must keep the controls visible so the lower entries of
    // the resolution list remain selectable even outside the top reveal zone.
    const bool comboIsOpen = IsAnyToolbarComboDropped();
    const bool shouldShow = comboIsOpen || (inTopZone && !g_suppressToolbarRevealUntilLeave);
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
        if (!DrawPreview(dc, hwnd, true)) DrawRendererFallback(dc, hwnd);
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
        AppendMenuW(context, MF_STRING, IDM_PIP_CLOSE, Tr(TextId::HidePip));
        AppendMenuW(context, MF_STRING | (g_pipAlwaysOnTop ? MF_CHECKED : MF_UNCHECKED), IDM_PIP_TOPMOST, Tr(TextId::AlwaysOnTop));
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
        DestroyPaintBuffer(hwnd);
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
            MessageBoxW(g_hwnd, Tr(TextId::PipCreateFailed), L"PlayView", MB_OK | MB_ICONERROR);
            return;
        }
    }
    if (g_pipAlwaysOnTop) SetWindowPos(g_pipHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    else SetWindowPos(g_pipHwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ShowWindow(g_pipHwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_pipHwnd);
}

LRESULT CALLBACK AboutWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        HWND body = CreateWindowExW(0, L"STATIC", Tr(TextId::AboutBody),
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
            20, 22, 380, 92, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            165, 128, 90, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ABOUT_OK)),
            GetModuleHandleW(nullptr), nullptr);
        if (body) SetControlFont(body);
        if (ok) SetControlFont(ok);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_ABOUT_OK || LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        if (g_aboutHwnd == hwnd) g_aboutHwnd = nullptr;
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void CreateAboutWindow() {
    if (g_aboutHwnd && IsWindow(g_aboutHwnd)) {
        ShowWindow(g_aboutHwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(g_aboutHwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetForegroundWindow(g_aboutHwnd);
        SetFocus(g_aboutHwnd);
        return;
    }

    RECT owner{};
    if (g_hwnd && IsWindow(g_hwnd)) GetWindowRect(g_hwnd, &owner);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &owner, 0);
    constexpr int width = 420;
    constexpr int height = 205;
    const int x = owner.left + std::max(0L, (owner.right - owner.left - width) / 2);
    const int y = owner.top + std::max(0L, (owner.bottom - owner.top - height) / 2);

    // This is intentionally modeless. It remains above the owned main window
    // and PiP, but does not disable the rest of PlayView while it is open.
    g_aboutHwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST | WS_EX_CONTROLPARENT,
        kAboutWindowClass, Tr(TextId::AboutTitle),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height, g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_aboutHwnd) {
        Log(L"Could not create the modeless About window; GetLastError=" +
            std::to_wstring(GetLastError()));
        return;
    }
    ShowWindow(g_aboutHwnd, SW_SHOW);
    UpdateWindow(g_aboutHwnd);
    SetWindowPos(g_aboutHwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g_aboutHwnd);
    SetFocus(g_aboutHwnd);
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
        AddControl(0, L"BUTTON", Tr(TextId::ReloadButton), BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_RELOAD);
        AddControl(0, L"BUTTON", Tr(TextId::PipButton), BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_PIP);
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
        
        g_muteButton = AddControl(0, L"BUTTON", g_settings.muted ? Tr(TextId::UnmuteButton) : Tr(TextId::MuteButton), BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_MUTE);
        AddControl(0, L"BUTTON", Tr(TextId::ResyncButton), BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDC_SYNC);
        CreateAppMenu(hwnd);
        RefreshDevices(true);
        SetTimer(hwnd, TIMER_RECONNECT, 2500, nullptr);
        g_statsLastTick = GetTickCount64();
        SetTimer(hwnd, TIMER_STATS, 1000, nullptr);
        return 0;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tracking);
        UpdateOverlayVisibility(false);
        return 0;
    }
    case WM_MOUSELEAVE:
        UpdateOverlayVisibility(false);
        return 0;
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
        if ((commandId == IDC_VIDEO_DEVICE || commandId == IDC_AUDIO_DEVICE || commandId == IDC_RESOLUTION) &&
            HIWORD(wParam) == CBN_CLOSEUP) {
            PostMessageW(hwnd, WM_APP_OVERLAY_RECHECK, 0, 0);
            return 0;
        }
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
            SetWindowTextW(g_muteButton, g_settings.muted ? Tr(TextId::UnmuteButton) : Tr(TextId::MuteButton));
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
                if (CreateProfileShortcut(g_profileMenuEntries[index])) MessageBoxW(hwnd, Tr(TextId::ShortcutCreated), L"PlayView", MB_OK | MB_ICONINFORMATION);
                else MessageBoxW(hwnd, Tr(TextId::ShortcutCreationFailed), L"PlayView", MB_OK | MB_ICONWARNING);
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
            g_videoStatus = Tr(TextId::CapturesResynced);
            UpdateStatusText();
            return 0;
        case IDC_MUTE:
            g_settings.muted = !g_settings.muted;
            g_muted.store(g_settings.muted);
            SetWindowTextW(g_muteButton, g_settings.muted ? Tr(TextId::UnmuteButton) : Tr(TextId::MuteButton));
            SaveSettings();
            return 0;
        case IDC_PIP: TogglePictureInPicture(); return 0;
        case IDM_VIEW_PIP: TogglePictureInPicture(); return 0;
        case IDM_PIP_TOPMOST: TogglePipTopmost(); return 0;
        case IDM_SHOW_PERFORMANCE_STATS:
            g_settings.showPerformanceStats = !g_settings.showPerformanceStats;
            ResetPerformanceStats();
            SaveSettings();
            RequestMenuRebuild();
            InvalidateVideoSurface(hwnd);
            return 0;
        case IDM_SHOW_TOOLBAR:
            g_settings.showToolbar = !g_settings.showToolbar;
            if (g_windowMode == 0) {
                if (g_settings.showToolbar) {
                    // Checked in windowed mode means permanently visible.
                    g_suppressToolbarRevealUntilLeave = false;
                    ShowToolbarControls(true);
                } else {
                    // Unchecked means reveal only on hover, after the pointer
                    // has left the menu/toolbar zone and entered it again.
                    g_suppressToolbarRevealUntilLeave = true;
                    ShowToolbarControls(false);
                }
            } else {
                // Fullscreen behavior is independent of this saved setting.
                g_suppressToolbarRevealUntilLeave = false;
                UpdateOverlayVisibility(true);
            }
            SaveSettings();
            RequestMenuRebuild();
            return 0;
        case IDM_VIEW_WINDOW: SetWindowMode(0); return 0;
        case IDM_VIEW_FULLSCREEN: SetWindowMode(g_windowMode == 2 ? 0 : 2); return 0;
        case IDM_EXIT: PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0;
        case IDM_ABOUT:
            CreateAboutWindow();
            return 0;
        case IDM_LANGUAGE_AUTOMATIC:
            ApplyLanguageChange(LanguagePreference::Automatic);
            return 0;
        case IDM_LANGUAGE_SPANISH:
            ApplyLanguageChange(LanguagePreference::Spanish);
            return 0;
        case IDM_LANGUAGE_ENGLISH:
            ApplyLanguageChange(LanguagePreference::English);
            return 0;
        }
        break;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) HideVolumeTooltip();
        break;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        HideVolumeTooltip();
        break;
    case WM_SETTINGCHANGE:
        if (g_languagePreference == LanguagePreference::Automatic &&
            ResolveUiLanguage(LanguagePreference::Automatic) != g_uiLanguage) {
            ApplyLanguageChange(LanguagePreference::Automatic);
        }
        break;
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
    case WM_APP_CLEAR_FRAME:
        g_videoRenderer.ClearFrame();
        ClearCompatibilityFrame();
        InvalidateVideoSurface(hwnd);
        if (g_pipHwnd && IsWindowVisible(g_pipHwnd)) InvalidateVideoSurface(g_pipHwnd);
        return 0;
    case WM_APP_FRAME: {
        // Keep g_frameMessagePending set throughout rendering. Capture can
        // replace stale ready slots, but it cannot post another UI message
        // until this one completes. This bounds the message queue to one frame.
        // Render the newest capture frame directly from this UI-thread message.
        // WM_PAINT is deliberately retained for exposure/resize and fallback,
        // but it is not used as the video clock: Windows coalesces WM_PAINT and
        // may dispatch it at a lower cadence in an overlapped window.
        auto renderWindowNow = [](HWND target, bool compact) {
            if (!target || !IsWindow(target) || !IsWindowVisible(target) || IsIconic(target)) return;
            if (g_videoRenderer.Render(target, compact)) {
                // Render() refreshed the entire DXGI surface. Clear any older
                // invalid region so it cannot trigger a redundant paint later.
                ValidateRect(target, nullptr);
            } else {
                // D3D11 unavailable: let the existing GDI compatibility path
                // paint through WM_PAINT instead of blocking capture delivery.
                InvalidateVideoSurface(target);
                UpdateWindow(target);
            }
        };
        renderWindowNow(hwnd, false);
        if (g_pipHwnd && IsWindowVisible(g_pipHwnd)) renderWindowNow(g_pipHwnd, true);

        // Release the notification token only after both targets have consumed
        // the newest frame. Recheck after releasing it to close the race where
        // capture published a frame while the token was still held.
        g_frameMessagePending.store(false, std::memory_order_release);
        if (HasQueuedReadyFrame()) QueueVideoFrameMessage(hwnd);
        return 0;
    }
    case WM_APP_REBUILD_MENU:
        CreateAppMenu(hwnd);
        return 0;
    case WM_EXITMENULOOP:
        PostMessageW(hwnd, WM_APP_OVERLAY_RECHECK, 0, 0);
        return 0;
    case WM_APP_OVERLAY_RECHECK:
        UpdateOverlayVisibility(false);
        return 0;
    case WM_APP_VIDEO_MODES: {
        std::unique_ptr<VideoModesUpdate> update(reinterpret_cast<VideoModesUpdate*>(lParam));
        if (update && update->deviceId == g_settings.videoId) {
            g_videoModes = std::move(update->modes);
            PopulateResolutionCombo();
        }
        return 0;
    }
    case WM_APP_VIDEO_READY:
        g_videoStatus = Tr(TextId::VideoActive);
        g_nextVideoRetry = 0;
        UpdateStatusText();
        return 0;
    case WM_APP_VIDEO_LOST:
        g_videoStatus = Tr(TextId::VideoSignalLostRetry);
        g_videoHasFrame.store(false);
        g_nextVideoRetry = GetTickCount64() + 4000;
        ClearQueuedFrames();
        g_videoRenderer.ClearFrame();
        ClearCompatibilityFrame();
        UpdateStatusText();
        InvalidateVideoSurface(hwnd);
        if (g_pipHwnd && IsWindowVisible(g_pipHwnd)) InvalidateVideoSurface(g_pipHwnd);
        return 0;
    case WM_APP_AUDIO_LOST:
        g_audioStatus = Tr(TextId::AudioDisconnectedRetry);
        g_nextAudioRetry = GetTickCount64() + 2500;
        UpdateStatusText();
        return 0;
    case WM_TIMER:
        if (wParam == TIMER_STATS) {
            const ULONGLONG now = GetTickCount64();
            const ULONGLONG elapsed = (g_statsLastTick > 0 && now > g_statsLastTick)
                ? (now - g_statsLastTick) : 1000ULL;
            if (elapsed >= 250ULL) {
                const uint64_t captured = g_captureFramesInterval.exchange(0, std::memory_order_relaxed);
                const uint64_t rendered = g_renderFramesInterval.exchange(0, std::memory_order_relaxed);
                const uint64_t dropped = g_droppedFramesInterval.exchange(0, std::memory_order_relaxed);
                const ULONGLONG lastFrame = g_lastVideoFrameTick.load(std::memory_order_relaxed);
                const bool streamStale = !g_videoHasFrame.load(std::memory_order_relaxed) ||
                    lastFrame == 0 || now > lastFrame + 1200ULL;
                if (streamStale) {
                    g_captureFps = 0;
                    g_renderFps = 0;
                    g_droppedFps = 0;
                } else {
                    g_captureFps = CalculateFramesPerSecond(captured, elapsed);
                    g_renderFps = std::min(CalculateFramesPerSecond(rendered, elapsed), g_captureFps);
                    g_droppedFps = CalculateFramesPerSecond(dropped, elapsed);
                }
                g_statsLastTick = now;
            }
            if (g_settings.showPerformanceStats) {
                InvalidateVideoSurface(hwnd);
                if (g_pipHwnd && IsWindowVisible(g_pipHwnd)) InvalidateVideoSurface(g_pipHwnd);
            }
            return 0;
        }
        if (wParam == TIMER_RECONNECT) {
            const ULONGLONG now = GetTickCount64();
            const bool waitingForFirstFrame = g_videoCapture.IsRunning() && !g_videoHasFrame.load() &&
                g_videoStartTick > 0 && now - g_videoStartTick > 5000;
            const bool framesStopped = g_videoCapture.IsRunning() && g_videoHasFrame.load() &&
                g_lastVideoFrameTick.load() > 0 && now - g_lastVideoFrameTick.load() > 5000;
            if ((waitingForFirstFrame && g_videoStatus.find(Tr(TextId::VideoWaitingFrames)) == std::wstring::npos) ||
                (framesStopped && g_videoStatus.find(Tr(TextId::VideoStoppedFrames)) == std::wstring::npos)) {
                g_videoStatus = waitingForFirstFrame
                    ? Tr(TextId::VideoWaitingFrames)
                    : Tr(TextId::VideoStoppedFrames);
                UpdateStatusText();
                InvalidateVideoSurface(hwnd);
            }
            if (!g_settings.videoId.empty() && !g_videoCapture.IsRunning() && now >= g_nextVideoRetry) {
                g_videoStatus = Tr(TextId::VideoReconnecting);
                g_videoStartTick = now;
                g_nominalVideoFps.store(60, std::memory_order_relaxed);
                ResetPerformanceStats();
                g_lastVideoFrameTick.store(0, std::memory_order_relaxed);
                g_videoHasFrame.store(false);
                g_nextVideoRetry = now + 4000;
                g_videoCapture.Start(hwnd, g_settings.videoId, g_settings.width, g_settings.height);
                UpdateStatusText();
            }
            if (!g_settings.audioId.empty() && !g_audioCapture.IsRunning() && now >= g_nextAudioRetry) {
                // Keep using only the saved endpoint ID. A retry never selects a
                // different microphone or capture card audio endpoint.
                g_audioCapture.Start(hwnd, g_settings.audioId);
                g_audioStatus = Tr(TextId::AudioConnected);
                g_nextAudioRetry = now + 4000;
                UpdateStatusText();
            }
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        if (!DrawPreview(dc, hwnd, false)) DrawRendererFallback(dc, hwnd);
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
        HMENU settings = CreatePopupMenu();
        HMENU language = CreatePopupMenu();
        AppendMenuW(context, MF_STRING, IDM_VIEW_WINDOW, Tr(TextId::Windowed));
        AppendMenuW(context, MF_STRING, IDM_VIEW_FULLSCREEN, Tr(TextId::Fullscreen));
        AppendMenuW(context, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(context, MF_STRING, IDM_VIEW_PIP, Tr(TextId::PipMenu));
        AppendMenuW(language, MF_STRING | (g_languagePreference == LanguagePreference::Automatic ? MF_CHECKED : MF_UNCHECKED),
            IDM_LANGUAGE_AUTOMATIC, Tr(TextId::LanguageAutomatic));
        AppendMenuW(language, MF_STRING | (g_languagePreference == LanguagePreference::Spanish ? MF_CHECKED : MF_UNCHECKED),
            IDM_LANGUAGE_SPANISH, Tr(TextId::LanguageSpanish));
        AppendMenuW(language, MF_STRING | (g_languagePreference == LanguagePreference::English ? MF_CHECKED : MF_UNCHECKED),
            IDM_LANGUAGE_ENGLISH, Tr(TextId::LanguageEnglish));
        AppendMenuW(settings, MF_POPUP, reinterpret_cast<UINT_PTR>(language), Tr(TextId::LanguageMenu));
        AppendMenuW(context, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), Tr(TextId::SettingsMenu));
        AppendMenuW(context, MF_STRING, IDM_EXIT, Tr(TextId::Exit));
        TrackPopupMenu(context, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
        DestroyMenu(context);
        return 0;
    }
    case WM_CLOSE:
        KillTimer(hwnd, TIMER_RECONNECT);
        KillTimer(hwnd, TIMER_STATS);
        if (g_aboutHwnd && IsWindow(g_aboutHwnd)) DestroyWindow(g_aboutHwnd);
        if (g_pipHwnd) DestroyWindow(g_pipHwnd);
        g_videoCapture.Stop();
        g_audioCapture.Stop();
        SaveSettings();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        SaveSettings();
        KillTimer(hwnd, TIMER_RECONNECT);
        KillTimer(hwnd, TIMER_STATS);
        if (g_aboutHwnd && IsWindow(g_aboutHwnd)) DestroyWindow(g_aboutHwnd);
        if (g_pipHwnd && IsWindow(g_pipHwnd)) DestroyWindow(g_pipHwnd);
        g_videoCapture.Stop();
        g_audioCapture.Stop();
        g_videoRenderer.Destroy();
        DestroyGdiPaintBuffer(g_mainGdiBuffer);
        DestroyGdiPaintBuffer(g_pipGdiBuffer);
        ClearCompatibilityFrame();
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
    LoadLanguagePreference();
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
        MessageBoxW(nullptr, Tr(TextId::MediaFoundationStartupError),
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

    WNDCLASSEXW aboutClass{};
    aboutClass.cbSize = sizeof(aboutClass);
    aboutClass.lpfnWndProc = AboutWindowProc;
    aboutClass.hInstance = instance;
    aboutClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    aboutClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    aboutClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    aboutClass.lpszClassName = kAboutWindowClass;
    RegisterClassExW(&aboutClass);

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
    if (!g_activeProfileName.empty()) SetWindowTextW(hwnd, (std::wstring(Tr(TextId::WindowTitleProfile)) + g_activeProfileName).c_str());

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
        // The About window is modeless, so route its keyboard navigation
        // through the dialog manager without blocking the main message loop.
        if (g_aboutHwnd && IsDialogMessageW(g_aboutHwnd, &message)) continue;
        if (!accelerators || !TranslateAcceleratorW(hwnd, accelerators, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (accelerators) DestroyAcceleratorTable(accelerators);

    g_videoCapture.Stop();
    g_videoCapture.ShutdownAndWait();
    g_audioCapture.Stop();
    MFShutdown();
    if (SUCCEEDED(comHr)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
