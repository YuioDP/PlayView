# PlayView

**PlayView** es un monitor ligero de capturadoras para Windows. Está diseñado para mostrar la señal de una consola en una ventana sencilla, sin las funciones generales de un reproductor multimedia.

> Versión de desarrollo: **0.2.0**. El proyecto incorpora captura de video y audio mediante interfaces nativas de Windows, ventana Picture in Picture y perfiles independientes. Antes de considerarlo estable debe compilarse en Windows y probarse con el modelo concreto de capturadora.

## Funciones

### Captura y reproducción

- Enumeración de dispositivos de captura de video mediante Windows Media Foundation.
- Selección independiente del dispositivo de audio asociado a la capturadora.
- Persistencia de dispositivos mediante el identificador interno de Windows. Si falta el dispositivo elegido, PlayView informa de ello y no selecciona otra cámara ni otro dispositivo.
- Reconexión automática limitada al identificador del dispositivo seleccionado.
- Selección de las resoluciones que declara el controlador, además del modo automático.
- Conservación de la proporción de imagen, sin estirar ni deformar el contenido.
- Control de volumen y silencio para el audio capturado.
- Modos ventana, ventana sin bordes y pantalla completa.
- Recarga y resincronización de las capturas.
- Indicador de desconexión, ausencia de fotogramas y recuperación del dispositivo.

### Picture in Picture

- Ventana PiP independiente y redimensionable.
- Posibilidad de arrastrarla por el escritorio.
- Opción para mantenerla siempre encima de otras ventanas o desactivar esa preferencia.
- La ventana PiP dibuja los mismos fotogramas que la ventana principal. No inicia una segunda captura.
- El audio continúa desde el mismo proceso de captura, por lo que cambiar de ventana no reinicia ni interrumpe deliberadamente la reproducción.
- La ventana principal puede minimizarse mientras la ventana PiP permanece visible.
- Menú contextual con clic derecho para ocultar PiP o alternar la opción de ventana siempre visible.
- La preferencia de ventana siempre visible se recuerda en el perfil activo.
- Atajo `Ctrl+Mayús+P` para mostrar u ocultar PiP. `Esc` también la oculta cuando tiene el foco.

### Perfiles y accesos directos

- Guardar la configuración actual como un perfil independiente.
- Cargar un perfil sin modificar la configuración principal.
- Cada perfil conserva sus propios identificadores de video y audio, resolución, volumen, silencio y modo de ventana.
- Asignar un logo personalizado a cada perfil. Se admiten archivos `.ico`, `.png`, `.jpg`, `.jpeg` y `.bmp`; las imágenes rasterizadas se convierten a `.ico` mediante Windows Imaging Component.
- Crear un acceso directo en el escritorio con el nombre y el logo del perfil.
- Todos los accesos directos ejecutan el mismo `PlayView.exe` y cargan el archivo de perfil indicado mediante el argumento `--profile`.
- Cambiar el logo o eliminar perfiles desde el menú **Perfiles**.
- Volver a la configuración principal desde **Perfiles > Usar configuración principal**.

### Atajos

| Atajo | Acción |
|---|---|
| `F5` | Recargar la captura y actualizar los dispositivos detectados. |
| `F11` | Alternar pantalla completa. |
| `Esc` | Volver al modo ventana en la ventana principal. |
| `Ctrl+Mayús+P` | Mostrar u ocultar Picture in Picture. |

## Ubicación de los datos

La configuración y los perfiles se almacenan en:

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

El ejecutable no necesita que estas carpetas se creen manualmente. Se generan al iniciar el programa.

## Tecnologías

- C++20 y Win32.
- Windows Media Foundation para enumerar y capturar dispositivos.
- WinMM (`waveOut`) para reproducir el audio capturado.
- Windows Imaging Component para convertir logos personalizados a iconos de Windows.
- GDI para la presentación inicial de los fotogramas.
- CMake y MSVC (Visual Studio 2022 / Windows SDK) para compilar.

No se incorporan Electron, Qt, VLC, FFmpeg ni otros paquetes pesados.

## Compilar localmente

Requisitos: Windows 10/11 de 64 bits, Visual Studio 2022 con el componente de desarrollo C++ para escritorio y CMake.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

El ejecutable se genera en:

```text
build\Release\PlayView.exe
```

El runtime de C++ se enlaza estáticamente. Las bibliotecas multimedia utilizadas son componentes incluidos en Windows.

## Compilar con GitHub Actions

1. Crea un repositorio vacío llamado `PlayView` en GitHub.
2. Sube a la rama `main` el contenido de esta carpeta, incluida la carpeta oculta `.github`.
3. Abre **Actions** en GitHub.
4. El flujo **Build PlayView** compilará el proyecto en un runner Windows.
5. Descarga el artefacto `PlayView-Windows-x64` desde el resumen de la ejecución. Contiene `PlayView.exe`, este README, el logo y el icono.

Las instrucciones de publicación por Git también están en `INSTRUCCIONES_GITHUB.md`.

Para generar una GitHub Release, crea y publica una etiqueta:

```powershell
git tag v0.2.0
git push origin v0.2.0
```

## Perfiles: funcionamiento

Al guardar la configuración actual como perfil, el diálogo solicita el nombre del archivo. Después se puede elegir un logo, o utilizar el icono general de PlayView. Al finalizar, el programa ofrece crear el acceso directo en el escritorio.

Los accesos directos apuntan al mismo ejecutable y contienen un argumento similar a:

```text
PlayView.exe --profile "%LOCALAPPDATA%\PlayView\profiles\PS5.ini"
```

La selección de capturadora no cambia de forma automática cuando falta el dispositivo guardado. La reconexión periódica busca únicamente el identificador persistido en el perfil activo.

## Alcance y limitaciones conocidas

Esta versión es una base de desarrollo y necesita validación con hardware real. En particular:

1. **Rendimiento gráfico.** La presentación inicial convierte los fotogramas a RGB32 mediante Source Reader y GDI. La siguiente mejora prevista es Direct3D 11, seguida de mediciones de latencia y uso de CPU con la capturadora real.
2. **Audio de salida.** El audio capturado se reproduce por el dispositivo de salida predeterminado de Windows. El selector de audio elige la entrada de la capturadora; todavía no existe un selector independiente de auriculares o altavoces.
3. **Sincronización.** El botón `Resincronizar` reinicia las capturas de audio y video. No existe todavía un ajuste manual de desfase en milisegundos.
4. **Detección de señal HDMI.** Algunas capturadoras continúan entregando fotogramas negros cuando no reciben señal HDMI. En esos modelos el programa puede no distinguir una pantalla negra de la ausencia de señal si el controlador no informa ese estado.
5. **Formatos y frecuencias.** Las resoluciones disponibles dependen de lo que exponga el controlador. La frecuencia de cuadros y los formatos de color se ajustarán según las pruebas con el dispositivo real.
6. **Picture in Picture.** La ventana PiP comparte la captura y el audio, pero su rendimiento seguirá dependiendo del método GDI de presentación de esta versión.
7. **Identidad del perfil.** Los perfiles se guardan en AppData y los accesos directos dependen de que el ejecutable y la carpeta de datos sigan en sus ubicaciones. Si mueves el ejecutable, crea de nuevo los accesos directos.

## Prueba recomendada

1. Ejecuta PlayView con la capturadora conectada.
2. Selecciona el dispositivo de video y la entrada de audio.
3. Guarda un perfil y asigna un logo personalizado.
4. Crea el acceso directo y ábrelo para comprobar que carga ese perfil.
5. Activa PiP, redimensiona la ventana y minimiza la ventana principal. Comprueba que continúan el video y el audio.
6. Cierra el programa, desconecta la capturadora y vuelve a abrir el perfil. PlayView debe informar que no encuentra el dispositivo y no debe cambiar a la webcam.
7. Reconecta la capturadora y comprueba la recuperación del mismo dispositivo.

## Fuentes técnicas

- Microsoft Learn, [Audio/Video Capture in Media Foundation](https://learn.microsoft.com/en-us/windows/win32/medfound/audio-video-capture-in-media-foundation).
- Microsoft Learn, [MFCreateDeviceSource](https://learn.microsoft.com/en-us/windows/win32/api/mfidl/nf-mfidl-mfcreatedevicesource).
- Microsoft Learn, [Capture Device Attributes](https://learn.microsoft.com/en-us/windows/win32/medfound/capture-device-attributes).
- Microsoft Learn, [Windows Imaging Component](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-windows-imaging-codec).
