# BlockBox Launcher

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

[简体中文](README.md) | [繁體中文](README.zh-TW.md) | **English** | [日本語](README.ja.md) | [Русский](README.ru.md) | [Español](README.es.md)

A cross-platform Minecraft launcher (Java / Bedrock edition) built with Qt and C++, with AI assistance all the way through. One box holds the whole blocky world.

## Features

- **Instance management**: create, import, switch between and launch Java / Bedrock instances, with migration from other launchers
- **Content downloads**: mods, modpacks, resource packs and shaders, aggregated from CurseForge and Modrinth
- **AI assistant**: AI chat, smart crash-log diagnosis, and a command assistant (natural language to in-game commands)
- **Plugin system**: built-in plugin SDK and plugin manager with support for local extensions (see `sdk/`)
- **Skin editing**: built-in skin editor with 3D preview
- **Multiplayer and networking**: LAN game transfer and tunnel networking
- **Save management**: world backup and restore
- **Multilingual**: Simplified Chinese, Traditional Chinese, English, Spanish, Japanese, Russian

## Supported platforms

- Windows
- macOS
- Linux
- HarmonyOS
- Android

## Tech stack

- **Framework**: Qt 5.15+ / Qt 6.x
- **Language**: C++
- **UI**: Qt Widgets + QSS themes
- **Build**: qmake

## Project structure

```
├── main.cpp            # Program entry
├── MainWindow.cpp      # Main window
├── MainWindowPages.cpp # Page assembly
├── platform.cpp/.h     # Platform adaptation
├── BlockBox.pro        # qmake project file
├── resources.qrc       # Qt resource manifest
├── components/         # UI components (dialogs, sidebar, cards, ...)
├── pages/              # Feature pages (including pages/settings/)
├── utils/              # Business logic (launch, download, auth, mods, plugins, network, ...)
├── layouts/            # Custom layouts
├── styles/             # QSS theme styles
├── resources/          # Bundled JSON data
├── Images/             # Icons and images
├── translations/       # Translations (.ts / .qm)
├── sdk/                # Plugin SDK headers
├── tests/              # Probes and test projects
├── android/            # Android project resources
└── harmony/            # HarmonyOS project notes
```

## Building

### Windows

1. Install Qt Creator and a MinGW (or MSVC) compiler
2. Open Qt Creator and import `BlockBox.pro`
3. Select a 64-bit kit, then build and run

### macOS

1. Install Qt Creator and Xcode
2. Import `BlockBox.pro` and select the Clang 64-bit kit
3. Build and run

### Linux

```bash
sudo apt-get install qtbase5-dev qt5-qmake build-essential
qmake BlockBox.pro
make
./BlockBox
```

### HarmonyOS

Install the HarmonyOS development environment and the Qt for HarmonyOS plugin, then select a HarmonyOS kit in Qt Creator to build and deploy.

## API key configuration

This repository **contains no third-party API keys**. All credentials are configured locally and are never uploaded with the code:

- **Microsoft account login**: the `client_id` in `utils/AuthManager.cpp` is a placeholder — fill in your own value locally. **Never commit real keys to the repository** (a pre-push check is configured to block them)

## Platform adaptation

Cross-platform differences are handled with conditional compilation: the `WINDOWS_OS` / `MAC_OS` / `LINUX_OS` / `HARMONY_OS` macros, implemented in `platform.cpp` and `platform.h`, covering the app data directory, config paths and platform detection.

## License

This project is released under the [MIT License](LICENSE) © Juanyun0601

## Contributing

Issues and pull requests are welcome.

---

[简体中文](README.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md) · [Русский](README.ru.md) · [Español](README.es.md)
