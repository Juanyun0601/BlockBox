# Lanzador BlockBox

[![License: GPL-3.0](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

[简体中文](README.md) | [繁體中文](README.zh-TW.md) | [English](README.en.md) | [日本語](README.ja.md) | [Русский](README.ru.md) | **Español**

Un lanzador de Minecraft multiplataforma (edición Java / Bedrock) desarrollado con Qt y C++, con asistencia de IA en todo el proceso. Una sola caja para todo el mundo de bloques.

## Características

- **Gestión de instancias**: crear, importar, cambiar entre varias versiones e iniciar instancias de Java / Bedrock, con migración desde otros lanzadores
- **Descarga de contenido**: mods, modpacks, paquetes de recursos y shaders, con fuentes de CurseForge y Modrinth
- **Asistente de IA**: chat con IA, diagnóstico inteligente de registros de fallos y asistente de comandos (lenguaje natural a comandos del juego)
- **Sistema de plugins**: SDK de plugins y gestor de plugins integrados, con soporte para extensiones locales (ver `sdk/`)
- **Edición de skins**: editor de skins integrado con vista previa 3D
- **Multijugador y red**: transferencia de partidas en red local y conexión por túneles
- **Gestión de partidas guardadas**: copia de seguridad y restauración de mundos
- **Multiidioma**: chino simplificado, chino tradicional, inglés, español, japonés y ruso

## Plataformas compatibles

- Windows
- macOS
- Linux
- HarmonyOS
- Android

## Tecnologías

- **Framework**: Qt 5.15+ / Qt 6.x
- **Lenguaje**: C++
- **Interfaz**: Qt Widgets + temas QSS
- **Compilación**: qmake

## Estructura del proyecto

```
├── main.cpp            # Punto de entrada del programa
├── MainWindow.cpp      # Ventana principal
├── MainWindowPages.cpp # Ensamblado de páginas
├── platform.cpp/.h     # Adaptación por plataforma
├── BlockBox.pro        # Archivo de proyecto qmake
├── resources.qrc       # Manifiesto de recursos de Qt
├── components/         # Componentes de interfaz (diálogos, barra lateral, tarjetas...)
├── pages/              # Páginas de funciones (incluye pages/settings/)
├── utils/              # Lógica de negocio (inicio, descargas, autenticación, mods, plugins, red...)
├── layouts/            # Diseños personalizados
├── styles/             # Temas de estilo QSS
├── resources/          # Datos JSON integrados
├── Images/             # Iconos e imágenes
├── translations/       # Traducciones (.ts / .qm)
├── sdk/                # Cabeceras del SDK de plugins
├── tests/              # Sondas y proyectos de prueba
├── android/            # Recursos del proyecto Android
└── harmony/            # Notas del proyecto HarmonyOS
```

## Compilación

### Windows

1. Instala Qt Creator y un compilador MinGW (o MSVC)
2. Abre Qt Creator e importa `BlockBox.pro`
3. Selecciona un kit de 64 bits, compila y ejecuta

### macOS

1. Instala Qt Creator y Xcode
2. Importa `BlockBox.pro` y selecciona el kit Clang de 64 bits
3. Compila y ejecuta

### Linux

```bash
sudo apt-get install qtbase5-dev qt5-qmake build-essential
qmake BlockBox.pro
make
./BlockBox
```

### HarmonyOS

Instala el entorno de desarrollo de HarmonyOS y el plugin Qt for HarmonyOS, y después selecciona un kit de HarmonyOS en Qt Creator para compilar y desplegar.

## Configuración de claves de API

Este repositorio **no contiene ninguna clave de API de terceros**. Todas las credenciales se configuran localmente y nunca se suben con el código:

- **Inicio de sesión con cuenta de Microsoft**: el `client_id` de `utils/AuthManager.cpp` es un marcador de posición; introduce tu propio valor en local. **No envíes claves reales al repositorio** (hay una comprobación pre-push configurada para bloquearlas)

## Adaptación por plataforma

Las diferencias entre plataformas se gestionan mediante compilación condicional: las macros `WINDOWS_OS` / `MAC_OS` / `LINUX_OS` / `HARMONY_OS`, implementadas en `platform.cpp` y `platform.h`, que cubren el directorio de datos de la aplicación, las rutas de configuración y la detección de plataforma.

## Licencia

Este proyecto se distribuye bajo la [licencia GNU GPL v3.0](LICENSE) © Juanyun0601

El programa se ofrece «tal cual», sin garantía de ningún tipo. Copiarlo, modificarlo y redistribuirlo exige cumplir la GPL-3.0, y las versiones modificadas también deben publicar su código bajo GPL-3.0.

## Contribuir

Las issues y los pull requests son bienvenidos. El código aportado se incorpora al proyecto bajo GPL-3.0.

---

[简体中文](README.md) · [繁體中文](README.zh-TW.md) · [English](README.en.md) · [日本語](README.ja.md) · [Русский](README.ru.md)
