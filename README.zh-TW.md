# 電腦方塊盒子 BlockBox

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

[简体中文](README.md) | **繁體中文** | [English](README.en.md) | [日本語](README.ja.md) | [Русский](README.ru.md) | [Español](README.es.md)

跨平台 Minecraft 啟動器（Java 版 / 基岩版），以 Qt 與 C++ 開發，全程 AI 加持。一個盒子裝下整個方塊世界。

## 功能特色

- **實例管理**：Java 版 / 基岩版實例的建立、匯入、多版本切換與啟動，支援從其他啟動器遷移
- **內容下載**：模組、整合包、資源包、光影，整合 CurseForge 與 Modrinth 來源
- **AI 助手**：AI 對話、崩潰日誌智慧診斷、指令助手（自然語言轉遊戲指令）
- **外掛系統**：內建外掛 SDK 與外掛管理，支援本機擴充（見 `sdk/`）
- **面板編輯**：內建面板編輯器與 3D 預覽
- **連線與網路**：區域網路連線傳輸、通道組網
- **存檔管理**：世界存檔備份與還原
- **多語言**：簡體中文、繁體中文、英文、西班牙文、日文、俄文

## 支援平台

- Windows
- macOS
- Linux
- HarmonyOS
- Android

## 技術架構

- **框架**: Qt 5.15+ / Qt 6.x
- **語言**: C++
- **介面**: Qt Widgets + QSS 主題
- **建置**: qmake

## 專案結構

```
├── main.cpp            # 程式進入點
├── MainWindow.cpp      # 主視窗
├── MainWindowPages.cpp # 頁面組裝
├── platform.cpp/.h     # 平台適配
├── BlockBox.pro        # qmake 專案檔
├── resources.qrc       # Qt 資源清單
├── components/         # 介面元件（對話框、側邊欄、卡片等）
├── pages/              # 各功能頁面（含 pages/settings/ 設定頁）
├── utils/              # 業務邏輯（啟動、下載、驗證、模組、外掛、網路等）
├── layouts/            # 自訂版面配置
├── styles/             # QSS 樣式主題
├── resources/          # 內建 JSON 資料
├── Images/             # 圖示與圖片資源
├── translations/       # 多語言翻譯（.ts / .qm）
├── sdk/                # 外掛 SDK 標頭檔
├── tests/              # 測試程式與測試專案
├── android/            # Android 專案資源
└── harmony/            # HarmonyOS 專案說明
```

## 建置說明

### Windows

1. 安裝 Qt Creator 與 MinGW（或 MSVC）編譯器
2. 開啟 Qt Creator，匯入 `BlockBox.pro`
3. 選擇 64-bit 建置套件，建置並執行

### macOS

1. 安裝 Qt Creator 與 Xcode
2. 匯入 `BlockBox.pro`，選擇 Clang 64-bit 套件
3. 建置並執行

### Linux

```bash
sudo apt-get install qtbase5-dev qt5-qmake build-essential
qmake BlockBox.pro
make
./BlockBox
```

### HarmonyOS

安裝鴻蒙開發環境與 Qt for HarmonyOS 外掛後，在 Qt Creator 中選擇 HarmonyOS 建置套件建置部署。

## API 金鑰設定

本倉庫**不包含任何第三方 API 金鑰**，憑證皆在本機設定，不會隨程式碼上傳：

- **微軟帳號登入**：`utils/AuthManager.cpp` 中的 `client_id` 為佔位符，請在本機填入你自己的值。**請勿將真實金鑰提交到倉庫**（倉庫已設定 pre-push 檢查用於攔截）

## 平台適配

跨平台差異透過條件編譯處理：`WINDOWS_OS` / `MAC_OS` / `LINUX_OS` / `HARMONY_OS` 巨集，具體實作在 `platform.cpp` 與 `platform.h`，包含應用程式資料目錄、設定路徑與平台偵測。

## 授權條款

本專案採用 [MIT 授權](LICENSE) © Juanyun0601

## 貢獻

歡迎提交 Issue 與 Pull Request。

---

[简体中文](README.md) · [English](README.en.md) · [日本語](README.ja.md) · [Русский](README.ru.md) · [Español](README.es.md)
