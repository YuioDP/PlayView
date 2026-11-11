# Subir PlayView 0.2.2 a GitHub y compilar el EXE

## Publicar el código

1. Descomprime `PlayView-0.2.2-source.zip`.
2. Crea en GitHub un repositorio vacío llamado `PlayView`, sin README, licencia ni `.gitignore`.
3. Abre PowerShell dentro de la carpeta `PlayView` descomprimida.
4. Ejecuta estos comandos y sustituye la URL de ejemplo por la dirección real del repositorio:

```powershell
git init
git add .
git commit -m "PlayView 0.2.2: doble búfer, captura 2D y correcciones de repintado"
git branch -M main
git remote add origin https://github.com/TU-USUARIO/PlayView.git
git push -u origin main
```

5. En GitHub entra en la pestaña **Actions** y abre la ejecución **Build PlayView**.
6. Cuando termine correctamente, descarga el artefacto `PlayView-Windows-x64` desde el resumen de esa ejecución. Contiene `PlayView.exe` y los recursos de marca.

## Crear una GitHub Release

Cuando quieras crear una versión identificada, ejecuta:

```powershell
git tag v0.2.2
git push origin v0.2.2
```

La acción compilará el programa y adjuntará `PlayView-Windows-x64.zip` a la GitHub Release.

## Si Git no está instalado

También puedes utilizar GitHub Desktop para publicar la carpeta. Comprueba que se incluya `.github/workflows/build.yml`, porque contiene la automatización de compilación.
