# PlayView 1.1.1 (English)

**PlayView** is a lightweight Windows capture-card monitor. It is designed to let you connect a game console, select the capture input, and play from a simple window without the extra features found in general-purpose media players.

## Features

### Video and audio capture

- Detects video capture devices supported by Windows Media Foundation.
- Lets you select the capture card's audio input separately.
- Remembers the selected devices. If a saved device is unavailable, PlayView reports it and does not switch to a webcam or another device.
- Attempts to restore the signal when the originally selected device becomes available again.
- Lets you choose from the formats and resolutions reported by the capture device's driver.
- Preserves the source aspect ratio to prevent stretching or distortion.
- Includes volume and mute controls, with the current level shown as a percentage.
- Provides capture reload and resynchronization commands.

### Display modes

- **Windowed mode:** Move, resize, or maximize the window. PlayView remembers its size, position, and maximized state.
- **Fullscreen mode:** Toggle fullscreen with `F11`. When you exit, the previous window state is restored.
- **Options bar:** In windowed mode, the bar can stay visible or appear when the pointer moves into the top area. In fullscreen mode, it appears and hides automatically as the pointer enters or leaves the top area, regardless of the windowed-mode preference.
- Controls are overlaid on the video and do not reserve space inside the displayed image.

### Picture in Picture (PiP)

- Opens a separate, resizable floating window.
- Can be dragged around the desktop and configured to stay on top of other windows.
- Shares the same video and audio capture session; it does not open a second instance of the capture device.
- Hovering over the PiP window reveals controls to return to PlayView or close only the PiP window.
- The video returns to its normal appearance when the pointer leaves the PiP window.

### Profiles and shortcuts

- Save separate configurations for different consoles or capture setups.
- Each profile stores its video and audio devices, resolution, volume, mute state, and available preferences.
- Supports custom profile icons in `.ico`, `.png`, `.jpg`, `.jpeg`, and `.bmp` formats.
- Creates Windows shortcuts with the profile's name and icon. All shortcuts launch the same `PlayView.exe`.

### Language support

PlayView includes Spanish and English. Open **Settings → Language** (shown as **Ajustes → Idioma** in Spanish) and choose one of these options:

- **Automatic (Windows display language):** uses Spanish when the Windows display language is Spanish and English when it is English. If Windows reports another language, PlayView falls back to Spanish.
- **Español:** forces the Spanish interface.
- **English:** forces the English interface.

The language preference is saved globally in `%LOCALAPPDATA%\PlayView\settings.ini`. It is independent of capture profiles, so switching between console profiles does not change the application language. The same setting is available from the fullscreen right-click context menu under **Settings → Language**. When Automatic is selected, PlayView re-checks the Windows UI language when Windows broadcasts a settings change.

## First-time setup

1. Connect the capture card to your PC and connect the console to the capture card's HDMI input.
2. Launch `PlayView.exe`.
3. Use the top options bar to select the video device and the corresponding audio input.
4. Choose an available resolution and adjust the volume.
5. Save a profile from the **Profiles** menu to easily restore these settings in future sessions.

PlayView remembers the selected devices. If you launch the program while the capture card is disconnected, it displays a warning instead of switching automatically to another camera. After reconnecting the device, use the reload command or allow PlayView to restore the selected device.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `F5` | Refresh devices and restart capture. |
| `F11` | Toggle between windowed and fullscreen modes. |
| `Esc` | Exit fullscreen or close/hide PiP, depending on the active window. |
| `Ctrl + Shift + P` | Show or hide Picture in Picture. |

You can also right-click the windows to access the context menu when the options bar is hidden.

## Configuration and files

Preferences, profiles, icons, and logs are stored in the current Windows user's local application data folder:

```text
%LOCALAPPDATA%\PlayView\
    settings.ini
    profiles\
        profile-name.ini
    profiles-icons\
        profile-name.ico
    logs\
        playview.log
```

These folders are created automatically. No additional files need to be installed or configured manually. Your settings remain available when you update or replace `PlayView.exe`.

## Requirements

- 64-bit Windows 10 or Windows 11.
- A capture card supported by Windows Media Foundation (for example, a UVC device recognized by Windows).
- The capture card's drivers must be installed correctly.

Available resolutions and frame rates depend on the connected device and its driver. Captured audio is played through the default Windows output device.

## Technical information

PlayView is written in C++20 and uses native Windows components:

- Windows Media Foundation for video and audio device discovery and capture.
- WinMM (`waveOut`) for captured-audio playback.
- Win32 and GDI with double buffering for the interface and video presentation.
- Windows Imaging Component for processing custom profile icons.

PlayView does not use Electron and does not require VLC, FFmpeg, or another media player to be installed. The application statically links the C++ runtime; the multimedia APIs it uses are included with Windows.

## Known limitations

- Capture audio is played through the default Windows output device. The audio selector in PlayView selects the capture card's input device.
- The **Resynchronize** command restarts audio and video capture; it does not provide a manual offset adjustment in milliseconds.
- Some capture cards continue sending black frames when there is no HDMI input. On those devices, Windows may not provide enough information to distinguish a black image from a missing HDMI signal.
- Profiles and shortcuts depend on the location of the executable and PlayView's data folder. If you move the program, you may need to recreate the shortcuts.

---

**PlayView 1.1.1** · A lightweight capture-card monitor for Windows.

---

# PlayView 1.1.1 (Español)

**PlayView** es un monitor ligero de capturadoras para Windows. Está diseñado para conectar una consola, seleccionar la entrada de captura y jugar desde una ventana sencilla, sin las funciones adicionales de los reproductores multimedia de propósito general.

## Funciones

### Captura de video y audio

- Detecta dispositivos de captura de video compatibles con Windows Media Foundation.
- Permite seleccionar por separado la entrada de audio de la capturadora.
- Recuerda los dispositivos seleccionados. Si un dispositivo guardado no está disponible, PlayView lo indica y no lo sustituye por una webcam ni por otro dispositivo.
- Intenta recuperar la señal cuando el dispositivo seleccionado originalmente vuelve a estar disponible.
- Permite elegir entre los formatos y las resoluciones que ofrece el controlador de la capturadora.
- Conserva la proporción de imagen original para evitar estiramientos o deformaciones.
- Incluye controles de volumen y silencio, con el nivel actual indicado en porcentaje.
- Permite recargar la captura y ejecutar una resincronización.

### Modos de visualización

- **Modo ventana:** permite mover, cambiar el tamaño o maximizar la ventana. PlayView recuerda sus dimensiones, posición y estado maximizado.
- **Pantalla completa:** permite alternar a pantalla completa con `F11`. Al salir, se restaura el estado anterior de la ventana.
- **Barra de opciones:** en modo ventana, la barra puede permanecer visible o aparecer cuando el cursor se desplaza hacia la zona superior. En pantalla completa, aparece y se oculta automáticamente al entrar o salir el cursor de esa zona, independientemente de la preferencia del modo ventana.
- Los controles se superponen al video y no reservan espacio dentro de la imagen mostrada.

### Picture in Picture (PiP)

- Abre una ventana flotante independiente cuyo tamaño se puede ajustar.
- Se puede arrastrar por el escritorio y configurar para que permanezca por encima de otras ventanas.
- Comparte la misma sesión de captura de video y audio; no abre una segunda instancia de la capturadora.
- Al pasar el cursor sobre la ventana PiP, aparecen controles para regresar a PlayView o cerrar únicamente la ventana PiP.
- El video recupera su apariencia normal cuando el cursor sale de la ventana PiP.

### Perfiles y accesos directos

- Permite guardar configuraciones independientes para distintas consolas o configuraciones de captura.
- Cada perfil almacena sus dispositivos de video y audio, resolución, volumen, estado de silencio y las preferencias disponibles.
- Admite iconos personalizados para los perfiles en formatos `.ico`, `.png`, `.jpg`, `.jpeg` y `.bmp`.
- Crea accesos directos de Windows con el nombre y el icono del perfil. Todos los accesos directos ejecutan el mismo `PlayView.exe`.

### Idiomas

PlayView incluye español e inglés. Abre **Ajustes → Idioma** (mostrado como **Settings → Language** en inglés) y elige una de estas opciones:

- **Automático (idioma de Windows):** utiliza español cuando el idioma de pantalla de Windows es español e inglés cuando es inglés. Si Windows informa otro idioma, PlayView utiliza español como alternativa.
- **Español:** fuerza la interfaz en español.
- **English:** fuerza la interfaz en inglés.

La preferencia de idioma se guarda de forma global en `%LOCALAPPDATA%\PlayView\settings.ini`. Es independiente de los perfiles de captura, por lo que cambiar entre perfiles de consolas no modifica el idioma de la aplicación. La misma opción está disponible en el menú contextual que aparece al hacer clic derecho en pantalla completa, dentro de **Ajustes → Idioma**. Cuando se selecciona Automático, PlayView vuelve a comprobar el idioma de la interfaz de Windows cuando el sistema notifica un cambio de configuración.

## Primer uso

1. Conecta la capturadora al PC y conecta la consola a la entrada HDMI de la capturadora.
2. Ejecuta `PlayView.exe`.
3. Utiliza la barra superior de opciones para seleccionar el dispositivo de video y la entrada de audio correspondiente.
4. Elige una resolución disponible y ajusta el volumen.
5. Guarda un perfil desde el menú **Perfiles** para recuperar fácilmente esta configuración en futuras sesiones.

PlayView recuerda los dispositivos seleccionados. Si ejecutas el programa mientras la capturadora está desconectada, mostrará una advertencia en lugar de cambiar automáticamente a otra cámara. Después de volver a conectar el dispositivo, utiliza la opción de recarga o permite que PlayView recupere el dispositivo seleccionado.

## Atajos de teclado

| Atajo | Acción |
|---|---|
| `F5` | Actualizar los dispositivos y reiniciar la captura. |
| `F11` | Alternar entre el modo ventana y pantalla completa. |
| `Esc` | Salir de pantalla completa o cerrar/ocultar PiP, según la ventana activa. |
| `Ctrl + Shift + P` | Mostrar u ocultar Picture in Picture. |

También puedes hacer clic derecho en las ventanas para acceder al menú contextual cuando la barra de opciones esté oculta.

## Configuración y archivos

Las preferencias, los perfiles, los iconos y los registros se almacenan en la carpeta de datos de aplicaciones locales del usuario actual de Windows:

```text
%LOCALAPPDATA%\PlayView\
    settings.ini
    profiles\
        nombre-del-perfil.ini
    profiles-icons\
        nombre-del-perfil.ico
    logs\
        playview.log
```

Estas carpetas se crean automáticamente. No es necesario instalar ni configurar archivos adicionales manualmente. La configuración se conserva al actualizar o reemplazar `PlayView.exe`.

## Requisitos

- Windows 10 o Windows 11 de 64 bits.
- Una capturadora compatible con Windows Media Foundation (por ejemplo, un dispositivo UVC reconocido por Windows).
- Los controladores de la capturadora deben estar correctamente instalados.

Las resoluciones y frecuencias de fotogramas disponibles dependen del dispositivo conectado y de su controlador. El audio capturado se reproduce a través del dispositivo de salida predeterminado de Windows.

## Información técnica

PlayView está desarrollado en C++20 y utiliza componentes nativos de Windows:

- Windows Media Foundation para detectar dispositivos de video y audio y realizar la captura.
- WinMM (`waveOut`) para reproducir el audio capturado.
- Win32 y GDI con doble búfer para la interfaz y la presentación de video.
- Windows Imaging Component para procesar los iconos personalizados de los perfiles.

PlayView no utiliza Electron ni requiere tener instalado VLC, FFmpeg u otro reproductor multimedia. La aplicación enlaza estáticamente el runtime de C++; las API multimedia que utiliza están incluidas en Windows.

## Limitaciones conocidas

- El audio capturado se reproduce a través del dispositivo de salida predeterminado de Windows. El selector de audio de PlayView permite elegir la entrada de audio de la capturadora.
- El comando **Resincronizar** reinicia la captura de audio y video; no permite establecer manualmente un desfase en milisegundos.
- Algunas capturadoras siguen enviando fotogramas negros cuando no reciben señal HDMI. En esos dispositivos, Windows puede no proporcionar información suficiente para distinguir una imagen negra de la ausencia de señal HDMI.
- Los perfiles y accesos directos dependen de la ubicación del ejecutable y de la carpeta de datos de PlayView. Si mueves el programa, puede ser necesario volver a crear los accesos directos.

---

**PlayView 1.1.1** · Un monitor ligero de capturadoras para Windows.
