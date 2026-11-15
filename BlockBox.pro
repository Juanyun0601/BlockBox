QT       += core gui network concurrent svg

# Android/OHOS use OpenGL ES; QOpenGLWidget (QtOpenGLWidgets) also works over ES
android: QT += opengl openglwidgets
!android: QT += opengl openglwidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# ---------------------------------------------------------------- 构建速度优化
# 预编译头：把几百个文件都要用的 Qt 头只解析一次，单文件编译变快（详见 pch.h）。
# 只对 gcc / clang 工具链启用：MSVC 的预编译头要求每个源文件第一行就包含它，
# 本工程没这么做；Android/OHOS 走各自工具链打包，也保持原样不受影响。
!android:!ohos:gcc|clang {
    CONFIG += precompile_header
    PRECOMPILED_HEADER = pch.h
    HEADERS += pch.h
}

# -pipe：编译器之间用管道传中间结果，省掉临时文件读写（gcc / clang 支持）。
gcc|clang {
    QMAKE_CFLAGS += -pipe
    QMAKE_CXXFLAGS += -pipe
}
# ------------------------------------------------------------ 构建速度优化结束

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
    components/ShareToFriendDialog.cpp \
    components/ThreadTipDialog.cpp \
    components/CreateGroupDialog.cpp \
    components/ChatForwardDialog.cpp \
    components/ChatBookmarkDialog.cpp \
     components/DownloadTaskCard.cpp \
     components/TaskProgressWidgets.cpp \
components/ProgressWidget.cpp \
components/ProgressDialog.cpp \
      components/Skin3DWidget.cpp \
      components/SubNavPanel.cpp \
     components/LoadingOverlay.cpp \
      components/BlurLoadingOverlay.cpp \
     components/AppMessageBox.cpp \
     components/AppDialogBase.cpp \
     components/LobbyPublishDialog.cpp \
     components/LobbyJoinPrepDialog.cpp \
     components/WorldConvertDialog.cpp \
     components/HighRiskConfirmDialog.cpp \
     components/QuizDialog.cpp \
     components/QuizReminder.cpp \
     components/HumanCheckDialog.cpp \
     components/AppInputDialog.cpp \
     components/AiComposeDialog.cpp \
     components/AppColorDialog.cpp \
     components/OnlineResourceDialog.cpp \
      components/ScreenshotViewer.cpp \
      components/NotificationCard.cpp \
     components/NotificationManager.cpp \
     components/NotificationHistoryDialog.cpp \
     components/FileOpStepsDialog.cpp \
     components/InstanceAssistantWindow.cpp \
     components/GameFloatingIcon.cpp \
     components/BedrockInstanceAssistantWindow.cpp \
     components/BackgroundWidget.cpp \
     components/PanoramaSkyRenderer.cpp \
     components/NewsCard.cpp \
     components/HomeAnnouncementBar.cpp \
     components/NewsDetailDialog.cpp \
     components/PerfMonitorCard.cpp \
     components/PerformanceDetailDialog.cpp \
     components/ModelSelectDialog.cpp \
     components/LocalModelDialog.cpp \
     components/CommandPackManagerDialog.cpp \
     components/CommandPackEditor.cpp \
     components/ResourceReferenceDialog.cpp \
     components/StructureDetailDialog.cpp \
     components/FavoriteFolderDialog.cpp \
     components/ContentCategoryFilterDialog.cpp \
     components/ContentViewSwitch.cpp \
     components/ModRelationDialog.cpp \
     components/SourceSwitchBar.cpp \
     components/MasonryContentCard.cpp \
     components/CardSelectionOverlay.cpp \
     components/TranslateSwitchCard.cpp \
     components/AutoHeightTextBrowser.cpp \
     components/CardListTranslator.cpp \
     components/TranslatePayloadCodec.cpp \
     components/OutlinedLabel.cpp \
     components/MarkdownOutlinedLabel.cpp \
     components/TextOutlineLayer.cpp \
     components/OutlinedTextEdit.cpp \
     components/LocalCategoryDialog.cpp \
     components/VoxelViewWidget.cpp \
    components/WorldMap3DView.cpp \
components/ProjectionBlockEditorDialog.cpp \
components/OrthoViewsPreviewDialog.cpp \
     components/HomeDataVisuals.cpp \
     components/HomeFriendsPanel.cpp \
     components/HomeHotThreadsPanel.cpp \
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
     pages/settings/SettingsProjectionEditorPage.cpp \
      pages/settings/SettingsKeyBindPage.cpp \
     pages/settings/SettingsSystemInfoPage.cpp \
     pages/settings/SettingsBedrockGamePage.cpp \
     pages/settings/BedrockInstanceSettingsPage.cpp \
     pages/settings/SettingsAiAssistantPage.cpp \
     pages/settings/SettingsAboutPage.cpp \
     pages/InstanceSelectPage.cpp \
    pages/LanTransferPage.cpp \
    pages/AddAccountPage.cpp \
    pages/ForumAccountPage.cpp \
    pages/AiChatPage.cpp \
     pages/InstanceManagePage.cpp \
     pages/InstanceModsPage.cpp \
     pages/InstanceOverviewPage.cpp \
     pages/InstanceLogPage.cpp \
         pages/InstanceFilePage.cpp \
     pages/InstanceRecordingsPage.cpp \
     pages/InstanceResourcesPage.cpp \
     pages/SaveSettingsPage.cpp \
     pages/WorldMapPage.cpp \
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
    utils/ForumAccountService.cpp \
    utils/HomeCarouselStore.cpp \
    utils/QuizBank.cpp \
    utils/TriviaBank.cpp \
    utils/SkinDownloader.cpp \
    utils/SpeedCalculator.cpp \
    utils/SystemInfo.cpp \
    utils/UpdateChecker.cpp \
    utils/news/NewsFeed.cpp \
    utils/news/AnnouncementFeed.cpp \
    utils/RuntimeArchive.cpp \
    utils/RuntimeDependencyManager.cpp \
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
    utils/StyleKit.cpp \
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
    utils/net/Task.cpp \
    utils/net/NetJob.cpp \
    utils/net/Download.cpp \
    utils/net/FileSink.cpp \
    utils/net/MetaCacheSink.cpp \
    utils/net/HttpMetaCache.cpp \
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
    utils/forge/LegacyForgeInstaller.cpp \
    utils/forge/LoaderVersionDir.cpp \
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
    utils/LauncherLogger.cpp \
    utils/FileOpStepLog.cpp \
    utils/JavaDownloader.cpp \
    utils/mod/ModData.cpp \
     utils/mod/CurseForgeAPI.cpp \
     utils/mod/ModrinthAPI.cpp \
      utils/WebpLoader.cpp \
      utils/ThumbnailProvider.cpp \
    utils/FavoritesManager.cpp \
    utils/ContentFavoriteSync.cpp \
    utils/LocalCategoryManager.cpp \
    utils/LocalFavoritesManager.cpp \
    utils/mod/MCModAPI.cpp \
    utils/mod/SearchQueryResolver.cpp \
    utils/mod/ModScanner.cpp \
    utils/mod/ModNameFetcher.cpp \
    utils/mod/ModLinkResolver.cpp \
    utils/mod/MurmurHash2.cpp \
    utils/modpack/ModpackDetector.cpp \
    utils/modpack/ModpackImporter.cpp \
    utils/modpack/ModpackFileAdviser.cpp \
    utils/modpack/ModpackExporter.cpp \
    utils/content/ContentDownloader.cpp \
    utils/content/ContentListCache.cpp \
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
    utils/ServersDatIO.cpp \
    utils/McServerPinger.cpp \
    utils/WorldBackupManager.cpp \
    utils/WorldMap/WorldMapRenderer.cpp \
    utils/WorldMap/SeedTerrainRenderer.cpp \
    utils/WorldMap/TreePredictor.cpp \
    utils/WorldMap/TreeGen.cpp \
    utils/WorldMap/MushroomPredictor.cpp \
    utils/WorldMap/CavePredictor.cpp \
    utils/WorldMap/StructureFinder.cpp \
    utils/WorldMap/StructureIcons.cpp \
    utils/WorldMap/StructureLoot.cpp \
utils/WorldMap/StructureModelBuilder.cpp \
utils/WorldMap/StructureTemplates.cpp \
utils/WorldMap/StrongholdModel.cpp \
utils/WorldMap/FortressModel.cpp \
utils/WorldMap/MineshaftModel.cpp \
    utils/WorldMap/McRandom.cpp \
    utils/WorldMap/BiomeCatalog.cpp \
    utils/WorldMap/BiomeName.cpp \
    utils/WorldMap/OreFinder.cpp \
    utils/WorldConvert/WorldConverter.cpp \
    utils/Schematic/NbtCodec.cpp \
    utils/Schematic/LitematicEditor.cpp \
    utils/Schematic/SchematicDocument.cpp \
    utils/Schematic/SchematicCoverRenderer.cpp \
    utils/Schematic/ProjectionEditorSettings.cpp \
    utils/Schematic/InstanceTextureLoader.cpp \
    utils/Schematic/BlockShapeLoader.cpp \
    utils/Schematic/InstanceBlockNames.cpp \
    utils/Schematic/SchematicOrthoViewRenderer.cpp \
    utils/CommandAssistant/CommandDatabase.cpp \
    utils/CommandAssistant/CommandTranslator.cpp \
    utils/CommandAssistant/CommandCompleter.cpp \
    utils/CommandAssistant/GameDetector.cpp \
    utils/CommandAssistant/GameRegistry.cpp \
    utils/CommandAssistant/BlockRegistry.cpp \
    utils/CommandAssistant/CommandPack.cpp \
    utils/Terracotta/TerracottaClient.cpp \
    utils/Hongshi/HongshiClient.cpp \
    utils/Hongshi/HongshiLobbyClient.cpp \
    utils/frp/FrpManager.cpp \
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
    pages/NewsPage.cpp \
    utils/skill/SkillManager.cpp \
    components/SkillManagerDialog.cpp \
    components/PluginRunnerDialog.cpp \
    utils/translation/TranslationService.cpp \
    components/TranslationSettingsDialog.cpp \
    utils/AiModelListFetcher.cpp \
    components/AiModelFetchRow.cpp \
    pages/CommunityForumPage.cpp \
    pages/ForumThreadDetailPage.cpp \
    pages/ForumThreadListPage.cpp \
    pages/ForumComposerPage.cpp \
    pages/ForumUserProfilePage.cpp \
    pages/CreationCenterPage.cpp \
    pages/CreationWorkDetailPage.cpp \
    pages/ForumFriendsPage.cpp \
    pages/ForumPluginListPage.cpp \
    pages/MultiplayerLobbyPage.cpp \
    pages/MultiplayerLobbyDetailPage.cpp \
    pages/FeedbackPage.cpp \
    pages/FeedbackComposePage.cpp \
    pages/ForumReportPage.cpp \
    utils/ForumContentService.cpp \
    utils/ForumLobbyService.cpp \
    utils/Lobby/LobbyInstanceMatcher.cpp \
    utils/Lobby/LobbyContentFetcher.cpp \
    utils/Lobby/LobbyShareLink.cpp \
    utils/ForumChatService.cpp \
    utils/ChatUiStore.cpp \
    utils/ChatBookmarkStore.cpp \
    utils/FeedbackService.cpp \
    utils/ForumImageStore.cpp \
    components/ForumKit.cpp \
    components/ForumLinkRowsEditor.cpp \
    components/ForumPostBrowser.cpp \
    components/PublishKit.cpp \
    components/CommunityAgreementDialog.cpp \
    pages/PublishPage.cpp \
    pages/PublishResourcePage.cpp \
    pages/PublishPlatformPage.cpp \
    pages/PublishBoardPage.cpp

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
    components/ShareToFriendDialog.h \
    components/ThreadTipDialog.h \
    components/CreateGroupDialog.h \
    components/ChatForwardDialog.h \
    components/ChatBookmarkDialog.h \
     components/DownloadTaskCard.h \
     components/TaskProgressWidgets.h \
components/ProgressWidget.h \
components/ProgressDialog.h \
      components/Skin3DWidget.h \
     components/SubNavPanel.h \
     components/LoadingOverlay.h \
       components/BlurLoadingOverlay.h \
       components/AppMessageBox.h \
       components/AppDialogBase.h \
       components/LobbyPublishDialog.h \
       components/LobbyJoinPrepDialog.h \
       components/WorldConvertDialog.h \
       components/HighRiskConfirmDialog.h \
       components/QuizDialog.h \
       components/QuizReminder.h \
       components/HumanCheckDialog.h \
       components/AppInputDialog.h \
       components/AiComposeDialog.h \
       components/AppColorDialog.h \
       components/OnlineResourceDialog.h \
      components/ScreenshotViewer.h \
       components/NotificationCard.h \
     components/NotificationManager.h \
     components/NotificationHistoryDialog.h \
     components/FileOpStepsDialog.h \
       components/BackgroundWidget.h \
       components/PanoramaSkyRenderer.h \
       components/InstanceAssistantWindow.h \
       components/GameFloatingIcon.h \
       components/BedrockInstanceAssistantWindow.h \
       components/NewsCard.h \
     components/HomeAnnouncementBar.h \
     components/HomeFriendsPanel.h \
     components/HomeHotThreadsPanel.h \
     components/NewsDetailDialog.h \
      components/PerfMonitorCard.h \
      components/PerformanceDetailDialog.h \
      components/ModelSelectDialog.h \
      components/LocalModelDialog.h \
      components/CommandPackManagerDialog.h \
      components/CommandPackEditor.h \
       components/StructureDetailDialog.h \
       components/ResourceReferenceDialog.h \
        components/FavoriteFolderDialog.h \
         components/ContentCategoryFilterDialog.h \
         components/ContentViewSwitch.h \
         components/ModRelationDialog.h \
         components/SourceSwitchBar.h \
         components/MasonryContentCard.h \
         components/CardSelectionOverlay.h \
         components/TranslateSwitchCard.h \
         components/AutoHeightTextBrowser.h \
         components/CardListTranslator.h \
         components/TranslatePayloadCodec.h \
         components/OutlinedLabel.h \
         components/MarkdownOutlinedLabel.h \
         components/TextOutlineLayer.h \
         components/OutlinedTextEdit.h \
        components/LocalCategoryDialog.h \
        components/VoxelViewWidget.h \
    components/WorldMap3DView.h \
components/ProjectionBlockEditorDialog.h \
components/OrthoViewsPreviewDialog.h \
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
    pages/AddAccountPage.h \
    pages/ForumAccountPage.h \
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
    utils/ForumAccountService.h \
    utils/HomeCarouselStore.h \
    utils/PageFilter.h \
    utils/QuizBank.h \
    utils/TriviaBank.h \
    utils/McimHelper.h \
    utils/SkinDownloader.h \
    utils/SpeedCalculator.h \
    utils/SystemInfo.h \
    utils/UpdateChecker.h \
    utils/news/NewsFeed.h \
    utils/news/AnnouncementFeed.h \
    utils/RuntimeArchive.h \
    utils/RuntimeDependencyManager.h \
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
    utils/StyleKit.h \
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
    utils/net/Task.h \
    utils/net/NetAction.h \
    utils/net/NetJob.h \
    utils/net/Download.h \
    utils/net/Sink.h \
    utils/net/FileSink.h \
    utils/net/Validator.h \
    utils/net/ChecksumValidator.h \
    utils/net/ByteArraySink.h \
    utils/net/MetaCacheSink.h \
    utils/net/HttpMetaCache.h \
    utils/net/FileSystem.h \
    utils/net/QObjectPtr.h \
    utils/net/NetworkAccess.h \
    utils/net/Mode.h \
    utils/net/BuildConfig.h \
    utils/download/DownloadMirrors.h \
    utils/StartupProgressWidget.h \
    utils/AsyncInitializer.h \
    utils/LowConfigMode.h \
    utils/JarUtils.h \
    utils/ManifestCache.h \
    utils/FavoritesManager.h \
    utils/ContentFavoriteSync.h \
    utils/LocalCategoryManager.h \
    utils/LocalFavoritesManager.h \
    utils/fabric/FabricInstaller.h \
    utils/forge/ForgeDownloader.h \
    utils/forge/ForgeInstaller.h \
    utils/forge/ForgeNewInstallTask.h \
    utils/forge/ForgeProfile.h \
    utils/forge/LegacyForgeInstaller.h \
    utils/forge/LoaderVersionDir.h \
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
    pages/InstanceRecordingsPage.h \
     pages/InstanceResourcesPage.h \
     pages/SaveSettingsPage.h \
     pages/WorldMapPage.h \
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
    utils/LauncherLogger.h \
    utils/FileOpStepLog.h \
    utils/JavaDownloader.h \
     utils/mod/ModData.h \
      utils/WebpLoader.h \
      utils/ThumbnailProvider.h \
      utils/mod/CurseForgeAPI.h \
    utils/CfApiKey.h \
    utils/mod/ModrinthAPI.h \
    utils/mod/MCModAPI.h \
    utils/mod/SearchQueryResolver.h \
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
    utils/content/ContentListCache.h \
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
    utils/Schematic/SchematicCoverRenderer.h \
    utils/Schematic/ProjectionEditorSettings.h \
    utils/Schematic/InstanceTextureLoader.h \
    utils/Schematic/BlockShapeLoader.h \
    utils/Schematic/InstanceBlockNames.h \
    utils/Schematic/SchematicOrthoViewRenderer.h \
    utils/Saves/LevelDatEditor.h \
    utils/ServersDatIO.h \
    utils/McServerPinger.h \
    utils/WorldBackupManager.h \
    utils/WorldMap/WorldMapRenderer.h \
    utils/WorldMap/SeedTerrainRenderer.h \
    utils/WorldMap/TreePredictor.h \
    utils/WorldMap/TreeGen.h \
    utils/WorldMap/TreeData.h \
    utils/WorldMap/MushroomPredictor.h \
    utils/WorldMap/CavePredictor.h \
    utils/WorldMap/StructureFinder.h \
    utils/WorldMap/StructureIcons.h \
    utils/WorldMap/StructureLoot.h \
utils/WorldMap/StructureModelBuilder.h \
utils/WorldMap/StructureTemplates.h \
utils/WorldMap/StrongholdModel.h \
utils/WorldMap/FortressModel.h \
utils/WorldMap/MineshaftModel.h \
    utils/WorldMap/McRandom.h \
    utils/WorldMap/BiomeCatalog.h \
    utils/WorldMap/BiomeName.h \
    utils/WorldMap/OreFinder.h \
    utils/WorldConvert/WorldConverter.h \
    utils/CommandAssistant/CommandTranslator.h \
    utils/CommandAssistant/CommandCompleter.h \
    utils/CommandAssistant/GameDetector.h \
    utils/CommandAssistant/GameRegistry.h \
    utils/CommandAssistant/BlockRegistry.h \
    utils/CommandAssistant/CommandPack.h \
    utils/Terracotta/TerracottaClient.h \
    utils/Hongshi/HongshiClient.h \
    utils/Hongshi/HongshiLobbyClient.h \
    utils/frp/FrpManager.h \
    utils/GravityCone/GravityConeClient.h \
    pages/MultiplayerPage.h \
    pages/MultiplayerUiParts.h \
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
    pages/NewsPage.h \
    utils/skill/SkillInfo.h \
    utils/skill/SkillManager.h \
    components/SkillManagerDialog.h \
    components/PluginRunnerDialog.h \
    utils/translation/TranslationService.h \
    components/TranslationSettingsDialog.h \
    utils/AiModelListFetcher.h \
    pages/CommunityForumPage.h \
    pages/ForumThreadDetailPage.h \
    pages/ForumThreadListPage.h \
    pages/ForumComposerPage.h \
    pages/ForumUserProfilePage.h \
    pages/CreationCenterPage.h \
    pages/CreationWorkDetailPage.h \
    pages/ForumFriendsPage.h \
    pages/ForumPluginListPage.h \
    pages/MultiplayerLobbyPage.h \
    pages/MultiplayerLobbyDetailPage.h \
    pages/FeedbackPage.h \
    pages/FeedbackComposePage.h \
    pages/ForumReportPage.h \
    utils/ForumContentService.h \
    utils/ForumLobbyService.h \
    utils/Lobby/LobbyInstanceMatcher.h \
    utils/Lobby/LobbyContentFetcher.h \
    utils/Lobby/LobbyShareLink.h \
    utils/ForumChatService.h \
    utils/ChatUiStore.h \
    utils/ChatBookmarkStore.h \
    utils/FeedbackService.h \
    utils/ForumImageStore.h \
    components/ForumKit.h \
    components/ForumLinkRowsEditor.h \
    components/ForumPostBrowser.h \
    components/PublishKit.h \
    components/CommunityAgreementDialog.h \
    pages/PublishPage.h \
    pages/PublishResourcePage.h \
    pages/PublishPlatformPage.h \
    pages/PublishBoardPage.h \
    components/AiModelFetchRow.h \
    utils/TouchScroll.h

FORMS += \
    mainwindow.ui

# 资源拆成两块：图片（很少改、体积大）和文本数据（QSS/翻译/JSON，开发时改得最勤）。
# 以前所有资源挤在一个 qrc 里，改一句样式就要重新生成并编译几 MB 的资源代码（约 20s）；
# 拆开后改样式只需要重编文本那一小块（约 2s）。
RESOURCES += \
    resources.qrc \
    resources_data.qrc \
    resources_panorama.qrc

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
    translations/BlockBox_es.ts \
    translations/BlockBox_ja.ts \
    translations/BlockBox_ru.ts

# Auto-generate .qm files from .ts files on each build
lrelease.name = Generate .qm from .ts
lrelease.input = TRANSLATIONS
lrelease.output = ${QMAKE_FILE_PATH}/${QMAKE_FILE_BASE}.qm
lrelease.commands = lrelease ${QMAKE_FILE_IN} -qm ${QMAKE_FILE_OUT}
lrelease.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += lrelease

# Multi-platform support configurations
# HarmonyOS support（qt-ohos 的 QT_ARCH 是 arm64，不能按 arm64-v8a 判断；
# 且 ohos spec 引入 linux.conf 会让下面的 linux 块也命中，故 linux 块需排除 ohos）
contains(QMAKE_PLATFORM, ohos): {
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
    exists($$NODE_ADDON_API_DIR) {
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

# Linux specific configurations（ohos spec 含 linux.conf，会命中 linux scope，需排除）
linux:!ohos: {
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

    # Android ABIs（打包脚本按 ABI 单独构建时经命令行传入，此处仅给缺省值）
    isEmpty(ANDROID_ABIS) {
        ANDROID_ABIS = armeabi-v7a arm64-v8a x86 x86_64
    }

    # Android permissions (added via AndroidManifest.xml)
    # INTERNET, WRITE_EXTERNAL_STORAGE, READ_EXTERNAL_STORAGE, etc.

    # Disable Windows-specific code on Android
    DEFINES -= WINDOWS_OS

    # Use OpenGL ES instead of desktop OpenGL
    DEFINES += QT_OPENGL_ES_2

    # Android-specific libraries
    LIBS += -llog -landroid

    # ── 陶瓦联机（Terracotta）安卓内置核心 ──
    # 打包前先运行 软件发布/packaging/fetch_terracotta_android.ps1 下载各 ABI 的
    # libterracotta.so；打包脚本按单一 ABI 调 qmake，这里按 ANDROID_ABIS 逐个挂载，
    # 缺失时跳过（联机页会提示核心未内置），不影响其余功能。
    TERRACOTTA_SO_DIR = $$PWD/third_party/terracotta
    for (abi, ANDROID_ABIS) {
        tc_so = $$TERRACOTTA_SO_DIR/$$abi/libterracotta.so
        exists($$tc_so) {
            ANDROID_EXTRA_LIBS += $$tc_so
        } else {
            message("Terracotta android core missing for $$abi — run 软件发布/packaging/fetch_terracotta_android.ps1 (联机页将提示核心未内置)")
        }
    }

    # ── OpenSSL（安卓端 HTTPS 必需）──
    # Qt 的 TLS 后端是运行时才 dlopen 的插件（plugins/tls/qopensslbackend），
    # androiddeployqt 只打包插件本体，不会自动带上 libssl_3.so / libcrypto_3.so。
    # 缺这两个库时 QSslSocket 起不来，所有 https 请求直接失败——表现为
    # 「安卓端不能联网」（下载、社区、检查更新全部报错），而电脑版一切正常。
    # 直接从当前 Qt 安卓套件的 lib 目录取，套件路径随构建的 ABI 自动对应。
    # 注意：这两个 exists 必须分开写，qmake 的 for 循环不认字面量列表。
    OPENSSL_SSL_SO = $$[QT_INSTALL_LIBS]/libssl_3.so
    OPENSSL_CRYPTO_SO = $$[QT_INSTALL_LIBS]/libcrypto_3.so
    exists($$OPENSSL_SSL_SO) {
        ANDROID_EXTRA_LIBS += $$OPENSSL_SSL_SO
    } else {
        message("OpenSSL for Android missing: $$OPENSSL_SSL_SO — 安卓端将无法访问 https")
    }
    exists($$OPENSSL_CRYPTO_SO) {
        ANDROID_EXTRA_LIBS += $$OPENSSL_CRYPTO_SO
    } else {
        message("OpenSSL for Android missing: $$OPENSSL_CRYPTO_SO — 安卓端将无法访问 https")
    }
    # ⚠ 上面这对库不能只包 `_3` 后缀的名字：Qt 套件里 libssl_3.so 的内部
    # 依赖（DT_NEEDED）写的是「libcrypto.so」，而套件里的文件偏偏叫
    # libcrypto_3.so —— APK 里缺这个名字时，Qt 启动加载 SSL 库会直接
    # dlopen 失败，应用开屏即闪退（UnsatisfiedLinkError），手机端整个打不开。
    # 所以必须额外打一份按 SONAME 命名的 libcrypto.so（由一键打包.bat 在
    # qmake 前从套件复制到 third_party/openssl/<abi>/，目录名与 terracotta
    # 一致用连字符 ABI 名）。
    for (abi, ANDROID_ABIS) {
        OPENSSL_CRYPTO_SONAME_SO = $$PWD/third_party/openssl/$$abi/libcrypto.so
        exists($$OPENSSL_CRYPTO_SONAME_SO) {
            ANDROID_EXTRA_LIBS += $$OPENSSL_CRYPTO_SONAME_SO
        } else {
            warning("libcrypto.so (SONAME copy) missing: $$OPENSSL_CRYPTO_SONAME_SO — 安卓端启动会闪退！请先跑一键打包.bat 的 OpenSSL 复制步骤")
        }
    }
}


# ── Cubiomes：按种子计算结构位置（MIT，https://github.com/Cubitect/cubiomes） ──
INCLUDEPATH += third_party/cubiomes
QMAKE_CFLAGS += -fwrapv -O2
SOURCES += \
    third_party/cubiomes/noise.c \
    third_party/cubiomes/biomes.c \
    third_party/cubiomes/layers.c \
    third_party/cubiomes/biomenoise.c \
    third_party/cubiomes/generator.c \
    third_party/cubiomes/finders.c \
    third_party/cubiomes/util.c \
    third_party/cubiomes/quadbase.c

