# PlayView 1.0.0

**PlayView** es un monitor ligero para capturadoras de video en Windows. Está pensado para conectar una consola, seleccionar la señal de captura y jugar desde una ventana sencilla, sin las funciones generales de un reproductor multimedia.

## Características

### Captura de video y audio

- Detecta dispositivos de captura de video compatibles con Windows Media Foundation.
- Permite seleccionar por separado la entrada de audio de la capturadora.
- Recuerda los dispositivos elegidos. Si uno no está conectado, informa de que no está disponible y no lo sustituye por una webcam u otro dispositivo.
- Intenta recuperar la señal cuando vuelve a estar disponible el dispositivo seleccionado.
- Permite elegir entre los formatos y resoluciones que ofrece el controlador de la capturadora.
- Conserva la proporción de la imagen para evitar que se estire o deforme.
- Incluye control de volumen y silencio. El nivel se muestra en porcentaje.
- Dispone de recarga y resincronización de las capturas.

### Modos de visualización

- **Ventana:** permite mover, redimensionar y maximizar la ventana. Recuerda su tamaño, posición y estado maximizado.
- **Pantalla completa:** se activa y desactiva con `F11`. Al salir, restaura el estado anterior de la ventana.
- **Barra de opciones:** en modo ventana puede permanecer fija o mostrarse al acercar el cursor a la zona superior. En pantalla completa aparece y se oculta automáticamente al acercar o retirar el cursor, independientemente de esa preferencia.
- Los controles se superponen al video y no reservan espacio dentro de la imagen.

### Picture in Picture (PiP)

- Abre una ventana flotante independiente y redimensionable.
- Puede arrastrarse por el escritorio y configurarse para permanecer siempre encima de otras ventanas.
- Comparte la misma captura de video y audio; no abre una segunda instancia del dispositivo.
- Al pasar el cursor sobre la miniventana aparecen controles para volver a PlayView o cerrar únicamente PiP.
- Al retirar el cursor, la imagen recupera su visualización normal.

### Perfiles y accesos directos

- Permite guardar diferentes configuraciones para varias consolas o conexiones.
- Cada perfil conserva sus dispositivos de video y audio, resolución, volumen, silencio y preferencias disponibles.
- Admite logos personalizados en formato `.ico`, `.png`, `.jpg`, `.jpeg` y `.bmp`.
- Puede crear un acceso directo de Windows con el nombre y el icono del perfil. Todos los accesos directos utilizan el mismo `PlayView.exe`.

## Primer uso

1. Conecta la capturadora al PC y conecta la consola a la entrada HDMI de la capturadora.
2. Abre `PlayView.exe`.
3. En la barra superior, selecciona el dispositivo de video y la entrada de audio correspondientes.
4. Selecciona una resolución disponible y ajusta el volumen.
5. Guarda un perfil desde el menú **Perfiles** para recuperar esa configuración fácilmente en futuras sesiones.

PlayView recuerda los dispositivos seleccionados. Si abres el programa sin conectar la capturadora, mostrará un aviso en lugar de cambiar automáticamente a otra cámara. Después de reconectarla, utiliza la opción de recarga o espera a que se recupere el dispositivo seleccionado.

## Atajos de teclado

| Atajo | Acción |
|---|---|
| `F5` | Recargar los dispositivos y reiniciar las capturas. |
| `F11` | Alternar entre ventana y pantalla completa. |
| `Esc` | Salir de pantalla completa o cerrar/ocultar PiP según la ventana activa. |
| `Ctrl + Mayús + P` | Mostrar u ocultar Picture in Picture. |

También puedes utilizar el menú contextual con clic derecho en las ventanas cuando la barra de opciones no esté visible.

## Configuración y archivos

Las preferencias, perfiles, iconos y registros se guardan en la carpeta del usuario:

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

Estas carpetas se crean automáticamente. No es necesario instalar ni configurar manualmente archivos adicionales. La configuración permanece al actualizar o reemplazar `PlayView.exe`.

## Requisitos

- Windows 10 u 11 de 64 bits.
- Capturadora compatible con Windows Media Foundation (por ejemplo, dispositivos UVC reconocidos por Windows).
- Controladores de la capturadora instalados correctamente.

Las resoluciones y frecuencias disponibles dependen de las capacidades y del controlador del dispositivo conectado. El audio capturado se reproduce mediante el dispositivo de salida predeterminado de Windows.

## Información técnica

PlayView está desarrollado en C++20 y utiliza componentes nativos de Windows:

- Windows Media Foundation para detectar y capturar audio y video.
- WinMM (`waveOut`) para reproducir el audio capturado.
- Win32 y GDI con doble búfer para la interfaz y la presentación de video.
- Windows Imaging Component para procesar iconos personalizados.

No utiliza Electron ni requiere instalar VLC, FFmpeg u otro reproductor multimedia. La compilación enlaza estáticamente el runtime de C++; las APIs multimedia utilizadas forman parte de Windows.

## Limitaciones conocidas

- El audio de la capturadora se reproduce por el dispositivo de salida predeterminado de Windows. La selección de audio dentro de PlayView corresponde a la entrada de audio de la capturadora.
- La acción **Resincronizar** reinicia las capturas de audio y video; no ofrece un ajuste manual de desfase en milisegundos.
- Algunas capturadoras siguen enviando fotogramas negros cuando no reciben señal HDMI. En esos dispositivos, Windows puede no proporcionar información suficiente para diferenciar una pantalla negra de una señal HDMI ausente.
- Los perfiles y accesos directos dependen de la ubicación del ejecutable y de la carpeta de datos de PlayView. Si mueves el programa a otra ubicación, puede ser necesario volver a crear los accesos directos.

---

**PlayView 1.0.0** · Monitor ligero de capturadoras para Windows.
