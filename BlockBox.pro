QT       += core gui network concurrent svg

# Android uses OpenGL ES, not OpenGL desktop
android: QT += opengl
!android: QT += opengl openglwidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# Increase compiler memory limit for moc (MSVC specific)
win32-msvc* {
    QMAKE_CXXFLAGS += /Zm500
    QMAKE_LFLAGS += /LARGEADDRESSAWARE
    # windows.h defines min/max macros that break std::min/std::max;
    # WIN32_LEAN_AND_MEAN keeps winsock.h out so winsock2.h wins cleanly.
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN
    # -lz resolves to plain z.lib; make the Qt kit lib dir part of the
    # MSVC LIB search path, like mingw default lib dir.
    LIBS += -L$$[QT_INSTALL_LIBS]
}

# Zlib for modpack zip decompression
LIBS += -lz

# Windows-specific libraries
win32: {
    LIBS += -lpdh -liphlpapi -ldwmapi
    LIBS += -lgdi32 -lole32 -lshell32 -lshlwapi -luuid -lcomdlg32
    # mingw links these by default; MSVC does not.
    LIBS += -luser32 -ladvapi32 -lws2_32 -loleaut32
    DEFINES += _WIN32_WINNT=0x0601 WINVER=0x0601
}

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    MainWindowPages.cpp \
    platform.cpp \
    layouts/FlowLayout.cpp \
    layouts/MasonryLayout.cpp \
    components/TopBar.cpp \
    components/UpdateProgressButton.cpp \
    components/TaskBar.cpp \
    components/SideBar.cpp \
    components/LaunchTaskCard.cpp \
components/CustomCheckBox.cpp \
     components/CustomRadioButton.cpp \
     components/CollapsibleSectionCard.cpp \
     components/InstanceFolderTree.cpp \
    components/LanTransferDialog.cpp \
     components/DownloadTaskCard.cpp \
     components/TaskProgressWidgets.cpp \
      components/Skin3DWidget.cpp \
      components/SubNavPanel.cpp \
     components/LoadingOverlay.cpp \
      components/BlurLoadingOverlay.cpp \
     components/AppMessageBox.cpp \
     components/AppDialogBase.cpp \
     components/AppInputDialog.cpp \
     components/AppColorDialog.cpp \
     components/AppFileDialog.cpp \
      components/ScreenshotViewer.cpp \
      components/NotificationCard.cpp \
     components/NotificationManager.cpp \
     components/NotificationHistoryDialog.cpp \
     components/InstanceAssistantWindow.cpp \
     components/GameFloatingIcon.cpp \
     components/BedrockInstanceAssistantWindow.cpp \
     components/BackgroundWidget.cpp \
     components/NewsCard.cpp \
     components/PerfMonitorCard.cpp \
     components/PerformanceDetailDialog.cpp \
     components/ModelSelectDialog.cpp \
     components/LocalModelDialog.cpp \
     components/CommandPackManagerDialog.cpp \
     components/CommandPackEditor.cpp \
     components/ResourceReferenceDialog.cpp \
     components/FavoriteFolderDialog.cpp \
     components/ContentViewSwitch.cpp \
     components/MasonryContentCard.cpp \
     components/OutlinedLabel.cpp \
     components/LocalCategoryDialog.cpp \
     components/VoxelViewWidget.cpp \
components/ProjectionBlockEditorDialog.cpp \
     components/HomeDataVisuals.cpp \
     components/FileDropOverlay.cpp \
     components/InstanceSelectDialog.cpp \
     pages/ShaderPackSettingsPage.cpp \
     utils/shader/ShaderPackParser.cpp \
    pages/HomePage.cpp \
    pages/ResourcesPage.cpp \
    pages/BedrockResourcesPage.cpp \
    pages/BedrockInstanceHomePage.cpp \
    pages/BedrockCommandAssistantPage.cpp \
    pages/SettingsPage.cpp \
    pages/settings/SettingsGeneralPage.cpp \
    pages/settings/SettingsInterfacePage.cpp \
    pages/settings/SettingsGamePage.cpp \
    pages/settings/SettingsInstancePage.cpp \
    pages/settings/SettingsJavaPage.cpp \
    pages/settings/SettingsAdvancedPage.cpp \
     pages/settings/SettingsKeyBindPage.cpp \
     pages/settings/SettingsSystemInfoPage.cpp \
     pages/settings/SettingsBedrockGamePage.cpp \
     pages/settings/BedrockInstanceSettingsPage.cpp \
     pages/settings/SettingsAiAssistantPage.cpp \
     pages/InstanceSelectPage.cpp \
    pages/LanTransferPage.cpp \
    pages/AccountManagePage.cpp \
    pages/AiChatPage.cpp \
     pages/InstanceManagePage.cpp \
     pages/InstanceModsPage.cpp \
     pages/InstanceOverviewPage.cpp \
     pages/InstanceLogPage.cpp \
         pages/InstanceFilePage.cpp \
     pages/InstanceResourcesPage.cpp \
     pages/SaveSettingsPage.cpp \
     pages/InstanceHomePage.cpp \
     pages/InstanceJavaPage.cpp \
     pages/InstanceJavaDownloadPage.cpp \
     pages/ProjectionManagePage.cpp \
     pages/ProjectionEditPage.cpp \
     pages/ServerManagePage.cpp \
    pages/LocalModDetailPage.cpp \
    pages/settings/InstanceGameSettingsPage.cpp \
    pages/MicrosoftLoginDialog.cpp \
    pages/LaunchDetailsPage.cpp \
    pages/InstallInstancePage.cpp \
    pages/LoaderDetailPage.cpp \
    pages/ForgeVersionListPage.cpp \
    pages/FabricVersionListPage.cpp \
    pages/ColorPickerPage.cpp \
    pages/TaskListPage.cpp \
    pages/TaskDetailPage.cpp \
    utils/SettingsManager.cpp \
    utils/SkinDownloader.cpp \
    utils/SpeedCalculator.cpp \
    utils/SystemInfo.cpp \
    utils/UpdateChecker.cpp \
    utils/HarmonyBridge.cpp \
    utils/LanguageManager.cpp \
    utils/LanTransfer.cpp \
    utils/tunnel/TunnelManager.cpp \
    utils/tunnel/EasyTierEngine.cpp \
    utils/FileExplorer.cpp \
    utils/FileReuse.cpp \
    utils/AndroidBridge.cpp \
    utils/AuthManager.cpp \
    utils/auth/AuthManagerMicrosoft.cpp \
    utils/BackgroundManager.cpp \
    utils/BingWallpaperManager.cpp \
    utils/ThemeManager.cpp \
    utils/GameLauncher.cpp \
    utils/BedrockLauncher.cpp \
    utils/launcher/GameLauncherJava.cpp \
    utils/launcher/GameLauncherPrepare.cpp \
    utils/launcher/GameLauncherCommand.cpp \
    utils/ErrorAnalyzer.cpp \
    utils/ResourceManager.cpp \
    utils/PerformanceMonitor.cpp \
    utils/LoaderCompatibility.cpp \
    utils/MemoryAllocator.cpp \
    utils/VersionDownloader.cpp \
    utils/download/VersionDownloaderAssets.cpp \
    utils/download/DownloadEngine.cpp \
    utils/DownloadTaskManager.cpp \
    utils/DownloadUtils.cpp \
    utils/StartupProgressWidget.cpp \
    utils/AsyncInitializer.cpp \
    utils/LowConfigMode.cpp \
    utils/JarUtils.cpp \
    utils/forge/ForgeDownloader.cpp \
    utils/forge/ForgeInstaller.cpp \
    utils/forge/ForgeNewInstallTask.cpp \
    utils/forge/ForgeProfile.cpp \
    utils/fabric/FabricInstaller.cpp \
    utils/optifine/OptiFineInstaller.cpp \
    utils/ManifestCache.cpp \
    utils/IconHelper.cpp \
     utils/NeoForgeInstaller.cpp \
     utils/LiteLoaderInstaller.cpp \
     utils/CleanroomInstaller.cpp \
     utils/OptiFabricInstaller.cpp \
    pages/NeoForgeVersionListPage.cpp \
     pages/ModDetailPage.cpp \
     pages/ContentDetailPage.cpp \
     pages/ModDownloadPage.cpp \
     pages/ContentDownloadPage.cpp \
     pages/ModListPage.cpp \
     pages/ContentListPage.cpp \
     pages/ModpackImportPage.cpp \
     pages/ModpackExportPage.cpp \
     pages/SearchPage.cpp \
     pages/BedrockSearchPage.cpp \
     pages/JavaDownloadPage.cpp \
     pages/FavoritesPage.cpp \
      pages/BedrockDownloadPage.cpp \
     pages/BedrockVersionDetailPage.cpp \
       utils/bedrock/BedrockVersionService.cpp \ 
       utils/bedrock/BedrockInstanceManager.cpp \
       pages/BedrockInstanceSelectPage.cpp \
       pages/BedrockContentPage.cpp \
       pages/BedrockContentDetailPage.cpp \
        utils/bedrock/BedrockContentInstaller.cpp \
     pages/OnboardingWizard.cpp \
    utils/JavaScanWorker.cpp \
    utils/LauncherImporter.cpp \
    utils/JavaDownloader.cpp \
    utils/mod/ModData.cpp \
     utils/mod/CurseForgeAPI.cpp \
     utils/mod/ModrinthAPI.cpp \
      utils/WebpLoader.cpp \
      utils/ThumbnailProvider.cpp \
    utils/FavoritesManager.cpp \
    utils/LocalCategoryManager.cpp \
    utils/mod/MCModAPI.cpp \
    utils/mod/ModScanner.cpp \
    utils/mod/ModNameFetcher.cpp \
    utils/mod/ModLinkResolver.cpp \
    utils/mod/MurmurHash2.cpp \
    utils/modpack/ModpackDetector.cpp \
    utils/modpack/ModpackImporter.cpp \
    utils/modpack/ModpackFileAdviser.cpp \
    utils/modpack/ModpackExporter.cpp \
    utils/content/ContentDownloader.cpp \
     utils/ClipboardMonitor.cpp \
     utils/FileImportRouter.cpp \
     utils/AiService.cpp \
    utils/SystemTools.cpp \
    utils/HardwareMonitor.cpp \
    utils/MultiThreadDownloader.cpp \
    utils/LocalModelManager.cpp \
    utils/GithubAccelerator.cpp \
    utils/PageTransitionAnimator.cpp \
    utils/ContentAnimator.cpp \
    utils/CommandAssistant/LevelDatReader.cpp \
    utils/CommandAssistant/LitematicReader.cpp \
    utils/Saves/LevelDatEditor.cpp \
    utils/Schematic/NbtCodec.cpp \
    utils/Schematic/LitematicEditor.cpp \
    utils/Schematic/SchematicDocument.cpp \
    utils/Schematic/InstanceTextureLoader.cpp \
    utils/Schematic/BlockShapeLoader.cpp \
    utils/CommandAssistant/CommandDatabase.cpp \
    utils/CommandAssistant/CommandTranslator.cpp \
    utils/CommandAssistant/CommandCompleter.cpp \
    utils/CommandAssistant/GameDetector.cpp \
    utils/CommandAssistant/GameRegistry.cpp \
    utils/CommandAssistant/BlockRegistry.cpp \
    utils/CommandAssistant/CommandPack.cpp \
    utils/Terracotta/TerracottaClient.cpp \
    utils/Hongshi/HongshiClient.cpp \
    utils/GravityCone/GravityConeClient.cpp \
    pages/MultiplayerPage.cpp \
    pages/BedrockMultiplayerPage.cpp \
    utils/SkinEditorDocument.cpp \
    components/SkinTextureCanvas.cpp \
    components/SkinUVOverlay.cpp \
    pages/SkinEditorPage.cpp \
    utils/plugin/PluginZip.cpp \
    utils/plugin/PluginManager.cpp \
    utils/plugin/PluginSafetyGuard.cpp \
    utils/plugin/ServerStatusChecker.cpp \
    components/CreatePluginDialog.cpp \
    components/PluginSettingsDialog.cpp \
    pages/PluginPage.cpp \
    utils/skill/SkillManager.cpp \
    components/SkillManagerDialog.cpp \
    components/PluginRunnerDialog.cpp \
    utils/translation/TranslationService.cpp \
    components/TranslationSettingsDialog.cpp

HEADERS += \
    platform.h \
    mainwindow.h \
    layouts/FlowLayout.h \
    layouts/MasonryLayout.h \
    components/TopBar.h \
    components/UpdateProgressButton.h \
    components/TaskBar.h \
    components/SideBar.h \
    components/LaunchTaskCard.h \
components/CustomCheckBox.h \
     components/CustomRadioButton.h \
     components/CollapsibleSectionCard.h \
     components/InstanceFolderTree.h \
    components/LanTransferDialog.h \
     components/DownloadTaskCard.h \
     components/TaskProgressWidgets.h \
      components/Skin3DWidget.h \
     components/SubNavPanel.h \
     components/LoadingOverlay.h \
       components/BlurLoadingOverlay.h \
       components/AppMessageBox.h \
       components/AppDialogBase.h \
       components/AppInputDialog.h \
       components/AppColorDialog.h \
       components/AppFileDialog.h \
      components/ScreenshotViewer.h \
       components/NotificationCard.h \
     components/NotificationManager.h \
     components/NotificationHistoryDialog.h \
       components/BackgroundWidget.h \
       components/InstanceAssistantWindow.h \
       components/GameFloatingIcon.h \
       components/BedrockInstanceAssistantWindow.h \
       components/NewsCard.h \
      components/PerfMonitorCard.h \
      components/PerformanceDetailDialog.h \
      components/ModelSelectDialog.h \
      components/LocalModelDialog.h \
      components/CommandPackManagerDialog.h \
      components/CommandPackEditor.h \
       components/ResourceReferenceDialog.h \
        components/FavoriteFolderDialog.h \
         components/ContentViewSwitch.h \
         components/MasonryContentCard.h \
         components/OutlinedLabel.h \
        components/LocalCategoryDialog.h \
        components/VoxelViewWidget.h \
components/ProjectionBlockEditorDialog.h \
         components/HomeDataVisuals.h \
         components/FileDropOverlay.h \
         components/InstanceSelectDialog.h \
        pages/ShaderPackSettingsPage.h \
       utils/shader/ShaderPackParser.h \
    pages/HomePage.h \
    pages/ResourcesPage.h \
    pages/BedrockResourcesPage.h \
    pages/BedrockInstanceHomePage.h \
    pages/BedrockCommandAssistantPage.h \
    pages/SettingsPage.h \
    pages/InstanceSelectPage.h \
    pages/LanTransferPage.h \
    pages/AccountManagePage.h \
    pages/AiChatPage.h \
    pages/MicrosoftLoginDialog.h \
    pages/LaunchDetailsPage.h \
    pages/InstallInstancePage.h \
    pages/LoaderDetailPage.h \
    pages/ForgeVersionListPage.h \
    pages/FabricVersionListPage.h \
    pages/ColorPickerPage.h \
    pages/TaskListPage.h \
    pages/TaskDetailPage.h \
    utils/SettingsManager.h \
    utils/McimHelper.h \
    utils/SkinDownloader.h \
    utils/SpeedCalculator.h \
    utils/SystemInfo.h \
    utils/UpdateChecker.h \
    utils/HarmonyBridge.h \
    utils/LanguageManager.h \
    utils/LanTransfer.h \
    utils/tunnel/TunnelManager.h \
    utils/tunnel/TunnelEngine.h \
    utils/tunnel/EasyTierEngine.h \
    utils/FileExplorer.h \
    utils/FileReuse.h \
    utils/AndroidBridge.h \
    utils/AuthManager.h \
    utils/BackgroundManager.h \
    utils/BingWallpaperManager.h \
    utils/ThemeManager.h \
    utils/GameLauncher.h \
    utils/BedrockLauncher.h \
    utils/ErrorAnalyzer.h \
    utils/ResourceManager.h \
    utils/PerformanceMonitor.h \
    utils/LoaderCompatibility.h \
    utils/MemoryAllocator.h \
    utils/VersionDownloader.h \
    utils/DownloadTaskManager.h \
    utils/DownloadUtils.h \
    utils/download/DownloadEngine.h \
    utils/StartupProgressWidget.h \
    utils/AsyncInitializer.h \
    utils/LowConfigMode.h \
    utils/JarUtils.h \
    utils/ManifestCache.h \
    utils/FavoritesManager.h \
    utils/LocalCategoryManager.h \
    utils/fabric/FabricInstaller.h \
    utils/forge/ForgeDownloader.h \
    utils/forge/ForgeInstaller.h \
    utils/forge/ForgeNewInstallTask.h \
    utils/forge/ForgeProfile.h \
    utils/optifine/OptiFineInstaller.h \
     utils/NeoForgeInstaller.h \
     utils/LiteLoaderInstaller.h \
     utils/CleanroomInstaller.h \
     utils/OptiFabricInstaller.h \
    pages/NeoForgeVersionListPage.h \
     pages/ModDetailPage.h \
     pages/ContentDetailPage.h \
     pages/ModDownloadPage.h \
     pages/ContentDownloadPage.h \
     pages/ModListPage.h \
     pages/ContentListPage.h \
     pages/ModpackImportPage.h \
     pages/ModpackExportPage.h \
     pages/SearchPage.h \
     pages/BedrockSearchPage.h \
     pages/JavaDownloadPage.h \
     pages/FavoritesPage.h \
      pages/BedrockDownloadPage.h \
     pages/BedrockVersionDetailPage.h \
       utils/bedrock/BedrockVersionService.h \  
       utils/bedrock/BedrockInstanceManager.h \
       pages/BedrockInstanceSelectPage.h \
       pages/BedrockContentPage.h \
       pages/BedrockContentDetailPage.h \
        utils/bedrock/BedrockContentInstaller.h \
     pages/OnboardingWizard.h \
    pages/InstanceManagePage.h \
    pages/InstanceModsPage.h \
    pages/InstanceOverviewPage.h \
    pages/InstanceLogPage.h \
         pages/InstanceFilePage.h \
     pages/InstanceResourcesPage.h \
     pages/SaveSettingsPage.h \
     pages/InstanceHomePage.h \
     pages/InstanceJavaPage.h \
     pages/InstanceJavaDownloadPage.h \
pages/ProjectionManagePage.h \
     pages/ProjectionEditPage.h \
     pages/ServerManagePage.h \
    pages/LocalModDetailPage.h \
    pages/settings/InstanceGameSettingsPage.h \
    pages/settings/SettingsKeyBindPage.h \
    pages/settings/SettingsBedrockGamePage.h \
    pages/settings/BedrockInstanceSettingsPage.h \
      utils/JavaScanWorker.h \
    utils/LauncherImporter.h \
    utils/JavaDownloader.h \
     utils/mod/ModData.h \
      utils/WebpLoader.h \
      utils/ThumbnailProvider.h \
      utils/mod/CurseForgeAPI.h \
    utils/mod/ModrinthAPI.h \
    utils/mod/MCModAPI.h \
    utils/mod/ModScanner.h \
    utils/mod/ModNameFetcher.h \
    utils/mod/ModLinkResolver.h \
    utils/mod/MurmurHash2.h \
    utils/modpack/ModpackInfo.h \
    utils/modpack/ModpackDetector.h \
    utils/modpack/ModpackImporter.h \
    utils/modpack/ModpackFileAdviser.h \
    utils/modpack/ModpackExporter.h \
    utils/content/ContentData.h \
    utils/content/ContentDownloader.h \
     utils/ClipboardMonitor.h \
     utils/FileImportRouter.h \
     utils/AiService.h \
    utils/SystemTools.h \
    utils/HardwareMonitor.h \
    utils/MultiThreadDownloader.h \
    utils/LocalModelManager.h \
    utils/GithubAccelerator.h \
    utils/PageTransitionAnimator.h \
    utils/ContentAnimator.h \
    utils/CommandAssistant/LevelDatReader.h \
    utils/CommandAssistant/LitematicReader.h \
    utils/Schematic/NbtCodec.h \
    utils/Schematic/LitematicEditor.h \
    utils/Schematic/SchematicDocument.h \
    utils/Schematic/InstanceTextureLoader.h \
    utils/Schematic/BlockShapeLoader.h \
    utils/Saves/LevelDatEditor.h \
    utils/CommandAssistant/CommandTranslator.h \
    utils/CommandAssistant/CommandCompleter.h \
    utils/CommandAssistant/GameDetector.h \
    utils/CommandAssistant/GameRegistry.h \
    utils/CommandAssistant/BlockRegistry.h \
    utils/CommandAssistant/CommandPack.h \
    utils/Terracotta/TerracottaClient.h \
    utils/Hongshi/HongshiClient.h \
    utils/GravityCone/GravityConeClient.h \
    pages/MultiplayerPage.h \
    pages/BedrockMultiplayerPage.h \
    utils/SkinEditorDocument.h \
    components/SkinTextureCanvas.h \
    components/SkinUVOverlay.h \
    pages/SkinEditorPage.h \
    utils/plugin/PluginInfo.h \
    utils/plugin/PluginZip.h \
    utils/plugin/PluginManager.h \
    utils/plugin/PluginSafetyGuard.h \
    utils/plugin/ServerStatusChecker.h \
    components/CreatePluginDialog.h \
    components/PluginSettingsDialog.h \
    pages/PluginPage.h \
    utils/skill/SkillInfo.h \
    utils/skill/SkillManager.h \
    components/SkillManagerDialog.h \
    components/PluginRunnerDialog.h \
    utils/translation/TranslationService.h \
    components/TranslationSettingsDialog.h

FORMS += \
    mainwindow.ui

RESOURCES += \
    resources.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
win32: target.path = $$(APPDATA)/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# Include styles directory
INCLUDEPATH += $$PWD/styles

# Translation files
TRANSLATIONS += \
    translations/BlockBox_en.ts \
    translations/BlockBox_zh.ts \
    translations/BlockBox_zh_Hant.ts \
    translations/BlockBox_es.ts

# Auto-generate .qm files from .ts files on each build
lrelease.name = Generate .qm from .ts
lrelease.input = TRANSLATIONS
lrelease.output = ${QMAKE_FILE_PATH}/${QMAKE_FILE_BASE}.qm
lrelease.commands = lrelease ${QMAKE_FILE_IN} -qm ${QMAKE_FILE_OUT}
lrelease.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += lrelease

# Multi-platform support configurations
# HarmonyOS support
contains(QT_ARCH, arm64-v8a): {
    TARGET = BlockBoxHarmony
    DEFINES += HARMONY_OS
    # 更新检查鸿蒙桥接:QtCore 私有 JS 线程接口 + NAPI
    QT += core-private
    LIBS += -lace_napi.z
    # node-addon-api 需显式开启 C++ 异常模式(qnapi_p.h 依赖 Error::what)
    DEFINES += NODE_ADDON_API_CPP_EXCEPTIONS
    # qcore_ohos_p.h -> qnapi_p.h 依赖 <napi.h>(qt-ohos 源码树内的 node-addon-api 头),
    # 缺省按与本工程同级的 qt-ohos 源码树定位;其他布局用 qmake 传入:
    #   qmake ... "INCLUDEPATH+=<qt-ohos>/script/work/qt6/qtbase/src/3rdparty/node-addon-api"
    NODE_ADDON_API_DIR = $$clean_path($$_PRO_FILE_PWD_/../qt-ohos/script/work/qt6/qtbase/src/3rdparty/node-addon-api)
    EXISTS($$NODE_ADDON_API_DIR) {
        INCLUDEPATH += $$NODE_ADDON_API_DIR
    } else {
        warning("node-addon-api headers not found; set INCLUDEPATH to qt-ohos node-addon-api for harmony build")
    }
}

# Windows specific configurations
win32: {
    TARGET = BlockBoxWindows
    DEFINES += WINDOWS_OS
    RC_ICONS = Images/icon.ico
    # Kill any running instance before linking to avoid permission error
    QMAKE_PRE_LINK = -taskkill /f /im $${TARGET}.exe 2>nul
}

# macOS specific configurations
macx: {
    TARGET = BlockBoxMac
    DEFINES += MAC_OS
    # Add macOS specific resources if needed
    QMAKE_INFO_PLIST = Info.plist
}

# Linux specific configurations
linux: {
    TARGET = BlockBoxLinux
    DEFINES += LINUX_OS
    # Add Linux specific resources if needed
}

# Android specific configurations
android: {
    TARGET = BlockBoxAndroid
    DEFINES += ANDROID_OS

    # Android SDK/NDK settings
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android

    # Minimum SDK version
    ANDROID_MIN_SDK_VERSION = 24
    ANDROID_TARGET_SDK_VERSION = 34

    # Android ABIs
    ANDROID_ABIS = armeabi-v7a arm64-v8a x86 x86_64

    # Android permissions (added via AndroidManifest.xml)
    # INTERNET, WRITE_EXTERNAL_STORAGE, READ_EXTERNAL_STORAGE, etc.

    # Disable Windows-specific code on Android
    DEFINES -= WINDOWS_OS

    # Use OpenGL ES instead of desktop OpenGL
    QT -= openglwidgets
    DEFINES += QT_OPENGL_ES_2

    # Android-specific libraries
    LIBS += -llog -landroid
}



