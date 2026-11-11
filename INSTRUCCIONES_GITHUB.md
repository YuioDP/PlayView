# Subir PlayView 0.2.4 a GitHub y compilar el EXE

## Si ya tienes el repositorio PlayView

1. Descomprime `PlayView-0.2.4-source.zip`.
2. Sustituye el contenido del repositorio por los archivos de la carpeta `PlayView` descomprimida. Conserva la carpeta oculta `.github`.
3. Abre PowerShell dentro de la carpeta del repositorio y ejecuta:

```powershell
git add -A
git commit -m "PlayView 0.2.4: preferencia de barra y cursor PiP"
git push origin main
```

## Si vas a crear el repositorio desde cero

1. Crea un repositorio vacío llamado `PlayView` en GitHub, sin README, licencia ni `.gitignore`.
2. Abre PowerShell dentro de la carpeta `PlayView` descomprimida y ejecuta:

```powershell
git init
git add .
git commit -m "PlayView 0.2.4: preferencia de barra y cursor PiP"
git branch -M main
git remote add origin https://github.com/TU-USUARIO/PlayView.git
git push -u origin main
```

Sustituye `TU-USUARIO` por tu usuario real de GitHub.

## Descargar el ejecutable

1. En GitHub entra en **Actions** y abre la ejecución **Build PlayView**.
2. Cuando termine correctamente, descarga el artefacto `PlayView-Windows-x64` desde el resumen de la ejecución. Contiene `PlayView.exe` y los recursos de marca.

## Crear una GitHub Release

```powershell
git tag v0.2.4
git push origin v0.2.4
```

La acción compilará el programa y adjuntará `PlayView-Windows-x64.zip` a la GitHub Release.

## Cambios de esta versión

Se conserva el sistema de captura de audio y video que ya funciona correctamente. En modo ventana, el elemento **Archivo → Mostrar barra de opciones** funciona como preferencia fija: activado mantiene la barra visible; desactivado la muestra solo al acercar el cursor a la franja superior. En pantalla completa, la barra siempre utiliza el comportamiento de aparición y ocultación por proximidad, independientemente del ajuste de ventana. Se elimina el rectángulo gris de fondo y las etiquetas se dibujan sobre el búfer de video. En PiP, los botones contextuales muestran el cursor de flecha, mientras que el cursor de movimiento se reserva al área arrastrable.
