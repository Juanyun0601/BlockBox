# BlockBox ランチャー

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

[简体中文](README.md) | [繁體中文](README.zh-TW.md) | [English](README.en.md) | **日本語** | [Русский](README.ru.md) | [Español](README.es.md)

Qt と C++ で開発されたクロスプラットフォームの Minecraft ランチャー（Java 版 / Bedrock 版）です。AI を全面的に活用しています。ひとつの箱にブロックの世界をまるごと。

## 機能

- **インスタンス管理**：Java 版 / Bedrock 版インスタンスの作成・インポート・複数バージョンの切り替えと起動、ほかのランチャーからの移行にも対応
- **コンテンツのダウンロード**：MOD、MOD パック、リソースパック、シェーダーを CurseForge と Modrinth からまとめて取得
- **AI アシスタント**：AI チャット、クラッシュログの自動診断、コマンドアシスタント（自然言語からゲーム内コマンドへ）
- **プラグインシステム**：プラグイン SDK とプラグイン管理を内蔵、ローカル拡張に対応（`sdk/` を参照）
- **スキン編集**：スキンエディタと 3D プレビューを内蔵
- **マルチプレイとネットワーク**：LAN でのワールド転送、トンネル接続
- **セーブデータ管理**：ワールドのバックアップと復元
- **多言語対応**：簡体字中国語、繁体字中国語、英語、スペイン語、日本語、ロシア語

## 対応プラットフォーム

- Windows
- macOS
- Linux
- HarmonyOS
- Android

## 技術スタック

- **フレームワーク**: Qt 5.15+ / Qt 6.x
- **言語**: C++
- **UI**: Qt Widgets + QSS テーマ
- **ビルド**: qmake

## プロジェクト構成

```
├── main.cpp            # プログラムのエントリーポイント
├── MainWindow.cpp      # メインウィンドウ
├── MainWindowPages.cpp # ページの組み立て
├── platform.cpp/.h     # プラットフォーム対応
├── BlockBox.pro        # qmake プロジェクトファイル
├── resources.qrc       # Qt リソースマニフェスト
├── components/         # UI コンポーネント（ダイアログ、サイドバー、カードなど）
├── pages/              # 各機能ページ（pages/settings/ 設定ページを含む）
├── utils/              # ビジネスロジック（起動、ダウンロード、認証、MOD、プラグイン、ネットワークなど）
├── layouts/            # カスタムレイアウト
├── styles/             # QSS スタイルテーマ
├── resources/          # 内蔵 JSON データ
├── Images/             # アイコンと画像リソース
├── translations/       # 翻訳ファイル（.ts / .qm）
├── sdk/                # プラグイン SDK ヘッダー
├── tests/              # プローブとテストプロジェクト
├── android/            # Android プロジェクトのリソース
└── harmony/            # HarmonyOS プロジェクトの説明
```

## ビルド方法

### Windows

1. Qt Creator と MinGW（または MSVC）コンパイラをインストール
2. Qt Creator で `BlockBox.pro` をインポート
3. 64-bit のキットを選択してビルド・実行

### macOS

1. Qt Creator と Xcode をインストール
2. `BlockBox.pro` をインポートし、Clang 64-bit キットを選択
3. ビルド・実行

### Linux

```bash
sudo apt-get install qtbase5-dev qt5-qmake build-essential
qmake BlockBox.pro
make
./BlockBox
```

### HarmonyOS

HarmonyOS の開発環境と Qt for HarmonyOS プラグインをインストールしたうえで、Qt Creator で HarmonyOS キットを選んでビルド・配置します。

## API キーの設定

このリポジトリには**サードパーティの API キーは一切含まれていません**。認証情報はすべてローカルで設定し、コードと一緒にアップロードされることはありません：

- **Microsoft アカウントのログイン**：`utils/AuthManager.cpp` の `client_id` はプレースホルダーです。ローカルでご自身の値を入力してください。**実際のキーをリポジトリにコミットしないでください**（コミットを防ぐ pre-push チェックが設定されています）

## プラットフォーム対応

プラットフォームごとの差異は条件付きコンパイルで処理しています：`WINDOWS_OS` / `MAC_OS` / `LINUX_OS` / `HARMONY_OS` マクロ。実装は `platform.cpp` と `platform.h` にあり、アプリのデータディレクトリ、設定パス、プラットフォーム判定を含みます。

## ライセンス

本プロジェクトは [MIT ライセンス](LICENSE) © Juanyun0601 の下で公開されています。

## コントリビュート

Issue と Pull Request を歓迎します。

---

[简体中文](README.md) · [繁體中文](README.zh-TW.md) · [English](README.en.md) · [Русский](README.ru.md) · [Español](README.es.md)
