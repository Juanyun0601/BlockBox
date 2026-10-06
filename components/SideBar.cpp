/**
 * @file   SideBar.cpp
 * @brief  侧边栏组件实现 - 手风琴式层级导航
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "SideBar.h"

#include <QDebug>
#include <QScrollArea>
#include <QSpacerItem>
#include <QPainter>
#include <QStyle>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QScrollBar>
#include <QShowEvent>

#include "utils/SettingsManager.h"
#include "utils/IconHelper.h"
#include "utils/FavoritesManager.h"
#include "utils/plugin/PluginManager.h"
#include "utils/ThemeManager.h"

SideBar::SideBar(QWidget *parent)
    : QWidget(parent)
    , m_selectedParentIndex(0)
    , m_iconNormalColor("#666666")
    , m_iconSelectedColor("#10B981")
    , m_expandBtn(nullptr)
{
    setAttribute(Qt::WA_StyledBackground);

    // 选中图标初始取当前主题主色
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    if (themeColor.isValid())
        m_iconSelectedColor = themeColor;

    initUI();
}

SideBar::~SideBar()
{
}

void SideBar::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Create scroll area for navigation items
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Create scroll content widget
    m_scrollContent = new QWidget(m_scrollArea);
    m_scrollLayout = new QVBoxLayout(m_scrollContent);
    m_scrollLayout->setContentsMargins(0, 8, 0, 8);
    m_scrollLayout->setSpacing(6);

    m_selectionHighlight = new QWidget(m_scrollContent);
    m_selectionHighlight->setObjectName("sidebarSelectionHighlight");
    m_selectionHighlight->setVisible(false);
    m_selectionHighlight->lower();

    // Build navigation structure and render parent items
    buildNavStructure();
    renderParentItems();

    m_scrollArea->setWidget(m_scrollContent);
    m_mainLayout->addWidget(m_scrollArea);

    // 子导航收起后的"展开"按钮（默认隐藏，子导航面板收起时显示）
    m_expandBtn = new QPushButton(this);
    m_expandBtn->setObjectName("sidebarExpandBtn");
    m_expandBtn->setFixedSize(60, 56);
    m_expandBtn->setIcon(QIcon(":/Images/Icons/expand.svg"));
    m_expandBtn->setIconSize(QSize(20, 20));
    m_expandBtn->setCursor(Qt::PointingHandCursor);
    m_expandBtn->setToolTip(tr("展开子导航"));
    m_expandBtn->setVisible(false);
    connect(m_expandBtn, &QPushButton::clicked, this, &SideBar::expandRequested);
    m_mainLayout->addWidget(m_expandBtn, 0, Qt::AlignHCenter);

    // Set initial selection state (button checked + icons only, no highlight animation yet)
    m_selectedParentIndex = 0;
    for (QPushButton *button : m_parentButtons)
    {
        if (button->property("parentIndex").toInt() == m_selectedParentIndex)
        {
            button->setChecked(true);
            break;
        }
    }
    refreshNavIcons();

    // 跟随主题主色刷新选中图标颜色
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged, this, [this](const QString &color) {
        QColor c(color);
        if (c.isValid())
            m_iconSelectedColor = c;
        refreshNavIcons();
    });
}

void SideBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!event->spontaneous())
    {
        initSelectionHighlight();
        animateEntrance();
    }
}

void SideBar::buildNavStructure()
{
    SettingsManager *settings = SettingsManager::instance();

    m_parentItems.clear();

    // 首页 - 无子项
    if (settings->isHomeButtonVisible())
    {
        NavItem home;
        home.text = tr("首页");
        home.iconPath = ":/Images/Icons/nav_home.svg";
        home.isParentItem = true;
        m_parentItems.append(home);
    }

    // 资源 - 有子项（含分组），子导航内容随版本（Java版/基岩版）切换
    if (settings->isResourcesButtonVisible())
    {
        NavItem resources;
        resources.text = tr("资源");
        resources.iconPath = ":/Images/Icons/nav_resources.svg";
        resources.isParentItem = true;

        if (m_bedrockMode)
        {
            // ── 基岩版资源子导航：新游戏 / 社区资源 / 收藏夹 ──
            // 新游戏分组
            NavItem newGameSection;
            newGameSection.text = tr("新游戏");
            newGameSection.isSectionLabel = true;

            NavItem installVersion;
            installVersion.text = tr("安装新实例");
            installVersion.iconPath = ":/Images/Icons/nav_install.svg";
            newGameSection.children.append(installVersion);

            resources.children.append(newGameSection);

            // 社区资源分组
            NavItem communitySection;
            communitySection.text = tr("社区资源");
            communitySection.isSectionLabel = true;

            NavItem rp;
            rp.text = tr("资源包");
            rp.iconPath = ":/Images/Icons/nav_resourcepacks.svg";
            communitySection.children.append(rp);

            NavItem tp;
            tp.text = tr("材质包");
            tp.iconPath = ":/Images/Icons/nav_textures.svg";
            communitySection.children.append(tp);

            NavItem map;
            map.text = tr("地图");
            map.iconPath = ":/Images/Icons/nav_worlds.svg";
            communitySection.children.append(map);

            NavItem script;
            script.text = tr("脚本");
            script.iconPath = ":/Images/Icons/nav_scripts.svg";
            communitySection.children.append(script);

            resources.children.append(communitySection);
        }
        else
        {
            // 新游戏分组
            NavItem newGameSection;
            newGameSection.text = tr("新游戏");
            newGameSection.isSectionLabel = true;

            NavItem installInstance;
            installInstance.text = tr("安装新实例");
            installInstance.iconPath = ":/Images/Icons/nav_install.svg";
            newGameSection.children.append(installInstance);

            NavItem downloadModpack;
            downloadModpack.text = tr("下载整合包");
            downloadModpack.iconPath = ":/Images/Icons/nav_download.svg";
            newGameSection.children.append(downloadModpack);

            NavItem importModpack;
            importModpack.text = tr("导入整合包");
            importModpack.iconPath = ":/Images/Icons/nav_import.svg";
            newGameSection.children.append(importModpack);

            resources.children.append(newGameSection);

            // 社区资源分组
            NavItem gameResSection;
            gameResSection.text = tr("社区资源");
            gameResSection.isSectionLabel = true;

            NavItem mods;
            mods.text = tr("模组");
            mods.iconPath = ":/Images/Icons/nav_mods.svg";
            gameResSection.children.append(mods);

            NavItem datapacks;
            datapacks.text = tr("数据包");
            datapacks.iconPath = ":/Images/Icons/nav_datapacks.svg";
            gameResSection.children.append(datapacks);

            NavItem resourcepacks;
            resourcepacks.text = tr("资源包");
            resourcepacks.iconPath = ":/Images/Icons/nav_resourcepacks.svg";
            gameResSection.children.append(resourcepacks);

            NavItem shaders;
            shaders.text = tr("光影包");
            shaders.iconPath = ":/Images/Icons/nav_shaders.svg";
            gameResSection.children.append(shaders);

            NavItem worlds;
            worlds.text = tr("世界");
            worlds.iconPath = ":/Images/Icons/nav_worlds.svg";
            gameResSection.children.append(worlds);

            resources.children.append(gameResSection);
        }

        // 收藏夹分组（Java/基岩版独立存储，SideBar 读取当前版本的收藏夹列表）
        NavItem favSection;
        favSection.text = tr("收藏夹");
        favSection.isSectionLabel = true;

        NavItem newFav;
        newFav.text = tr("新建收藏夹");
        newFav.iconPath = ":/Images/Icons/nav_folder_plus.svg";
        favSection.children.append(newFav);

        // 动态读取当前版本模式的收藏夹列表
        QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();
        for (const FavoriteFolder &f : folders)
        {
            NavItem favItem;
            favItem.text = f.name;
            favItem.iconPath = ":/Images/Icons/nav_folder.svg";
            // 通过 property 携带分组 ID，便于 MainWindow 在点击时获取
            favItem.data = f.id;
            favSection.children.append(favItem);
        }

        resources.children.append(favSection);

        m_parentItems.append(resources);
    }

    // 设置 - 有子项（无分组）
    if (settings->isSettingsButtonVisible())
    {
        NavItem settings;
        settings.text = tr("设置");
        settings.iconPath = ":/Images/Icons/nav_settings.svg";
        settings.isParentItem = true;

        NavItem general;
        general.text = tr("常规设置");
        general.iconPath = ":/Images/Icons/nav_general.svg";
        settings.children.append(general);

        NavItem appearance;
        appearance.text = tr("界面设置");
        appearance.iconPath = ":/Images/Icons/nav_appearance.svg";
        settings.children.append(appearance);

        NavItem game;
        game.text = tr("全局游戏设置");
        game.iconPath = ":/Images/Icons/nav_game.svg";
        settings.children.append(game);

        if (!m_bedrockMode)
        {
            NavItem instance;
            instance.text = tr("实例设置");
            instance.iconPath = ":/Images/Icons/nav_instance.svg";
            settings.children.append(instance);

            NavItem java;
            java.text = tr("Java管理");
            java.iconPath = ":/Images/Icons/nav_java.svg";
            settings.children.append(java);

            NavItem advanced;
            advanced.text = tr("高级设置");
            advanced.iconPath = ":/Images/Icons/nav_advanced.svg";
            settings.children.append(advanced);
        }

        NavItem keyBind;
        keyBind.text = tr("按键绑定");
        keyBind.iconPath = ":/Images/Icons/nav_advanced.svg";
        settings.children.append(keyBind);

        NavItem systemInfo;
        systemInfo.text = tr("系统信息");
        systemInfo.iconPath = ":/Images/Icons/nav_system.svg";
        settings.children.append(systemInfo);

        NavItem aiAssistant;
        aiAssistant.text = tr("AI 助手");
        aiAssistant.iconPath = ":/Images/Icons/nav_ai.svg";
        settings.children.append(aiAssistant);

        m_parentItems.append(settings);
    }

    // AI 助手 - 无子项
    {
        NavItem ai;
        ai.text = tr("AI 助手");
        ai.iconPath = ":/Images/Icons/nav_ai.svg";
        ai.isParentItem = true;
        m_parentItems.append(ai);
    }

    // 实例管理 - 有子项（含分组），不在侧边栏显示，通过顶部实例图标进入
    {
        NavItem instanceManage;
        instanceManage.text = tr("实例管理");
        instanceManage.iconPath = ":/Images/Icons/nav_instance.svg";
        instanceManage.isParentItem = true;
        instanceManage.hideInSidebar = true;

        // 管理分组
        NavItem manageSection;
        manageSection.text = tr("管理");
        manageSection.isSectionLabel = true;

        NavItem overview;
        overview.text = tr("概览");
        overview.iconPath = ":/Images/Icons/overview.svg";
        manageSection.children.append(overview);

        NavItem instSettings;
        instSettings.text = tr("设置");
        instSettings.iconPath = ":/Images/Icons/instance_settings.svg";
        manageSection.children.append(instSettings);

        NavItem modify;
        modify.text = tr("修改");
        modify.iconPath = ":/Images/Icons/modify.svg";
        manageSection.children.append(modify);

        NavItem exportPack;
        exportPack.text = tr("导出整合包");
        exportPack.iconPath = ":/Images/Icons/export.svg";
        manageSection.children.append(exportPack);

        NavItem lanTransfer;
        lanTransfer.text = tr("文件传输");
        lanTransfer.iconPath = ":/Images/Icons/share.svg";
        manageSection.children.append(lanTransfer);

        instanceManage.children.append(manageSection);

        // 资源分组
        NavItem resourceSection;
        resourceSection.text = tr("资源");
        resourceSection.isSectionLabel = true;

        NavItem saves;
        saves.text = tr("存档");
        saves.iconPath = ":/Images/Icons/nav_worlds.svg";
        resourceSection.children.append(saves);

        NavItem server;
        server.text = tr("服务器");
        server.iconPath = ":/Images/Icons/server.svg";
        resourceSection.children.append(server);

        NavItem projection;
        projection.text = tr("投影");
        projection.iconPath = ":/Images/Icons/list.svg";
        resourceSection.children.append(projection);

        NavItem screenshot;
        screenshot.text = tr("截图");
        screenshot.iconPath = ":/Images/Icons/screenshot.svg";
        resourceSection.children.append(screenshot);

        NavItem mods;
        mods.text = tr("模组");
        mods.iconPath = ":/Images/Icons/nav_mods.svg";
        resourceSection.children.append(mods);

        NavItem resourcepack;
        resourcepack.text = tr("资源包");
        resourcepack.iconPath = ":/Images/Icons/nav_resourcepacks.svg";
        resourceSection.children.append(resourcepack);

        NavItem shader;
        shader.text = tr("光影包");
        shader.iconPath = ":/Images/Icons/nav_shaders.svg";
        resourceSection.children.append(shader);

        instanceManage.children.append(resourceSection);

        // 日志分组
        NavItem logSection;
        logSection.text = tr("日志");
        logSection.isSectionLabel = true;

        NavItem logItem;
        logItem.text = tr("日志");
        logItem.iconPath = ":/Images/Icons/nav_log.svg";
        logSection.children.append(logItem);

        instanceManage.children.append(logSection);

        m_parentItems.append(instanceManage);
    }

    // 插件 - 有子项（含分组）：插件列表动态读取 + 底部工具
    {
        NavItem plugin;
        plugin.text = tr("插件");
        plugin.iconPath = ":/Images/Icons/nav_plugin.svg";
        plugin.isParentItem = true;

        // 插件列表分组（动态读取已安装插件）
        NavItem listSection;
        listSection.text = tr("插件列表");
        listSection.isSectionLabel = true;

        // 插件列表总览入口（点击回到插件总览页）
        NavItem overviewItem;
        overviewItem.text = tr("插件列表");
        overviewItem.iconPath = ":/Images/Icons/nav_plugin.svg";
        overviewItem.data = QStringLiteral("__overview__");
        listSection.children.append(overviewItem);

        const QList<PluginInfo> plugins = PluginManager::instance()->plugins();
        if (plugins.isEmpty())
        {
            NavItem empty;
            empty.text = tr("暂无插件");
            empty.iconPath = ":/Images/Icons/plugin.svg";
            listSection.children.append(empty);
        }
        else
        {
            for (const PluginInfo &p : plugins)
            {
                NavItem item;
                item.text = p.name;
                const QString iconFile = PluginManager::instance()->pluginIconPath(p);
                if (!iconFile.isEmpty())
                {
                    item.iconPath = iconFile;
                    item.iconIsSvg = false;
                }
                else
                {
                    item.iconPath = ":/Images/Icons/plugin.svg";
                }
                item.data = p.id;
                listSection.children.append(item);
            }
        }

        plugin.children.append(listSection);

        // 工具分组（位于子导航底部）
        NavItem toolSection;
        toolSection.text = tr("工具");
        toolSection.isSectionLabel = true;

        NavItem createItem;
        createItem.text = tr("制作插件");
        createItem.iconPath = ":/Images/Icons/plugin_create.svg";
        toolSection.children.append(createItem);

        NavItem importItem;
        importItem.text = tr("导入插件");
        importItem.iconPath = ":/Images/Icons/nav_import.svg";
        toolSection.children.append(importItem);

        plugin.children.append(toolSection);

        m_parentItems.append(plugin);
    }
}

void SideBar::renderParentItems()
{
    m_parentButtons.clear();
    m_parentIconLabels.clear();
    m_parentTextLabels.clear();

    for (int i = 0; i < m_parentItems.size(); ++i)
    {
        const NavItem &item = m_parentItems[i];

        if (item.hideInSidebar)
        {
            continue;
        }

        QPushButton *button = new QPushButton(m_scrollContent);
        button->setObjectName("parentNavButton");
        button->setFixedSize(60, 56);
        button->setCheckable(true);
        button->setProperty("parentIndex", i);
        button->setProperty("iconPath", item.iconPath);
        button->setCursor(Qt::PointingHandCursor);

        auto *btnLayout = new QVBoxLayout(button);
        btnLayout->setContentsMargins(0, 6, 0, 4);
        btnLayout->setSpacing(2);

        QLabel *iconLabel = new QLabel(button);
        iconLabel->setObjectName("parentNavIconLabel");
        iconLabel->setFixedSize(24, 24);
        iconLabel->setAlignment(Qt::AlignCenter);

        QLabel *textLabel = new QLabel(item.text, button);
        textLabel->setObjectName("parentNavTextLabel");
        textLabel->setAlignment(Qt::AlignCenter);

        btnLayout->addWidget(iconLabel, 0, Qt::AlignHCenter);
        btnLayout->addWidget(textLabel, 0, Qt::AlignHCenter);

        if (!item.iconPath.isEmpty())
        {
            iconLabel->setPixmap(loadColoredIcon(item.iconPath, m_iconNormalColor).pixmap(24, 24));
        }

        connect(button, &QPushButton::clicked, this, &SideBar::onButtonClicked);

        m_parentButtons.append(button);
        m_parentIconLabels.append(iconLabel);
        m_parentTextLabels.append(textLabel);
        m_scrollLayout->addWidget(button, 0, Qt::AlignHCenter);
    }

    // Add spacer to push items to top
    m_scrollLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));
}

void SideBar::onButtonClicked()
{
    QPushButton *clickedButton = qobject_cast<QPushButton*>(sender());
    if (!clickedButton) return;

    QVariant parentVar = clickedButton->property("parentIndex");
    if (parentVar.isValid())
    {
        int parentIndex = parentVar.toInt();
        setSelectedIndex(parentIndex);
        emit parentItemClicked(parentIndex);
    }
}

void SideBar::setSelectedIndex(int index)
{
    for (QPushButton *button : m_parentButtons)
    {
        button->setChecked(false);
    }

    for (QPushButton *button : m_parentButtons)
    {
        if (button->property("parentIndex").toInt() == index)
        {
            button->setChecked(true);
            break;
        }
    }
    m_selectedParentIndex = index;
    refreshNavIcons();

    slideHighlightTo(index);
}

void SideBar::refreshNavStructure()
{
    int savedIndex = m_selectedParentIndex;

    // 清空布局中的所有项（按钮 + 间隔符）
    while (QLayoutItem *item = m_scrollLayout->takeAt(0))
    {
        if (QWidget *w = item->widget())
        {
            w->deleteLater();
        }
        delete item;
    }
    m_parentButtons.clear();

    buildNavStructure();
    renderParentItems();

    if (savedIndex >= 0 && savedIndex < m_parentButtons.size())
    {
        m_selectedParentIndex = savedIndex;
        m_parentButtons[savedIndex]->setChecked(true);
        refreshNavIcons();
    }
}

void SideBar::refreshNavIcons()
{
    // Refresh parent button icons
    for (int i = 0; i < m_parentButtons.size(); ++i)
    {
        QPushButton *button = m_parentButtons[i];
        if (!button) continue;

        QString iconPath = button->property("iconPath").toString();
        if (iconPath.isEmpty())
        {
            continue;
        }

        bool isSelected = button->isChecked();
        QColor color = isSelected ? m_iconSelectedColor : m_iconNormalColor;

        if (i < m_parentIconLabels.size() && m_parentIconLabels[i])
        {
            m_parentIconLabels[i]->setPixmap(loadColoredIcon(iconPath, color).pixmap(24, 24));
        }
    }
}

void SideBar::clearSelection()
{
    m_selectionHighlight->setVisible(false);
    for (QPushButton *button : m_parentButtons)
    {
        button->setChecked(false);
    }
    m_selectedParentIndex = -1;
    refreshNavIcons();
}

void SideBar::setNavIconColor(const QColor &color)
{
    m_iconNormalColor = color;
    refreshNavIcons();
}

void SideBar::setExpandButtonVisible(bool visible)
{
    if (m_expandBtn)
        m_expandBtn->setVisible(visible);
}

void SideBar::setUpdateButton(QWidget *button)
{
    if (!button)
        return;
    button->setParent(this);
    button->hide();
    // 插到"展开"按钮上方，带少量上下边距，避免与导航项贴死
    QWidget *container = new QWidget(this);
    QVBoxLayout *containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 6, 0, 10);
    containerLayout->addWidget(button, 0, Qt::AlignHCenter);
    const int index = m_expandBtn ? m_mainLayout->indexOf(m_expandBtn)
                                  : m_mainLayout->count();
    m_mainLayout->insertWidget(index, container, 0, Qt::AlignHCenter);
}

void SideBar::setBedrockMode(bool bedrock)
{
    if (m_bedrockMode == bedrock)
        return;
    m_bedrockMode = bedrock;
    refreshNavStructure();
}

void SideBar::initSelectionHighlight()
{
    if (m_parentButtons.isEmpty()) return;

    for (QPushButton *btn : m_parentButtons)
    {
        if (btn->property("parentIndex").toInt() == m_selectedParentIndex)
        {
            m_selectionHighlight->setGeometry(indicatorRect(btn));
            m_selectionHighlight->setVisible(true);
            m_selectionHighlight->lower();
            break;
        }
    }
}

void SideBar::slideHighlightTo(int buttonIndex)
{
    QPushButton *target = nullptr;
    for (QPushButton *btn : m_parentButtons)
    {
        if (btn->property("parentIndex").toInt() == buttonIndex)
        {
            target = btn;
            break;
        }
    }
    if (!target) return;

    m_selectionHighlight->setVisible(true);

    auto *anim = new QPropertyAnimation(m_selectionHighlight, "geometry");
    anim->setDuration(250);
    anim->setStartValue(m_selectionHighlight->geometry());
    anim->setEndValue(indicatorRect(target));
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

QRect SideBar::indicatorRect(QPushButton *btn) const
{
    // 指示条贴靠侧边栏最左缘，垂直居中于按钮
    return QRect(0, btn->y() + (btn->height() - 24) / 2, 3, 24);
}

void SideBar::animateEntrance()
{
    auto *group = new QSequentialAnimationGroup(this);

    for (QPushButton *btn : m_parentButtons)
    {
        auto *effect = new QGraphicsOpacityEffect(btn);
        effect->setOpacity(0.0);
        btn->setGraphicsEffect(effect);

        auto *anim = new QPropertyAnimation(effect, "opacity");
        anim->setDuration(120);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);

        group->addPause(25);
        group->addAnimation(anim);
    }

    connect(group, &QSequentialAnimationGroup::finished, this, [this]()
    {
        for (QPushButton *btn : m_parentButtons)
        {
            if (btn)
            {
                btn->setGraphicsEffect(nullptr);
            }
        }
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

QIcon SideBar::loadColoredIcon(const QString &path, const QColor &color) const
{
    QIcon baseIcon = IconHelper::loadColoredIcon(path, color, 20);
    if (baseIcon.isNull()) return QIcon();

    QPixmap icon = baseIcon.pixmap(20, 20);
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.drawPixmap(2, 2, icon);
    painter.end();

    return QIcon(pixmap);
}