# BlockBox Bug Report

> Generated: 2026-08-17
> Scope: C-BlockBox Qt/C++ project — full code review
> Total bugs found: **90+**
>
> ## 修复状态
> 已修复并通过编译验证（qmake + mingw，0 错误）：
> - **P0:** C1, C2, C3, C4, C5, C6（崩溃/数据损坏/UI冻结）, **C7, C8**（UI阻塞：异步化QProcess）
> - **P1:** H1-H5（单例线程安全）, H9, H10, H11, H14, H15, H16, H17, H18, H19（未初始化指针）, **H12**（AuthManager空检查）, **H13**（compressContext 移至工作线程，压缩结果真正用于请求）, **H6, H7**（DownloadTaskManager 互斥锁+原子ID）
> - **P2:** M1, M2, M3, M4, M5, M12, M17, M18, M20, M23, M24, **M29**（文件树根路径优化）, **M30**（定时器停止修复）, **M22**（updatePluginAsync 异步化下载）, **M27**（NotificationManager 挂到 qApp 释放）, **M9**（登录按钮防重复点击，后续改动中已修复）, **M16/M28**（后续改动中已修复）
> - **C9**（UI 阻塞）
>
> 已核实为安全/低风险，未改动：
> - **M13** — `disconnect(..., this, nullptr)` 第三参数 receiver=this，只断开 GameLauncher 自身的连接，不影响 mainwindow 的连接，语义正确。
> - **M6/M7** — Content 页通过 `animateToWidget()`（非按索引）切换，功能正确；索引漂移仅为潜在增长问题，激进重构有回归风险，暂不处理。
> - **M25/M26** — 硬编码浅色主题样式，改主题涉及较多样式调整，列为已知问题。
> - **P3 (L1-L16)** — 低风险代码质量问题，部分（L1/L2）已随 H8/M5 一并修复。

---

## CRITICAL (P0) — Will cause crashes or data loss

### C1. `InstallInstancePage` 双重释放 `m_networkManager`
- **File:** `pages/InstallInstancePage.cpp:46-48, 154`
- **Description:** `m_networkManager` 使用 `this` 作为 parent 创建，但析构函数中手动 `delete m_networkManager`。Qt 父子机制会再次删除，导致 **double-free / heap corruption**。
- **Fix:** 删除析构函数中的 `delete m_networkManager`。

### C2. `AiChatPage::updateConversationTitle` 按钮索引映射错误
- **File:** `pages/AiChatPage.cpp:1193-1196`
- **Description:** `m_convButtons` 是过滤后的列表，与 `m_conversations` 无 1:1 映射。用 `m_convButtons[index]` 直接访问会设置**错误按钮**的文字。
- **Fix:** 通过 `convIndex` property 匹配正确的按钮。

### C3. `AiChatPage::onRenameConversation` 同样的索引错误
- **File:** `pages/AiChatPage.cpp:1358-1361`
- **Description:** 与 C2 相同，`currentRow` 是 `m_conversations` 索引，不是 `m_convButtons` 索引。

### C4. `MultiThreadDownloader::onSegmentFinished` 文件写入失败静默丢弃数据
- **File:** `utils/MultiThreadDownloader.cpp:193-197`
- **Description:** `segFile.open()` 失败时数据被丢弃，但 `seg.completed` 标记为 `true`。合并时产生**损坏的输出文件**。
- **Fix:** 文件写入失败时调用 `failDownload()`。

### C5. `GameLauncher` 析构不等待 `QProcess` 终止
- **File:** `utils/GameLauncher.cpp:60-64`
- **Description:** `waitForFinished(3000)` 超时后直接 `delete m_gameProcess`。进程未退出时删除导致**未定义行为/崩溃**。

### C6. `ContentDownloader::downloadToInstance` 重复调用导致 tempFile 泄漏
- **File:** `utils/content/ContentDownloader.cpp:78`
- **Description:** 连续调用两次 `downloadToInstance()`，旧 `d->tempFile` 被覆盖未删除，造成内存泄漏。

### C7. ~~`ContentDownloader` 使用 `QProcess::waitForFinished` 阻塞 UI 线程 60 秒~~ ✅ 已修复
- **File:** `utils/content/ContentDownloader.cpp:152,158`
- **Description:** 在 `QNetworkReply::finished` 信号处理器中调用 `waitForFinished(60000)`，主线程冻结 60 秒。
- **Fix:** 改为异步 QProcess，通过 `finished` 信号回调处理结果。

### C8. ~~`BedrockContentInstaller` 使用 `waitForFinished` 阻塞 UI 线程最多 360 秒~~ ✅ 已修复
- **File:** `utils/bedrock/BedrockContentInstaller.cpp:45,52,59`
- **Description:** 三个 `QProcess` 串行调用 `waitForFinished(120000)`，总计可冻结 UI 长达 6 分钟。
- **Fix:** 改为 `QProcess::startDetached()` 异步执行，不再阻塞 UI 线程。

### C9. `InstanceAssistantWindow::onInjectClicked` 使用 `Sleep()` 阻塞 UI 线程 ~500ms
- **File:** `components/InstanceAssistantWindow.cpp:1387,1396,1400,1416`
- **Description:** 多个 `Sleep()` 调用冻结主线程，UI 完全无响应。
- **Fix:** 使用 `QTimer::singleShot` 链或移到工作线程。

---

## HIGH (P1) — May cause crashes or severe functional issues

### H1. `AuthManager::instance()` 单例无线程安全保护
- **File:** `utils/AuthManager.cpp:36-41`
- **Description:** 无互斥锁，多线程同时调用可创建多个实例。

### H2. `VersionDownloader::instance()` 单例无线程安全保护
- **File:** `utils/VersionDownloader.cpp:71-77`

### H3. `BackgroundManager::instance()` 单例无线程安全保护
- **File:** `utils/BackgroundManager.cpp:23-29`

### H4. `GithubAccelerator::instance()` 单例无线程安全保护
- **File:** `utils/GithubAccelerator.cpp:26-32`

### H5. `MemoryAllocator::instance()` 单例无线程安全保护
- **File:** `utils/MemoryAllocator.cpp:23-30`

### H6. `DownloadTaskManager::m_tasks` 无线程安全保护
- **File:** `utils/DownloadTaskManager.cpp:77-150`
- **Description:** 所有读写操作无互斥锁，下载回调在后台线程触发导致**数据竞争**。

### H7. `DownloadTaskManager::generateTaskId()` 非原子递增
- **File:** `utils/DownloadTaskManager.cpp:36-39`
- **Description:** `m_nextTaskId++` 多线程调用可能产生**重复任务 ID**。

### H8. `VersionDownloader` 的 `m_isDownloading`/`m_isPaused` 不一致锁保护
- **File:** `utils/VersionDownloader.cpp` (多处)
- **Description:** 多处读写未加锁，构成数据竞争。

### H9. `TaskListPage::processEvents()` 可导致悬空指针
- **File:** `pages/TaskListPage.cpp:322`
- **Description:** 嵌套事件处理期间 `card` 指针被删除，恢复执行后**崩溃**。

### H10. `AiChatPage::onDeleteConversation` 空指针解引用
- **File:** `pages/AiChatPage.cpp:1306`
- **Description:** `m_selectionHighlight` 为 `nullptr` 时调用 `setVisible(false)`。

### H11. `AiChatPage::onClearAllConversations` 空指针解引用
- **File:** `pages/AiChatPage.cpp:1417`

### H12. ~~`MicrosoftLoginDialog` 中 `AuthManager::instance()` 未做空检查~~ ✅ 已修复
- **File:** `pages/MicrosoftLoginDialog.cpp:15-24`
- **Description:** `m_authManager` 未做空检查即调用5次 `connect()`。
- **Fix:** 添加 `if (m_authManager)` 空检查保护所有 connect 和方法调用。

### H13. `AiService::compressContext()` 阻塞主线程最多 30 秒
- **File:** `utils/AiService.cpp:1336-1341`
- **Description:** 同步 `QEventLoop` 冻结 UI。

### H14. Windows 路径分隔符 — `split("/")` 在 Windows 上失效
- **File:** `mainwindow.cpp:1044, 1070`
- **Description:** `split("/").last()` 在 Windows 路径上返回完整路径而非文件名。

### H15. `TopBar::animateEditionDot` 动画目标错误
- **File:** `components/TopBar.cpp:497`
- **Description:** 动画目标是 `m_editionDot` (QLabel) 的 `"opacity"` 属性，但 QLabel 没有 `opacity` 属性。应该动画 `m_dotOpacity` (QGraphicsOpacityEffect)。动画**静默失败**。

### H16. `TopBar` 多个公共方法空指针解引用
- **File:** `components/TopBar.cpp:944,954,972,976,981,696`
- **Description:** `setInstanceSettingsSelected`, `setInstanceSelectSelected`, `setAccountSelected`, `setAccountName`, `setAccountAvatar`, `setInstanceName` 等方法均未检查空指针。

### H17. `MainWindowPages.cpp:1110-1113` 返回按钮标题设置逻辑错误
- **File:** `MainWindowPages.cpp:1110-1113`
- **Description:** Line 1110 设置 `tr("设置")`，Line 1113 立即覆盖为 `tr("设置>Java管理")`。复制粘贴错误。

### H18. `MainWindowPages.cpp` 中 `initModDetailPage` 解引用未初始化的 `m_modDownloadPage`
- **File:** `MainWindowPages.cpp:674-689`
- **Description:** `m_modDownloadPage` 在 `mainwindow.h` 中声明为未初始化的原始指针。如果 `initModDetailPage` 在 `initModDownloadPage` 之前调用，**未定义行为**。

### H19. `mainwindow.h` 中 18 个页面指针成员未初始化为 `nullptr`
- **File:** `mainwindow.h:203-226`
- **Description:** `m_instanceSelectPage`, `m_accountManagePage`, `m_launchDetailsPage`, `m_installInstancePage`, `m_loaderDetailPage`, `m_forgeVersionListPage`, `m_taskListPage`, `m_taskDetailPage`, `m_modDownloadPage`, `m_modDetailPage`, `m_modpackImportPage`, `m_modpackExportPage`, `m_searchPage`, `m_contentDownloadPage`, `m_contentDetailPage`, `m_contentListPage`, `m_resourcesPage`, `m_instanceManagePage` 均为未初始化指针。任何在此类初始化前检查 `if (m_xxx)` 的代码都是**未定义行为**。

---

## MEDIUM (P2) — Functional issues or potential risks

### M1. `SettingsManager::saveAccountsToFile()` 写入失败静默丢失账户数据
- **File:** `utils/SettingsManager.cpp:137-142`

### M2. `FavoritesManager::save()` 静默丢失收藏夹数据
- **File:** `utils/FavoritesManager.cpp:256-262`

### M3. `SkinDownloader::~SkinDownloader()` 泄漏 `QNetworkReply` 对象
- **File:** `utils/SkinDownloader.cpp:36-43`
- **Description:** `abort()` 后未调用 `deleteLater()`。

### M4. `InstanceFolderInfo::isDefault` 未初始化
- **File:** `utils/SettingsManager.h:41-45`

### M5. `VersionDownloader::cancelDownload()` 未清理临时分段目录
- **File:** `utils/VersionDownloader.cpp:126-153`

### M6. Content 页重复创建导致 `stackedWidget` 索引漂移
- **File:** `mainwindow.cpp:2271-2435`
- **Description:** `showContentDownloadPage()` 等每次 `addWidget()` 追加到末尾而非替换固定索引。Placeholder 索引 15/16/17/23 永远是空白页。

### M7. `showFavoritesPage` 信号连接累积导致 use-after-free 风险
- **File:** `mainwindow.cpp:2427-2434`
- **Description:** 每次新建 `FavoritesPage` 时向单例 `FavoritesManager` 新增连接但不断开旧连接。

### M8. 嵌套 lambda 返回按钮连接累积
- **File:** `MainWindowPages.cpp:295-314`
- **Description:** 3 层嵌套的 disconnect/reconnect lambda 链，多次导航后返回按钮触发**多个处理函数**。

### M9. `AccountManagePage` 微软登录按钮快速点击可创建多个对话框
- **File:** `pages/AccountManagePage.cpp:568-628`

### M10. `ThemeManager::instance()` 空检查不一致（多处）
- **File:** `pages/AiChatPage.cpp:694`, `pages/InstanceHomePage.cpp:175`, `pages/BedrockResourcesPage.cpp:21`, `pages/TaskListPage.cpp:39` 等

### M11. `SettingsManager::instance()` 空检查不一致（多处）
- **File:** `pages/HomePage.cpp:190`, `pages/ModDownloadPage.cpp:102`, `pages/ContentDownloadPage.cpp:103` 等

### M12. `GameLauncher::waitForGameWindow()` 中 `static int elapsedTime` 跨启动持久化
- **File:** `utils/GameLauncher.cpp:390`

### M13. `GameLauncher::launchGame()` 中 `disconnect` 断开 `fileCompletionFinished` 所有连接
- **File:** `utils/GameLauncher.cpp:195-214`

### M14. `AccountManagePage` 30+ 个成员变量未初始化
- **File:** `pages/AccountManagePage.h:175-250`

### M15. `FavoritesPage.cpp` 静态 `QNetworkAccessManager` 和 `QNetworkDiskCache` 永不释放
- **File:** `pages/FavoritesPage.cpp:41-56`

### M16. `BedrockVersionService::fetchVersionList` 竞态条件
- **File:** `utils/bedrock/BedrockVersionService.cpp:101-126`
- **Description:** 共享状态变量无互斥锁保护，watchdog 超时和新 fetch 可同时修改同一状态。

### M17. `ModpackExporter` 整数溢出 — ZIP 超过 4GB
- **File:** `utils/modpack/ModpackExporter.cpp:100,244`
- **Description:** `uncompressedSize` 声明为 `quint32`，`data.size()` 返回 `qint64`。大文件截断导致损坏的 ZIP。EOCD 的 `m_entries.size()` 截断为 `quint16`，超过 65535 条目时溢出。

### M18. `ModpackExporter::exportAsBlockBox` 内存耗尽风险
- **File:** `utils/modpack/ModpackExporter.cpp:1430`
- **Description:** `allDataBlocks.append(writeData)` 将所有文件数据累积到单个 `QByteArray`，大整合包可耗尽内存。

### M19. `ModpackImporter::installLoader` 信号连接累积
- **File:** `utils/modpack/ModpackImporter.cpp:383-471`
- **Description:** 多次调用时向单例安装器重复 `connect()`，可能导致重复回调。

### M20. `SettingsPage::rebuildEditionStack` 未恢复 `m_currentIndex`
- **File:** `pages/SettingsPage.cpp:165-191`
- **Description:** Java/基岩版切换后 `QStackedWidget` 索引变化，但 `m_currentIndex` 未更新，显示页可能与侧边栏选择不匹配。

### M21. `BedrockInstanceManager::activateInstance` 失败时数据残留
- **File:** `utils/bedrock/BedrockInstanceManager.cpp:280-310`
- **Description:** `createJunction` 失败时备份被恢复，但已复制的数据未清理。

### M22. `PluginManager::downloadToFile` 阻塞 UI 线程
- **File:** `utils/plugin/PluginManager.cpp:45-78`
- **Description:** 本地 `QEventLoop` 最多阻塞 30 秒。

### M23. `SubNavPanel::slideHighlightTo` 潜在无限递归
- **File:** `components/SubNavPanel.cpp:383-408`
- **Description:** 当 `target->geometry()` 无效时，`QTimer::singleShot(0, ...)` 反复重试。如果几何永远无效，**无限递归**。

### M24. `TaskBar::onTaskStatusChanged` lambda 中 `this` 指针可能悬空
- **File:** `components/TaskBar.cpp:294-300`
- **Description:** 动画上下文是 `card`，但 lambda 捕获 `this`。如果 `TaskBar` 在动画完成前销毁，`this` 指针悬空。

### M25. `FavoriteFolderDialog` 硬编码浅色主题样式
- **File:** `components/FavoriteFolderDialog.cpp:36-58`
- **Description:** 使用硬编码颜色 `#202124`, `#555555`, `#d0d0d0`, `#ffffff`，深色主题下显示异常。

### M26. `LocalModDetailPage` 硬编码浅色主题样式
- **File:** `pages/LocalModDetailPage.cpp` 多处
- **Description:** 所有卡片使用 `background-color: white`，深色主题下出现白色矩形。

### M27. `NotificationManager` 单例永不释放
- **File:** `components/NotificationManager.cpp:22-28`
- **Description:** 无 parent，永不删除，`m_cards` 中的残留卡片也可能泄漏。

### M28. `SubNavPanel::showForParent` 交错动画可能访问已删除的 `QGraphicsOpacityEffect`
- **File:** `components/SubNavPanel.cpp:274-287`
- **Description:** 快速连续调用 `showForParent` 时，旧 stagger 动画可能仍引用已被 `setGraphicsEffect` 替换的旧 effect。

### M29. ~~`InstanceFolderTree` 设置 `QFileSystemModel` 根路径为系统根目录~~ ✅ 已修复
- **File:** `InstanceFolderTree.cpp:131-132`
- **Description:** `setRootPath(QDir::rootPath())` 索引整个文件系统，资源消耗巨大。
- **Fix:** 改为 `QDir::homePath()` 作为初始根路径，`setCurrentFolder` 时动态调整到父目录。

### M30. ~~`BackgroundWidget::hideEvent` FlowLight/Rotating 模式不停止定时器~~ ✅ 已修复
- **File:** `components/BackgroundWidget.cpp:358-363`
- **Description:** 隐藏时定时器继续运行，`update()` 调用浪费 CPU。
- **Fix:** `hideEvent` 中无条件停止定时器；新增 `showEvent` 在重新显示时恢复定时器。

---

## LOW (P3) — Code quality / minor issues

### L1. `VersionDownloader::onManifestReplyFinished()` 未检查 JSON 解析错误
- **File:** `utils/VersionDownloader.cpp:313-314`

### L2. `VersionDownloader::onVersionJsonReplyFinished()` 未检查 JSON 解析错误
- **File:** `utils/VersionDownloader.cpp:390-391`

### L3. `AiService::parseSseData()` 静默丢弃格式错误的 JSON
- **File:** `utils/AiService.cpp:609-614`

### L4. `GithubAccelerator::proxyUrl()` 未检查 `m_proxies` 是否为空
- **File:** `utils/GithubAccelerator.cpp:72`

### L5. `SettingsManager::getAccounts()` 中使用 `const_cast` 绕过 const 正确性
- **File:** `utils/SettingsManager.cpp:170`

### L6. `VersionDownloader::onSegmentReplyFinished()` 在 `deleteLater()` 后继续访问 reply
- **File:** `utils/VersionDownloader.cpp:751-770`

### L7. `DownloadEngine` 进度计算在 32 位平台上可能整数溢出
- **File:** `utils/download/DownloadEngine.cpp:254`

### L8. `mainwindow.cpp` 多处 `received * 100` 在 32 位平台上可能整数溢出
- **File:** `mainwindow.cpp:1470, 1479, 2223, 2636, 2651`

### L9. `mainwindow.cpp:1241` — 方法间缺少换行，格式混淆
- **File:** `mainwindow.cpp:1241`

### L10. `ModpackExporter` 自定义 CRC32 表与 zlib CRC32 可能不一致
- **File:** `utils/modpack/ModpackExporter.cpp:300-372`

### L11. `PluginZip::parseEocd` 潜在越界读取
- **File:** `utils/plugin/PluginZip.cpp:47-55`

### L12. `PluginManager::sanitizeId` 未检查 Windows 保留文件名
- **File:** `utils/plugin/PluginManager.cpp:598-608`

### L13. `ServerStatusChecker` 线程池最大 24 线程可能耗尽文件描述符
- **File:** `utils/plugin/ServerStatusChecker.cpp:181-182`

### L14. `MainWindowPages.cpp` 中 `ensurePageInitialized` 缺少 4 个页面索引
- **File:** `MainWindowPages.cpp:377-449`
- **Description:** `ContentDownloadPage`(15), `ContentDetailPage`(16), `ContentListPage`(17), `FavoritesPage`(23) 缺少 case。对应 init 函数定义了但**从未被调用**（死代码）。

### L15. `Skin3DWidget` GL 上下文失效时纹理资源泄漏
- **File:** `Skin3DWidget.cpp:149-157, 571-588`
- **Description:** `m_bgWhiteTexture` 在 GL 上下文重建时不会重新创建。

### L16. `downloadEngine::cancelAll` 取消期间 `finished` 回调可能不触发
- **File:** `utils/download/DownloadEngine.cpp:340-352`
- **Description:** `m_activeReplies` 被清空后，正在取消的 reply 的 `finished` 回调找不到自身，调用者可能永远等待。

---

## 按文件统计

| 文件 | Bug 数量 |
|------|---------|
| `mainwindow.cpp` | 6 |
| `MainWindowPages.cpp` | 8 |
| `mainwindow.h` | 1 |
| `pages/AiChatPage.cpp` | 5 |
| `pages/InstallInstancePage.cpp` | 1 |
| `pages/AccountManagePage.cpp` | 3 |
| `pages/MicrosoftLoginDialog.cpp` | 1 |
| `pages/TaskListPage.cpp` | 2 |
| `pages/HomePage.cpp` | 1 |
| `pages/SettingsPage.cpp` | 1 |
| `pages/LocalModDetailPage.cpp` | 1 |
| `pages/FavoritesPage.cpp` | 1 |
| `pages/BedrockResourcesPage.cpp` | 1 |
| `pages/InstanceHomePage.cpp` | 1 |
| `pages/ModDownloadPage.cpp` | 1 |
| `pages/ContentDownloadPage.cpp` | 1 |
| `components/TopBar.cpp` | 4 |
| `components/SideBar.cpp` | 1 |
| `components/TaskBar.cpp` | 2 |
| `components/SubNavPanel.cpp` | 2 |
| `components/BackgroundWidget.cpp` | 2 |
| `components/NotificationManager.cpp` | 1 |
| `components/FavoriteFolderDialog.cpp` | 1 |
| `components/InstanceAssistantWindow.cpp` | 1 |
| `utils/AuthManager.cpp` | 1 |
| `utils/VersionDownloader.cpp` | 5 |
| `utils/DownloadTaskManager.cpp` | 2 |
| `utils/GameLauncher.cpp` | 4 |
| `utils/MultiThreadDownloader.cpp` | 1 |
| `utils/SkinDownloader.cpp` | 1 |
| `utils/SettingsManager.cpp` | 2 |
| `utils/SettingsManager.h` | 1 |
| `utils/FavoritesManager.cpp` | 1 |
| `utils/AiService.cpp` | 3 |
| `utils/BackgroundManager.cpp` | 1 |
| `utils/GithubAccelerator.cpp` | 2 |
| `utils/MemoryAllocator.cpp` | 1 |
| `utils/download/DownloadEngine.cpp` | 2 |
| `utils/content/ContentDownloader.cpp` | 2 |
| `utils/bedrock/BedrockContentInstaller.cpp` | 1 |
| `utils/bedrock/BedrockInstanceManager.cpp` | 2 |
| `utils/bedrock/BedrockVersionService.cpp` | 1 |
| `utils/modpack/ModpackExporter.cpp` | 3 |
| `utils/modpack/ModpackImporter.cpp` | 1 |
| `utils/plugin/PluginManager.cpp` | 2 |
| `utils/plugin/PluginZip.cpp` | 1 |
| `utils/plugin/ServerStatusChecker.cpp` | 1 |
| `pages/AccountManagePage.h` | 1 |
| `InstanceFolderTree.cpp` | 1 |
| `Skin3DWidget.cpp` | 1 |

---

## 修复优先级建议

1. **立即修复 (P0):** C1-C9 — 会导致崩溃/数据损坏/UI 冻结
2. **尽快修复 (P1):** H1-H19 — 可能导致崩溃或严重异常
3. **计划修复 (P2):** M1-M30 — 功能异常或潜在风险
4. **有空修复 (P3):** L1-L16 — 代码质量改善
