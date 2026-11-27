QT       += core gui network concurrent svg

# Android/OHOS use OpenGL ES; QOpenGLWidget (QtOpenGLWidgets) also works over ES
android: QT += opengl openglwidgets
!android: QT += opengl openglwidgets

# 扫码登录：手机端用相机取景识别电脑上的登录二维码，取景要 Qt Multimedia。
# 桌面端用不到相机（电脑端只负责显示二维码 / 从图片识别），所以只在安卓构建里链接，
# 顺带定义 BLOCKBOX_HAS_QR_CAMERA 让界面知道这台设备能不能扫码。
android {
    QT += multimedia multimediawidgets
    DEFINES += BLOCKBOX_HAS_QR_CAMERA
}

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# 官方构建标识：打包脚本（软件发布/一键打包.bat）编译时追加 DEFINES+=BLOCKBOX_OFFICIAL_BUILD。
# 未定义该宏时一律按「非官方构建」处理：主窗口标题追加水印、启动后提示一次风险、
# 「设置 → 关于」用告警色显示构建标识。

# 发布版本号：打包脚本传 APP_VERSION=<版本>（如 qmake ... APP_VERSION=beta9.1），
# 编译期写死进二进制，保证程序自报版本与安装包文件名、官网公示清单里的版本号一致。
# 不传时 AppVersion::current() 用源码里的默认值（本地开发构建）。
!isEmpty(APP_VERSION) {
    DEFINES += BLOCKBOX_BUILD_VERSION=\\\"$$APP_VERSION\\\"
}

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
    # 崩溃上报读取模块基址（GetModuleInformation）要用 psapi
    LIBS += -lpsapi
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
    components/SystemTray.cpp \
    components/LaunchTaskCard.cpp \
components/CustomCheckBox.cpp \
     components/CustomRadioButton.cpp \
     components/CollapsibleSectionCard.cpp \
     components/InstanceFolderTree.cpp \
    components/LanTransferDialog.cpp \
    components/ShareToFriendDialog.cpp \
    components/ForumClaimDialog.cpp \
    components/ThreadTipDialog.cpp \
    components/CreateGroupDialog.cpp \
    components/ChatForwardDialog.cpp \
    components/MedalShareDialog.cpp \
    components/ChatBookmarkDialog.cpp \
    components/ChatSettingsDialog.cpp \
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
     components/QrLoginDialog.cpp \
     components/LaunchLogDialog.cpp \
     components/LobbyPublishDialog.cpp \
     components/LobbyJoinPrepDialog.cpp \
     components/WorldConvertDialog.cpp \
     components/WorldBackupDialog.cpp \
     components/WorldBackupCenterDialog.cpp \
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
    components/ServerDownloadDialog.cpp \
     components/NotificationHistoryDialog.cpp \
     components/FileOpStepsDialog.cpp \
     components/InstanceAssistantWindow.cpp \
     components/InstancePluginExtPage.cpp \
     components/GameFloatingIcon.cpp \
     components/TouchControlOverlay.cpp \
     components/TouchLayoutEditorDialog.cpp \
     components/BedrockInstanceAssistantWindow.cpp \
     components/BackgroundWidget.cpp \
     components/PanoramaSkyRenderer.cpp \
     components/NewsCard.cpp \
     components/HomeAnnouncementBar.cpp \
     components/NewsDetailDialog.cpp \
     components/PerfMonitorCard.cpp \
     components/PerformanceDetailDialog.cpp \
     components/ModelSelectDialog.cpp \
     components/ModelProviderSettingsPanel.cpp \
     components/LocalModelDialog.cpp \
     components/CommandPackManagerDialog.cpp \
     components/CommandPackEditor.cpp \
     components/ResourceReferenceDialog.cpp \
     components/FavoriteFolderDialog.cpp \
     components/ContentCategoryFilterDialog.cpp \
     components/ResourceFilterDialog.cpp \
     components/ResourceSyncListWindow.cpp \
     components/ContentViewSwitch.cpp \
     components/ModRelationDialog.cpp \
     components/CompatibilityCheckDialog.cpp \
     components/MigrationWizardDialog.cpp \
     components/NetworkCheckDialog.cpp \
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
components/ProjectionBlockEditorDialog.cpp \
components/ProjectionFillShapeDialog.cpp \
components/OrthoViewsPreviewDialog.cpp \
components/ProjectionPreviewDialog.cpp \
components/ProjectionMaterialDialog.cpp \
components/LayerBuildPlanWidget.cpp \
components/ProjectionBuildGuideDialog.cpp \
     components/HomeDataVisuals.cpp \
     components/HomeFriendsPanel.cpp \
     components/HomeHotThreadsPanel.cpp \
     components/AnnualReportCard.cpp \
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
     pages/settings/SettingsControlPage.cpp \
     pages/settings/BedrockInstanceSettingsPage.cpp \
     pages/settings/SettingsAiAssistantPage.cpp \
     pages/settings/SettingsAboutPage.cpp \
     pages/InstanceSelectPage.cpp \
    pages/LanTransferPage.cpp \
    pages/AddAccountPage.cpp \
    pages/ForumAccountPage.cpp \
     pages/SkinStationPage.cpp \
    pages/LibraryPage.cpp \
     pages/PlaytimeStatsPage.cpp \
     pages/AnnualReportPage.cpp \
     pages/QrScanPage.cpp \
    pages/AiChatPage.cpp \
     pages/InstanceManagePage.cpp \
     pages/InstanceModsPage.cpp \
     pages/InstanceOverviewPage.cpp \
     pages/InstanceLogPage.cpp \
         pages/InstanceFilePage.cpp \
     pages/InstanceRecordingsPage.cpp \
     pages/InstanceResourcesPage.cpp \
     pages/SaveSettingsPage.cpp \
     pages/WorldMapEntryPage.cpp \
     pages/InstanceHomePage.cpp \
     pages/InstanceJavaPage.cpp \
     pages/InstanceJavaDownloadPage.cpp \
     pages/InstanceServerPage.cpp \
     pages/ProjectionManagePage.cpp \
     pages/ProjectionEditPage.cpp \
     pages/ServerManagePage.cpp \
    pages/LocalModDetailPage.cpp \
    pages/settings/InstanceGameSettingsPage.cpp \
    pages/MicrosoftLoginDialog.cpp \
    pages/LaunchDetailsPage.cpp \
    pages/GameLaunchPage.cpp \
    pages/InstallInstancePage.cpp \
    pages/LoaderDetailPage.cpp \
    pages/ForgeVersionListPage.cpp \
    pages/FabricVersionListPage.cpp \
    pages/ColorPickerPage.cpp \
    pages/TaskListPage.cpp \
    pages/TaskDetailPage.cpp \
    utils/SettingsManager.cpp \
    utils/QuickLaunchManager.cpp \
    utils/MobileControlSettings.cpp \
    utils/ForumAccountService.cpp \
    utils/HomeCarouselStore.cpp \
    utils/HomePresetCovers.cpp \
    utils/QuizBank.cpp \
    utils/TriviaBank.cpp \
    utils/SkinDownloader.cpp \
    utils/SpeedCalculator.cpp \
    utils/SystemInfo.cpp \
    utils/UpdateChecker.cpp \
    utils/trust/TrustManifest.cpp \
    utils/trust/SelfIntegrityChecker.cpp \
    utils/trust/LogSanitizer.cpp \
    utils/news/NewsFeed.cpp \
    utils/news/AnnouncementFeed.cpp \
    utils/RuntimeArchive.cpp \
    utils/RuntimeDependencyManager.cpp \
    utils/HarmonyBridge.cpp \
    utils/LanguageManager.cpp \
    utils/LanTransfer.cpp \
    utils/ChatP2P.cpp \
    utils/tunnel/TunnelManager.cpp \
    utils/tunnel/EasyTierEngine.cpp \
    utils/FileExplorer.cpp \
    utils/FileReuse.cpp \
    utils/AndroidBridge.cpp \
    utils/AuthManager.cpp \
    utils/auth/AuthManagerMicrosoft.cpp \
    utils/auth/AuthlibInjector.cpp \
    utils/BackgroundManager.cpp \
    utils/BingWallpaperManager.cpp \
    utils/PanoramaThemeManager.cpp \
    utils/ThemeManager.cpp \
    utils/StyleKit.cpp \
    utils/GameLauncher.cpp \
    utils/BedrockLauncher.cpp \
    utils/launcher/GameLauncherJava.cpp \
    utils/launcher/GameLauncherPrepare.cpp \
    utils/launcher/GameLauncherCommand.cpp \
    utils/ErrorAnalyzer.cpp \
    utils/ResourceManager.cpp \
    utils/QrCode.cpp \
    utils/QrDecode.cpp \
    utils/QrTables.cpp \
    utils/ResourceVersionSyncer.cpp \
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
    utils/compat/LocalCompatibilityChecker.cpp \
    utils/compat/CompatMatrix.cpp \
    utils/compat/CompatMatrixFetcher.cpp \
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
    utils/MedalCatalog.cpp \
    utils/MedalShareCard.cpp \
    utils/GameAchievementWatcher.cpp \
    utils/GameAchievementService.cpp \
    utils/GameAchievementCatalog.cpp \
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
    utils/MigrationService.cpp \
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
    utils/links/LinkRouter.cpp \
    utils/modpack/ModpackDetector.cpp \
    utils/modpack/ModpackImporter.cpp \
    utils/modpack/ModpackFileAdviser.cpp \
    utils/modpack/ModpackExporter.cpp \
    utils/content/ContentDownloader.cpp \
    utils/content/ContentListCache.cpp \
     utils/ClipboardMonitor.cpp \
     utils/FileImportRouter.cpp \
     utils/AiService.cpp \
     utils/AiMarkdownStyle.cpp \
     utils/mcp/McpServer.cpp \
     utils/mcp/McpToolCatalog.cpp \
     utils/dsh/DshPluginInstaller.cpp \
     utils/zcode/ZcodePluginInstaller.cpp \
    utils/GameScreenshot.cpp \
    utils/SystemTools.cpp \
    utils/MemoryOptimizer.cpp \
    utils/HardwareMonitor.cpp \
    utils/MultiThreadDownloader.cpp \
    utils/SegmentedDownloader.cpp \
    utils/LocalModelManager.cpp \
    utils/GithubAccelerator.cpp \
    utils/PageTransitionAnimator.cpp \
    utils/ContentAnimator.cpp \
    utils/CommandAssistant/LevelDatReader.cpp \
    utils/CommandAssistant/LitematicReader.cpp \
    utils/Saves/LevelDatEditor.cpp \
    utils/ServersDatIO.cpp \
    utils/McServerPinger.cpp \
    utils/server/LocalServerManager.cpp \
    utils/server/ServerAdminStore.cpp \
    utils/WorldBackupManager.cpp \
    utils/PlaytimeStats.cpp \
    utils/WorldMap/StructureFinder.cpp \
    utils/WorldMap/StructureIcons.cpp \
    utils/MapBlockPalette.cpp \
    utils/WorldMapSignalBridge.cpp \
    utils/WorldConvert/WorldConverter.cpp \
    utils/Schematic/NbtCodec.cpp \
    utils/Schematic/LitematicEditor.cpp \
    utils/Schematic/SchematicDocument.cpp \
    utils/Schematic/ProjectionFillShape.cpp \
    utils/Schematic/SchematicCoverRenderer.cpp \
    utils/Schematic/ProjectionEditorSettings.cpp \
    utils/Schematic/InstanceTextureLoader.cpp \
    utils/Schematic/BlockShapeLoader.cpp \
    utils/Schematic/InstanceBlockNames.cpp \
    utils/Schematic/SchematicOrthoViewRenderer.cpp \
    utils/Schematic/EntityModelLoader.cpp \
    utils/Schematic/InstanceEntityTextures.cpp \
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
    utils/translation/ContentTranslationService.cpp \
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
    pages/ForumSchematicListPage.cpp \
    pages/CommunitySkinPage.cpp \
    pages/CommunitySkinDetailPage.cpp \
    pages/MultiplayerLobbyPage.cpp \
    pages/MultiplayerLobbyDetailPage.cpp \
    pages/FeedbackPage.cpp \
    pages/FeedbackComposePage.cpp \
    pages/FeedbackDetailPage.cpp \
    pages/TranslationComposePage.cpp \
    pages/ForumReportPage.cpp \
    utils/ForumContentService.cpp \
    utils/ForumLobbyService.cpp \
    utils/Lobby/LobbyInstanceMatcher.cpp \
    utils/Lobby/LobbyContentFetcher.cpp \
    utils/Lobby/LobbyShareLink.cpp \
    utils/ForumChatService.cpp \
    utils/ForumPlayerStatsService.cpp \
    utils/ChatUiStore.cpp \
    utils/ChatPrefs.cpp \
    utils/ChatBookmarkStore.cpp \
    utils/FeedbackService.cpp \
    utils/CrashReporter.cpp \
    utils/Telemetry.cpp \
    utils/ForumImageStore.cpp \
    utils/CoverImageCache.cpp \
    components/ForumKit.cpp \
    components/PlayerCountChart.cpp \
    components/ForumLinkRowsEditor.cpp \
    components/ForumTagEditor.cpp \
    components/ForumCarouselEditor.cpp \
    components/ForumPostBrowser.cpp \
    components/PublishKit.cpp \
    components/CommunityAgreementDialog.cpp \
    components/CrashReportDialog.cpp \
    components/UserAgreementDialog.cpp \
    components/MailFeedbackDialog.cpp \
    pages/PublishPage.cpp \
    pages/PublishResourcePage.cpp \
    pages/PublishPlatformPage.cpp \
    pages/PublishBoardPage.cpp \
    pages/MorePage.cpp \
    pages/SeedMapPage.cpp \
    pages/MemoryOptimizePage.cpp \
    pages/MultiThreadDownloadPage.cpp \

HEADERS += \
    platform.h \
    mainwindow.h \
    layouts/FlowLayout.h \
    layouts/MasonryLayout.h \
    components/TopBar.h \
    components/UpdateProgressButton.h \
    components/TaskBar.h \
    components/SideBar.h \
    components/SystemTray.h \
    components/LaunchTaskCard.h \
components/CustomCheckBox.h \
     components/CustomRadioButton.h \
     components/CollapsibleSectionCard.h \
     components/InstanceFolderTree.h \
    components/LanTransferDialog.h \
    components/ShareToFriendDialog.h \
    components/ForumClaimDialog.h \
    components/ThreadTipDialog.h \
    components/CreateGroupDialog.h \
    components/ChatForwardDialog.h \
    components/ChatBookmarkDialog.h \
    components/ChatSettingsDialog.h \
    components/MedalShareDialog.h \
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
       components/QrLoginDialog.h \
       components/LaunchLogDialog.h \
       components/LobbyPublishDialog.h \
       components/LobbyJoinPrepDialog.h \
       components/WorldConvertDialog.h \
       components/WorldBackupDialog.h \
       components/WorldBackupCenterDialog.h \
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
    components/ServerDownloadDialog.h \
     components/NotificationHistoryDialog.h \
     components/FileOpStepsDialog.h \
       components/BackgroundWidget.h \
       components/PanoramaSkyRenderer.h \
       components/InstanceAssistantWindow.h \
       components/InstancePluginExtPage.h \
       components/GameFloatingIcon.h \
       components/TouchControlOverlay.h \
       components/TouchLayoutEditorDialog.h \
       components/BedrockInstanceAssistantWindow.h \
       components/NewsCard.h \
     components/HomeAnnouncementBar.h \
     components/HomeFriendsPanel.h \
     components/HomeHotThreadsPanel.h \
     components/AnnualReportCard.h \
     components/NewsDetailDialog.h \
      components/PerfMonitorCard.h \
      components/PerformanceDetailDialog.h \
      components/ModelSelectDialog.h \
      components/ModelProviderSettingsPanel.h \
      components/LocalModelDialog.h \
      components/CommandPackManagerDialog.h \
      components/CommandPackEditor.h \
       components/ResourceReferenceDialog.h \
        components/FavoriteFolderDialog.h \
         components/ContentCategoryFilterDialog.h \
         components/ResourceFilterDialog.h \
         components/ResourceSyncListWindow.h \
         components/ContentViewSwitch.h \
         components/ModRelationDialog.h \
         components/CompatibilityCheckDialog.h \
         components/MigrationWizardDialog.h \
         components/NetworkCheckDialog.h \
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
components/ProjectionBlockEditorDialog.h \
components/ProjectionFillShapeDialog.h \
components/OrthoViewsPreviewDialog.h \
components/ProjectionPreviewDialog.h \
components/ProjectionMaterialDialog.h \
components/LayerBuildPlanWidget.h \
components/ProjectionBuildGuideDialog.h \
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
     pages/SkinStationPage.h \
    pages/LibraryPage.h \
     pages/PlaytimeStatsPage.h \
     pages/AnnualReportPage.h \
     pages/QrScanPage.h \
    pages/AiChatPage.h \
    pages/MicrosoftLoginDialog.h \
    pages/LaunchDetailsPage.h \
    pages/GameLaunchPage.h \
    pages/InstallInstancePage.h \
    pages/LoaderDetailPage.h \
    pages/ForgeVersionListPage.h \
    pages/FabricVersionListPage.h \
    pages/ColorPickerPage.h \
    pages/TaskListPage.h \
    pages/TaskDetailPage.h \
    utils/SettingsManager.h \
    utils/QuickLaunchManager.h \
    utils/MobileControlSettings.h \
    utils/ForumAccountService.h \
    utils/HomeCarouselStore.h \
    utils/HomePresetCovers.h \
    utils/AssetUrls.h \
    utils/PageFilter.h \
    utils/QuizBank.h \
    utils/TriviaBank.h \
    utils/McimHelper.h \
    utils/SkinDownloader.h \
    utils/SpeedCalculator.h \
    utils/SystemInfo.h \
    utils/UpdateChecker.h \
    utils/trust/TrustManifest.h \
    utils/trust/SelfIntegrityChecker.h \
    utils/trust/LogSanitizer.h \
    utils/trust/BuildChannel.h \
    utils/news/NewsFeed.h \
    utils/news/AnnouncementFeed.h \
    utils/RuntimeArchive.h \
    utils/RuntimeDependencyManager.h \
    utils/HarmonyBridge.h \
    utils/LanguageManager.h \
    utils/LanTransfer.h \
    utils/ChatP2P.h \
    utils/tunnel/TunnelManager.h \
    utils/tunnel/TunnelEngine.h \
    utils/tunnel/EasyTierEngine.h \
    utils/FileExplorer.h \
    utils/FileReuse.h \
    utils/AndroidBridge.h \
    utils/AuthManager.h \
    utils/auth/AuthlibInjector.h \
    utils/BackgroundManager.h \
    utils/BingWallpaperManager.h \
    utils/PanoramaThemeManager.h \
    utils/ThemeManager.h \
    utils/StyleKit.h \
    utils/GameLauncher.h \
    utils/BedrockLauncher.h \
    utils/ErrorAnalyzer.h \
    utils/ResourceManager.h \
    utils/QrCode.h \
    utils/QrTables.h \
    utils/ResourceVersionSyncer.h \
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
    utils/compat/LocalCompatibilityChecker.h \
    utils/compat/CompatMatrix.h \
    utils/compat/CompatMatrixFetcher.h \
    utils/ManifestCache.h \
    utils/MedalCatalog.h \
    utils/MedalShareCard.h \
    utils/GameAchievementWatcher.h \
    utils/GameAchievementService.h \
    utils/GameAchievementCatalog.h \
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
     pages/WorldMapEntryPage.h \
     pages/InstanceHomePage.h \
     pages/InstanceJavaPage.h \
     pages/InstanceJavaDownloadPage.h \
     pages/InstanceServerPage.h \
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
    utils/MigrationService.h \
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
    utils/links/LinkRouter.h \
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
 utils/AiMarkdownStyle.h \
     utils/mcp/McpServer.h \
     utils/mcp/McpToolCatalog.h \
     utils/dsh/DshPluginInstaller.h \
     utils/zcode/ZcodePluginInstaller.h \
    utils/GameScreenshot.h \
    utils/SystemTools.h \
    utils/MemoryOptimizer.h \
    utils/HardwareMonitor.h \
    utils/MultiThreadDownloader.h \
    utils/SegmentedDownloader.h \
    utils/LocalModelManager.h \
    utils/GithubAccelerator.h \
    utils/PageTransitionAnimator.h \
    utils/ContentAnimator.h \
    utils/CommandAssistant/LevelDatReader.h \
    utils/CommandAssistant/LitematicReader.h \
    utils/Schematic/NbtCodec.h \
    utils/Schematic/LitematicEditor.h \
    utils/Schematic/SchematicDocument.h \
    utils/Schematic/ProjectionFillShape.h \
    utils/Schematic/SchematicDownloadTarget.h \
    utils/Schematic/SchematicCoverRenderer.h \
    utils/Schematic/ProjectionEditorSettings.h \
    utils/Schematic/InstanceTextureLoader.h \
    utils/Schematic/BlockShapeLoader.h \
    utils/Schematic/InstanceBlockNames.h \
    utils/Schematic/SchematicOrthoViewRenderer.h \
    utils/Schematic/EntityModelLoader.h \
    utils/Schematic/InstanceEntityTextures.h \
    utils/Saves/LevelDatEditor.h \
    utils/ServersDatIO.h \
    utils/McServerPinger.h \
    utils/server/LocalServerManager.h \
    utils/server/ServerAdminStore.h \
    utils/WorldBackupManager.h \
    utils/PlaytimeStats.h \
    utils/WorldMap/StructureFinder.h \
    utils/WorldMap/StructureIcons.h \
    utils/MapBlockPalette.h \
    utils/WorldMapSignalBridge.h \
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
    utils/translation/ContentTranslationService.h \
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
    pages/ForumSchematicListPage.h \
    pages/CommunitySkinPage.h \
    pages/CommunitySkinDetailPage.h \
    pages/SkinLibraryItem.h \
    pages/MultiplayerLobbyPage.h \
    pages/MultiplayerLobbyDetailPage.h \
    pages/FeedbackPage.h \
    pages/FeedbackComposePage.h \
    pages/FeedbackDetailPage.h \
    pages/TranslationComposePage.h \
    pages/ForumReportPage.h \
    utils/ForumContentService.h \
    utils/ForumLobbyService.h \
    utils/Lobby/LobbyInstanceMatcher.h \
    utils/Lobby/LobbyContentFetcher.h \
    utils/Lobby/LobbyShareLink.h \
    utils/ForumChatService.h \
    utils/ForumPlayerStatsService.h \
    utils/ChatUiStore.h \
    utils/ChatPrefs.h \
    utils/ChatBookmarkStore.h \
    utils/FeedbackService.h \
    utils/CrashReporter.h \
    utils/Telemetry.h \
    utils/ForumImageStore.h \
    utils/CoverImageCache.h \
    components/ForumKit.h \
    components/PlayerCountChart.h \
    components/ForumLinkRowsEditor.h \
    components/ForumTagEditor.h \
    components/ForumCarouselEditor.h \
    components/ForumPostBrowser.h \
    components/PublishKit.h \
    components/CommunityAgreementDialog.h \
    components/CrashReportDialog.h \
    components/UserAgreementDialog.h \
    components/MailFeedbackDialog.h \
    pages/PublishPage.h \
    pages/PublishResourcePage.h \
    pages/PublishPlatformPage.h \
    pages/PublishBoardPage.h \
    pages/MorePage.h \
    pages/SeedMapPage.h \
    pages/MemoryOptimizePage.h \
    pages/MultiThreadDownloadPage.h \
    components/AiModelFetchRow.h \
    utils/TouchScroll.h

FORMS += \
    mainwindow.ui

# 资源拆成两块：图片（很少改、体积大）和文本数据（QSS/翻译/JSON，开发时改得最勤）。
# 以前所有资源挤在一个 qrc 里，改一句样式就要重新生成并编译几 MB 的资源代码（约 20s）；
# 拆开后改样式只需要重编文本那一小块（约 2s）。
# 旋转全景的六张图不再编进安装包：首次选中主题时按需下载（见 utils/PanoramaThemeManager）。
# resources_panorama.qrc 仅保留给开发探针使用，正式版不加入编译。
RESOURCES += \
    resources.qrc \
    resources_data.qrc

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


# ═══════════════════════════════════════════════════════════════════
# 存档地图：桌面端改为「首次进入时从 CDN 下载原生模块」
#
# 模块源码与构建脚本在 modules/worldmap/：主程序不再把地图页 / 地图渲染
# 编译进去，安装包因此变小，地图也能单独更新而不必重发启动器。
#
# 移动端（安卓 / 鸿蒙）与 Windows ARM64（MSVC 工具链）系统限制运行时
# 加载下载的动态库，这几端继续把地图编译进主程序；入口页会自动走内置
# 分支，调用方式与下载模块版完全一致。
# ═══════════════════════════════════════════════════════════════════
WORLDMAP_REMOTE_MODULE = false
win32-g++: WORLDMAP_REMOTE_MODULE = true
linux:!android:!ohos: WORLDMAP_REMOTE_MODULE = true

equals(WORLDMAP_REMOTE_MODULE, true) {
    DEFINES += BLOCKBOX_WORLDMAP_REMOTE_MODULE

    # 模块动态库要调用主程序里的设置 / 主题 / 通知 / 图标 / 存档解析等同一份
    # 实现（避免出现两个单例、两套状态），因此主程序需要把符号导出：
    #   - MinGW：--export-all-symbols 并生成导入库，供模块链接；
    #   - Linux：--export-dynamic，模块加载时从主程序全局符号里解析。
    win32-g++ {
        CONFIG(debug, debug|release) {
            BB_WORLDMAP_IMPLIB_DIR = debug
        } else {
            BB_WORLDMAP_IMPLIB_DIR = release
        }
        BB_WORLDMAP_IMPLIB = $$OUT_PWD/$$BB_WORLDMAP_IMPLIB_DIR/BlockBoxWindows.dll.a
        QMAKE_LFLAGS += -Wl,--export-all-symbols
        QMAKE_LFLAGS += -Wl,--out-implib,$$BB_WORLDMAP_IMPLIB
    }
    linux:!android:!ohos {
        QMAKE_LFLAGS += -Wl,--export-dynamic
    }

    # 种子地图（更多 > 种子地图）只用得到「按种子渲染」这一小块：把它连同
    # 配色表 / 树木 / 蘑菇 / 洞穴预测直接编进主程序，用户不用先下整个存档
    # 地图模块才能看一眼种子地形。这里不含地图页、结构模板与那套 NBT 资源，
    # 只是渲染用的纯函数，体积比整个模块小得多。
    # 注意：这里逐条 SOURCES +=，不写成续行列表 —— 这些文件在内置分支的
    # 续行列表里也出现，qmake 在某些版本里会对跨分支续行列表做去重时丢条目。
    # MapBlockPalette 已在主 SOURCES 列表（顶层），此处不重复。
    SOURCES += utils/WorldMap/WorldMapRenderer.cpp
    SOURCES += utils/WorldMap/SeedTerrainRenderer.cpp
    SOURCES += utils/WorldMap/TreePredictor.cpp
    SOURCES += utils/WorldMap/TreeGen.cpp
    SOURCES += utils/WorldMap/MushroomPredictor.cpp
    SOURCES += utils/WorldMap/CavePredictor.cpp
    SOURCES += utils/WorldMap/McRandom.cpp
    SOURCES += utils/WorldMap/BiomeCatalog.cpp
    SOURCES += utils/WorldMap/OreFinder.cpp
    HEADERS += utils/WorldMap/WorldMapRenderer.h
    HEADERS += utils/WorldMap/SeedTerrainRenderer.h
    HEADERS += utils/WorldMap/TreePredictor.h
    HEADERS += utils/WorldMap/TreeGen.h
    HEADERS += utils/WorldMap/TreeData.h
    HEADERS += utils/WorldMap/MushroomPredictor.h
    HEADERS += utils/WorldMap/CavePredictor.h
    HEADERS += utils/WorldMap/McRandom.h
    HEADERS += utils/WorldMap/BiomeCatalog.h
    HEADERS += utils/WorldMap/OreFinder.h
}

!equals(WORLDMAP_REMOTE_MODULE, true) {
    # ── 内置地图（安卓 / 鸿蒙 / Windows ARM64）──
    # 地图资料（结构模板 / 战利品表 等）只有内置地图才随主程序编译
    RESOURCES += resources_worldmap.qrc
    SOURCES += \
        pages/WorldMapPage.cpp \
        components/WorldMap3DView.cpp \
        components/MapExportDialog.cpp \
        components/StructureDetailDialog.cpp \
        utils/WorldMap/WorldMapRenderer.cpp \
        utils/WorldMap/SeedTerrainRenderer.cpp \
        utils/WorldMap/TreePredictor.cpp \
        utils/WorldMap/TreeGen.cpp \
        utils/WorldMap/MushroomPredictor.cpp \
        utils/WorldMap/CavePredictor.cpp \
        utils/WorldMap/StructureLoot.cpp \
        utils/WorldMap/StructureModelBuilder.cpp \
        utils/WorldMap/StructureTemplates.cpp \
        utils/WorldMap/StrongholdModel.cpp \
        utils/WorldMap/FortressModel.cpp \
        utils/WorldMap/MineshaftModel.cpp \
        utils/WorldMap/McRandom.cpp \
        utils/WorldMap/BiomeCatalog.cpp \
        utils/WorldMap/BiomeName.cpp \
        utils/WorldMap/OreFinder.cpp

    HEADERS += \
        pages/WorldMapPage.h \
        components/WorldMap3DView.h \
        components/MapExportDialog.h \
        components/StructureDetailDialog.h \
        utils/WorldMap/WorldMapRenderer.h \
        utils/WorldMap/SeedTerrainRenderer.h \
        utils/WorldMap/TreePredictor.h \
        utils/WorldMap/TreeGen.h \
        utils/WorldMap/TreeData.h \
        utils/WorldMap/MushroomPredictor.h \
        utils/WorldMap/CavePredictor.h \
        utils/WorldMap/StructureLoot.h \
        utils/WorldMap/StructureModelBuilder.h \
        utils/WorldMap/StructureTemplates.h \
        utils/WorldMap/StrongholdModel.h \
        utils/WorldMap/FortressModel.h \
        utils/WorldMap/MineshaftModel.h \
        utils/WorldMap/McRandom.h \
        utils/WorldMap/BiomeCatalog.h \
        utils/WorldMap/BiomeName.h \
        utils/WorldMap/OreFinder.h
}

# 地图入口页与模块下载管理器：所有平台都编译。
# 非远端平台里入口页直接转内置地图页，管理器不会联网。
SOURCES += \
    utils/WorldMapModule/WorldMapModuleManager.cpp
HEADERS += \
    utils/WorldMapModule/WorldMapModuleManager.h

# 本地数据包管理：世界级数据包的扫描/启停/排序/预览/兼容性检测，桌面端与移动端通用。
SOURCES += \
    utils/Saves/DataPackManager.cpp \
    components/DataPackManagerDialog.cpp
HEADERS += \
    utils/Saves/DataPackManager.h \
    components/DataPackManagerDialog.h
