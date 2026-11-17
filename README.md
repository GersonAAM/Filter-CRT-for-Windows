# RetroCRT

Filtro CRT (tubo de rayos catódicos) en tiempo real para **toda la pantalla** en Windows 10 (2004+) y Windows 11. Captura el escritorio por GPU (DXGI) y aplica un *pixel shader* en una ventana superpuesta por la que pasan los clics y el teclado.

Ideal para juegos en ventana/sin bordes, emuladores, series y anime. Sin dependencias externas.

## Compilación

**Opción recomendada:** ejecuta `build.bat` (detecta automáticamente Visual Studio/MSVC o MinGW-w64). El ejecutable portable se genera en `bin\RetroCRT.exe` con el shader embebido.

**Con CMake:**
```bat
:: Visual Studio
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release

:: MinGW
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

## Uso

Abre `RetroCRT.exe`. Aparecerá un icono verde en la bandeja del sistema (clic derecho para opciones, doble clic para abrir ajustes).

| Atajo | Acción |
|---|---|
| `Ctrl + Alt + C` | Activar / desactivar el filtro |
| `Ctrl + Alt + V` | Cambiar al siguiente perfil |
| `Ctrl + Alt + M` | Abrir / cerrar panel de ajustes |

Los cambios en el panel (curvatura, scanlines, máscara RGB, aberración, glow, grano, etc.) se aplican en vivo. Usa **Guardar config.json** para conservarlos.

### Perfiles incluidos

| Perfil | Ideal para | Estilo |
|---|---|---|
| **TV Sony Trinitron** | Series y películas | Curvatura media, scanlines suaves, tono cálido |
| **Monitor PVM / Arcade** | Juegos retro y modernos | Scanlines nítidas, máscara RGB definida, casi plano |
| **VHS / TV Antigua** | Nostalgia | Curvatura fuerte, esquinas redondeadas, grano |
| **Sutil / Todo Terreno** | Escritorio y texto | Sin curvatura ni recorte, scanlines muy ligeras |

> **Tip:** Si un juego o película tiene HUD o subtítulos pegados a los bordes, pon **Curvatura** y **Bisel** en `0`.

## Modo portátil vs. instalado (UIAccess)

Por seguridad del S.O Windows, el menú Inicio, Alt+Tab y las notificaciones se dibujan por encima de cualquier ventana normal.

| Característica | Portátil (`bin\RetroCRT.exe`) | Instalado (`install-uiaccess.ps1`) |
|---|---|---|
| **Menú Inicio y paneles** | El filtro se pausa mientras están abiertos | Se ven **con filtro** (usa capa UIAccess) |
| **Instalación** | Ninguna (ejecutar desde cualquier carpeta) | Clic derecho en `install-uiaccess.ps1` → *Ejecutar con PowerShell* (admin) |
| **Ubicación de `config.json`** | Junto al `.exe` | `%APPDATA%\RetroCRT\` |

Para desinstalar la versión UIAccess: `powershell -ExecutionPolicy Bypass -File install-uiaccess.ps1 -Uninstall`.

## Personalización avanzada

- **Perfiles custom:** edita `config.json` directamente para duplicar o crear nuevos bloques dentro de `"profiles"`.
- **Editar el shader sin recompilar:** copia `shaders\crt.hlsl` a una carpeta `shaders\` junto al `.exe`, modifícalo y pulsa **Recargar shader** en la bandeja del sistema.

## Limitaciones y notas

- **Pantalla completa exclusiva:** tapa el overlay. Usa modo **ventana** o **ventana sin bordes**.
- **Vídeo con DRM:** Netflix, Disney+, etc. en navegador se verán en negro por protección de contenido. Desactiva la aceleración por hardware del navegador o usa reproductores locales (mpv, VLC, MPC-HC).
- **Cursor y monitor:** solo funciona en el monitor principal y el puntero del ratón no se deforma con la curvatura.
- **Errores:** si algo falla o un atajo choca con otra app, revisa `retrocrt.log` junto al ejecutable.