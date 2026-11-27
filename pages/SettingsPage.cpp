/**
 * @file   SettingsPage.cpp
 * @brief  设置页面实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "SettingsPage.h"

#include <QDebug>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOption>
#include <QToolTip>

#include "utils/BackgroundManager.h"
#include "utils/HomeCarouselStore.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/MemoryAllocator.h"
#include "utils/Schematic/ProjectionEditorSettings.h"
#include "components/CollapsibleSectionCard.h"
#include "components/OutlinedLabel.h"
#include "settings/SettingsKeyBindPage.h"
#include "settings/SettingsBedrockGamePage.h"

#include "platform.h"

namespace {

/// 手机端把内容包进滚动区：内容装不下时上下滑动查看。
/// 桌面端布局保持原样（窗口足够高，不出现滚动条）。
QWidget *scrollableOnMobile(QWidget *content)
{
    if (!Platform::isMobile())
        return content;
    auto *scroll = new QScrollArea(content->parentWidget());
    scroll->setObjectName(QStringLiteral("settingsSubPageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    return scroll;
}

} // namespace

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent), m_currentIndex(0), m_forgeDownloadSourceCombo(nullptr), m_fabricDownloadSourceCombo(nullptr), m_optiFineDownloadSourceCombo(nullptr), m_neoForgeDownloadSourceCombo(nullptr), m_backgroundModeCombo(nullptr), m_solidColorSection(nullptr), m_imageSection(nullptr), m_solidColorBtn(nullptr), m_imagePathEdit(nullptr), m_browseImageBtn(nullptr), m_javaListWidget(nullptr), m_javaEmptyLabel(nullptr), m_globalJavaCombo(nullptr), m_autoSelectJavaCheck(nullptr), m_systemInfoSettings(nullptr), m_aboutSettings(nullptr)
{
    initUI();
}

SettingsPage::~SettingsPage()
{
}

void SettingsPage::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    m_rightFrame = new QFrame(this);
    m_rightFrame->setObjectName("settingsRightFrame");
    m_rightLayout = new QVBoxLayout(m_rightFrame);
    m_rightLayout->setContentsMargins(0, 0, 0, 0);
    m_rightLayout->setSpacing(0);
    initRightContent();
    m_mainLayout->addWidget(m_rightFrame);
}

void SettingsPage::initRightContent()
{
    m_contentStack = new QStackedWidget(m_rightFrame);
    m_contentStack->setObjectName("settingsContentStack");
    m_generalSettings = new QWidget(m_contentStack);
    m_generalSettings->setObjectName("settingsGeneralContent");
    m_interfaceSettings = new QWidget(m_contentStack);
    m_interfaceSettings->setObjectName("settingsInterfaceContent");
    m_globalGameSettings = new QWidget(m_contentStack);
    m_globalGameSettings->setObjectName("settingsGameContent");
    m_bedrockGameSettings = new QWidget(m_contentStack);
    m_bedrockGameSettings->setObjectName("settingsBedrockGameContent");
    m_instanceSettings = new QWidget(m_contentStack);
    m_instanceSettings->setObjectName("settingsInstanceContent");
    m_javaManagerSettings = new QWidget(m_contentStack);
    m_javaManagerSettings->setObjectName("settingsJavaContent");
    m_advancedSettings = new QWidget(m_contentStack);
    m_advancedSettings->setObjectName("settingsAdvancedContent");
    m_keyBindSettings = new QWidget(m_contentStack);
    m_keyBindSettings->setObjectName("settingsKeyBindContent");
    m_systemInfoSettings = new QWidget(m_contentStack);
    m_systemInfoSettings->setObjectName("settingsSystemInfoContent");
    m_aiAssistantSettings = new QWidget(m_contentStack);
    m_aiAssistantSettings->setObjectName("settingsAiAssistantContent");
    m_aboutSettings = new QWidget(m_contentStack);
    m_aboutSettings->setObjectName("settingsAboutContent");
    m_projectionEditorSettings = new QWidget(m_contentStack);
    m_projectionEditorSettings->setObjectName("settingsProjectionEditorContent");
    // 操作设置是手机 / 平板专属：桌面端没有触屏与手柄操作，不创建这一页
    if (Platform::isMobile()) {
        m_controlSettings = new QWidget(m_contentStack);
        m_controlSettings->setObjectName("settingsControlContent");
    }
    initGeneralSettings();
    initInterfaceSettings();
    initGlobalGameSettings();
    initBedrockGameSettings();
    initInstanceSettings();
    initJavaManagerSettings();
    initAdvancedSettings();
    initKeyBindSettings();
    initSystemInfoSettings();
    initAiAssistantSettings();
    initAboutSettings();
    initProjectionEditorSettings();
    if (m_controlSettings)
        initControlSettings();
    m_contentStack->addWidget(m_generalSettings);
    m_contentStack->addWidget(m_interfaceSettings);
    m_contentStack->addWidget(m_globalGameSettings);
    m_contentStack->addWidget(m_bedrockGameSettings);
    m_contentStack->addWidget(m_instanceSettings);
    m_contentStack->addWidget(m_javaManagerSettings);
    m_contentStack->addWidget(m_advancedSettings);
    m_contentStack->addWidget(m_keyBindSettings);
    m_contentStack->addWidget(m_systemInfoSettings);
    m_contentStack->addWidget(m_aiAssistantSettings);
    m_contentStack->addWidget(m_aboutSettings);
    m_contentStack->addWidget(m_projectionEditorSettings);
    if (m_controlSettings)
        m_contentStack->addWidget(m_controlSettings);
    // 按当前版本模式重建顺序，保证页签索引与侧边栏一致
    rebuildEditionStack();
    // 手机端：内容堆叠整体套一层滚动区，子设置页装不下时上下滑动查看
    m_rightLayout->addWidget(scrollableOnMobile(m_contentStack));
}

void SettingsPage::onNavItemClicked(int index)
{
    m_contentStack->setCurrentIndex(index);
    m_currentIndex = index;
    resetSettingsScroll();
    // 手机端：整页滚动区回到顶部，切换子页时从页首开始看
    if (auto *scroll = findChild<QScrollArea *>(QStringLiteral("settingsSubPageScroll")))
        if (auto *bar = scroll->verticalScrollBar())
            bar->setValue(0);
}

void SettingsPage::resetSettingsScroll()
{
    QWidget *w = parentWidget();
    while (w) {
        if (auto *scrollArea = qobject_cast<QScrollArea*>(w)) {
            if (QScrollBar *bar = scrollArea->verticalScrollBar())
                bar->setValue(0);
            return;
        }
        w = w->parentWidget();
    }
}

void SettingsPage::onRestoreDefaults()
{
    // 按当前页面判断，而不是按页签序号：Java / 基岩版模式的页签顺序不同，
    // 新增子页后序号会整体后移，顺序判断很容易恢复错页面。
    QWidget *page = m_contentStack ? m_contentStack->currentWidget() : nullptr;
    if (!page)
        return;

    if (page == m_interfaceSettings) {
        qDebug() << "[SettingsPage]" << "恢复界面设置默认值";
        ThemeManager::instance()->setSidebarBgColor("");
        ThemeManager::instance()->resetColorOverrides();
        HomeCarouselStore::clearCustom();
        refreshHomeCarouselRows();
        emit carouselImagesChanged();
    } else if (page == m_advancedSettings) {
        qDebug() << "[SettingsPage]" << "恢复高级设置默认值";
        SettingsManager::instance()->setForumBaseUrl(QString());
    } else if (page == m_aiAssistantSettings) {
        qDebug() << "[SettingsPage]" << "恢复AI助手设置默认值";
        SettingsManager::instance()->setProperty("ai/customSystemPrompt/chat", QString());
        SettingsManager::instance()->setProperty("ai/customSystemPrompt/work", QString());
        SettingsManager::instance()->setProperty("ai/systemPromptLibrary", QString());
        SettingsManager::instance()->setProperty("ai/defaultModelId", QString());
        refreshAiDefaultModelCombo();
    } else if (page == m_projectionEditorSettings) {
        qDebug() << "[SettingsPage]" << "恢复投影编辑器设置默认值";
        ProjectionEditorSettings::resetToDefaults();
        refreshProjectionEditorSettingsControls();
    }
}

void SettingsPage::onThemeChanged(int index)
{
    switch (index) {
    case 0: ThemeManager::instance()->loadTheme(ThemeManager::LightTheme); break;
    case 1: ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme); break;
    case 2: ThemeManager::instance()->loadTheme(ThemeManager::CustomTheme); break;
    default: break;
    }
}

void SettingsPage::onBackgroundModeChanged(int index)
{
    auto mode = static_cast<BackgroundManager::BackgroundMode>(index);
    BackgroundManager::instance()->setMode(mode);
}

void SettingsPage::setCurrentSettingsTab(int index)
{
    if (index >= 0 && index < m_contentStack->count()) {
        m_contentStack->setCurrentIndex(index);
        m_currentIndex = index;
        // 切到 AI 助手子页时刷新默认模型下拉（模型可能在别处增删）
        if (m_contentStack->currentWidget() == m_aiAssistantSettings) {
            refreshAiDefaultModelCombo();
        }
        // 子导航搜索词保持生效：应用到新切换到的设置面板
        if (!m_pageFilterQuery.isEmpty())
            m_panelFilter.apply(m_contentStack->currentWidget(), m_pageFilterQuery);
    }
    // 切换设置子页时重置外层滚动位置，避免新页面停留在上一子页的滚动位置
    resetSettingsScroll();
}

int SettingsPage::tabIndex(SettingsTabId id) const
{
    if (!m_contentStack)
        return -1;

    QWidget *page = nullptr;
    switch (id) {
    case GeneralTab: page = m_generalSettings; break;
    case InterfaceTab: page = m_interfaceSettings; break;
    case GameTab: page = m_bedrockMode ? m_bedrockGameSettings : m_globalGameSettings; break;
    case InstanceTab: page = m_instanceSettings; break;
    case JavaTab: page = m_javaManagerSettings; break;
    case AdvancedTab: page = m_advancedSettings; break;
    case KeyBindTab: page = m_keyBindSettings; break;
    case SystemInfoTab: page = m_systemInfoSettings; break;
    case AiAssistantTab: page = m_aiAssistantSettings; break;
    case AboutTab: page = m_aboutSettings; break;
    case ProjectionEditorTab: page = m_projectionEditorSettings; break;
    case ControlTab: page = m_controlSettings; break;
    }

    return page ? m_contentStack->indexOf(page) : -1;
}

QString SettingsPage::tabTitle(int index) const
{
    if (!m_contentStack)
        return QString();

    QWidget *page = m_contentStack->widget(index);
    if (!page)
        return QString();

    if (page == m_generalSettings) return tr("设置>常规设置");
    if (page == m_interfaceSettings) return tr("设置>界面设置");
    if (page == m_globalGameSettings || page == m_bedrockGameSettings)
        return tr("设置>全局游戏设置");
    if (page == m_controlSettings) return tr("设置>操作设置");
    if (page == m_instanceSettings) return tr("设置>实例设置");
    if (page == m_javaManagerSettings) return tr("设置>Java管理");
    if (page == m_advancedSettings) return tr("设置>高级设置");
    if (page == m_keyBindSettings) return tr("设置>按键绑定");
    if (page == m_systemInfoSettings) return tr("设置>系统信息");
    if (page == m_aiAssistantSettings) return tr("设置>AI 助手");
    if (page == m_aboutSettings) return tr("设置>关于");
    if (page == m_projectionEditorSettings) return tr("设置>投影编辑器");
    return QString();
}

int SettingsPage::pluginManageTabIndex() const
{
    // 插件管理不在内容堆栈里：它的子导航序号排在所有设置内容页之后
    return m_contentStack ? m_contentStack->count() : -1;
}

bool SettingsPage::isPluginManageTab(int index) const
{
    return index >= 0 && index == pluginManageTabIndex();
}

void SettingsPage::applyPageFilter(const QString &query)
{
    m_pageFilterQuery = query;
    QWidget *panel = m_contentStack ? m_contentStack->currentWidget() : nullptr;
    if (query.isEmpty())
        m_panelFilter.clear();
    else
        m_panelFilter.apply(panel, query);
}

void SettingsPage::setBedrockMode(bool bedrock)
{
    if (m_bedrockMode == bedrock)
        return;
    m_bedrockMode = bedrock;
    rebuildEditionStack();
    updateGameSettingsForEdition();
    // 基岩版模式切回后保证停留在有效页签
    if (m_currentIndex >= m_contentStack->count())
        m_contentStack->setCurrentIndex(m_contentStack->count() - 1);
}

void SettingsPage::rebuildEditionStack()
{
    m_contentStack->removeWidget(m_generalSettings);
    m_contentStack->removeWidget(m_interfaceSettings);
    m_contentStack->removeWidget(m_globalGameSettings);
    m_contentStack->removeWidget(m_bedrockGameSettings);
    m_contentStack->removeWidget(m_instanceSettings);
    m_contentStack->removeWidget(m_javaManagerSettings);
    m_contentStack->removeWidget(m_advancedSettings);
    m_contentStack->removeWidget(m_keyBindSettings);
    m_contentStack->removeWidget(m_systemInfoSettings);
    m_contentStack->removeWidget(m_aiAssistantSettings);
    m_contentStack->removeWidget(m_aboutSettings);
    m_contentStack->removeWidget(m_projectionEditorSettings);
    if (m_controlSettings)
        m_contentStack->removeWidget(m_controlSettings);

    m_contentStack->addWidget(m_generalSettings);
    m_contentStack->addWidget(m_interfaceSettings);
    if (m_bedrockMode) {
        m_contentStack->addWidget(m_bedrockGameSettings);
    } else {
        m_contentStack->addWidget(m_globalGameSettings);
    }
    // 操作设置紧跟游戏设置，和侧边栏顺序保持一致
    if (m_controlSettings)
        m_contentStack->addWidget(m_controlSettings);
    if (!m_bedrockMode) {
        m_contentStack->addWidget(m_instanceSettings);
        m_contentStack->addWidget(m_javaManagerSettings);
        m_contentStack->addWidget(m_advancedSettings);
    }
    m_contentStack->addWidget(m_keyBindSettings);
    m_contentStack->addWidget(m_systemInfoSettings);
    m_contentStack->addWidget(m_aiAssistantSettings);
    m_contentStack->addWidget(m_aboutSettings);
    // 投影编辑器为 Java 版内置功能，仅 Java 版设置页保留该项
    if (!m_bedrockMode)
        m_contentStack->addWidget(m_projectionEditorSettings);

    // 重建后 Java/基岩版子项顺序不同，旧 m_currentIndex 不再对应正确页签。
    // 复位到"常规设置"（0），并交由外层（onChildNavClicked）重新导航到目标子项。
    m_currentIndex = 0;
    if (m_contentStack->count() > 0)
        m_contentStack->setCurrentIndex(0);
}

void SettingsPage::initBedrockGameSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_bedrockGameSettings);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    SettingsBedrockGamePage *bedrockGamePage = new SettingsBedrockGamePage(m_bedrockGameSettings);
    layout->addWidget(bedrockGamePage);

    // 管理实例：请求跳转到基岩版实例选择页
    QWidget *w = window();
    if (w) {
        connect(bedrockGamePage, &SettingsBedrockGamePage::manageInstancesRequested,
                w, [w]() {
            QMetaObject::invokeMethod(w, "showBedrockInstanceSelectPage", Qt::QueuedConnection);
        });
    }
}

void SettingsPage::updateGameSettingsForEdition()
{
    if (m_gameInstallPathCard)
        m_gameInstallPathCard->setVisible(!m_bedrockMode);
    if (m_gameJavaPathCard)
        m_gameJavaPathCard->setVisible(!m_bedrockMode);
    if (m_gameMemoryCard)
        m_gameMemoryCard->setVisible(!m_bedrockMode);
}

void SettingsPage::addHelpTooltip(QBoxLayout *layout, QLabel *label, const QString &text)
{
    label->setToolTip(text);
    
    QToolButton *helpBtn = new QToolButton();
    helpBtn->setText("i");
    helpBtn->setObjectName("helpButton");
    helpBtn->setToolTip(text);
    helpBtn->setCursor(Qt::PointingHandCursor);
    helpBtn->setFixedSize(18, 18);
    
    connect(helpBtn, &QToolButton::clicked, [text, helpBtn]() {
        QToolTip::showText(helpBtn->mapToGlobal(QPoint(helpBtn->width() + 4, -4)), text, helpBtn, QRect(), 5000);
    });
    
    for (int i = 0; i < layout->count(); ++i) {
        if (layout->itemAt(i)->widget() == label) {
            layout->insertWidget(i + 1, helpBtn, 0, Qt::AlignVCenter);
            return;
        }
    }
    layout->addWidget(helpBtn);
}

void SettingsPage::disableWheelEffect(QWidget *widget)
{
    widget->installEventFilter(this);
}

QVBoxLayout *SettingsPage::createSettingsCard(QBoxLayout *parentLayout,
                                              const QString &title,
                                              bool startCollapsed)
{
    auto *card = new CollapsibleSectionCard(title, startCollapsed);
    parentLayout->addWidget(card);
    return card->contentLayout();
}

QFrame *SettingsPage::createSettingsHeader(QBoxLayout *parentLayout, const QString &title)
{
    auto *card = new QFrame();
    card->setObjectName(QStringLiteral("settingsHeaderCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *hLayout = new QHBoxLayout(card);
    hLayout->setContentsMargins(24, 12, 24, 12);
    hLayout->setSpacing(12);

    auto *titleLabel = new OutlinedLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("sectionTitle"));
    hLayout->addWidget(titleLabel);
    hLayout->addStretch();

    auto *restoreBtn = new QPushButton(tr("恢复默认值"), card);
    restoreBtn->setObjectName(QStringLiteral("restoreDefaultsBtn"));
    connect(restoreBtn, &QPushButton::clicked, this, &SettingsPage::onRestoreDefaults);
    hLayout->addWidget(restoreBtn);

    parentLayout->addWidget(card);
    return card;
}

QHBoxLayout *SettingsPage::appendSettingRow(QVBoxLayout *cardLayout,
                                            const QString &title,
                                            const QString &desc,
                                            const QString &help,
                                            bool isLast)
{
    QFrame *row = new QFrame();
    row->setObjectName("settingRow");
    row->setAttribute(Qt::WA_StyledBackground, true);
    if (isLast) {
        row->setProperty("lastRow", true);
    }

    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(24, 16, 24, 16);
    rowLayout->setSpacing(20);

    QWidget *info = new QWidget(row);
    info->setObjectName("settingInfo");
    QVBoxLayout *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);

    if (!title.isEmpty()) {
        QHBoxLayout *titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(6);

        QLabel *titleLabel = new QLabel(title, info);
        titleLabel->setObjectName("settingTitle");
        titleRow->addWidget(titleLabel);

        if (!help.isEmpty()) {
            addHelpTooltip(titleRow, titleLabel, help);
        }
        titleRow->addStretch();
        infoLayout->addLayout(titleRow);
    }

    if (!desc.isEmpty()) {
        QLabel *descLabel = new QLabel(desc, info);
        descLabel->setObjectName("settingDesc");
        descLabel->setWordWrap(true);
        infoLayout->addWidget(descLabel);
    }

    rowLayout->addWidget(info, 1);

    cardLayout->addWidget(row);
    return rowLayout;
}

QVBoxLayout *SettingsPage::appendSettingBlock(QVBoxLayout *cardLayout,
                                              const QString &title,
                                              const QString &desc,
                                              const QString &help,
                                              bool isLast)
{
    QFrame *row = new QFrame();
    row->setObjectName("settingRow");
    row->setAttribute(Qt::WA_StyledBackground, true);
    if (isLast) {
        row->setProperty("lastRow", true);
    }

    QVBoxLayout *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(24, 16, 24, 16);
    rowLayout->setSpacing(10);

    if (!title.isEmpty()) {
        QHBoxLayout *titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(6);

        QLabel *titleLabel = new QLabel(title, row);
        titleLabel->setObjectName("settingTitle");
        titleRow->addWidget(titleLabel);

        if (!help.isEmpty()) {
            addHelpTooltip(titleRow, titleLabel, help);
        }
        titleRow->addStretch();
        rowLayout->addLayout(titleRow);
    }

    if (!desc.isEmpty()) {
        QLabel *descLabel = new QLabel(desc, row);
        descLabel->setObjectName("settingDesc");
        descLabel->setWordWrap(true);
        rowLayout->addWidget(descLabel);
    }

    cardLayout->addWidget(row);
    return rowLayout;
}

bool SettingsPage::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        if (qobject_cast<QComboBox*>(obj) || qobject_cast<QSlider*>(obj)) {
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void SettingsPage::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(event);
}

void SettingsPage::retranslateUi()
{
    const auto labels = findChildren<QLabel *>();
    for (QLabel *label : labels) {
        QString text = label->text();
        if (!text.isEmpty() && label->objectName() != "sectionTitle") {
            QString translated = tr(text.toUtf8().constData());
            if (translated != text) {
                label->setText(translated);
            }
        }
    }

    const auto groups = findChildren<QGroupBox *>();
    for (QGroupBox *group : groups) {
        QString title = group->title();
        if (!title.isEmpty()) {
            QString translated = tr(title.toUtf8().constData());
            if (translated != title) {
                group->setTitle(translated);
            }
        }
    }

    const auto buttons = findChildren<QPushButton *>();
    for (QPushButton *btn : buttons) {
        QString text = btn->text();
        if (!text.isEmpty()) {
            QString translated = tr(text.toUtf8().constData());
            if (translated != text) {
                btn->setText(translated);
            }
        }
    }

    const auto checks = findChildren<QCheckBox *>();
    for (QCheckBox *cb : checks) {
        QString text = cb->text();
        if (!text.isEmpty()) {
            QString translated = tr(text.toUtf8().constData());
            if (translated != text) {
                cb->setText(translated);
            }
        }
    }
}

void SettingsPage::initKeyBindSettings()
{
  QVBoxLayout *layout = new QVBoxLayout(m_keyBindSettings);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  SettingsKeyBindPage *keyBindPage = new SettingsKeyBindPage(m_keyBindSettings);
  layout->addWidget(keyBindPage);

  // 按键绑定变更时通知 MainWindow 重新加载快捷键
  QWidget *w = window();
  if (w)
  {
    connect(keyBindPage, &SettingsKeyBindPage::bindingsChanged,
            w, [w]() {
      QMetaObject::invokeMethod(w, "reloadShortcuts", Qt::QueuedConnection);
    });

    // 绑定捕获期间禁用快捷键，避免快捷键触发导致绑定失败
    connect(keyBindPage, &SettingsKeyBindPage::captureStarted, w, [w]() {
      QMetaObject::invokeMethod(w, "setShortcutsEnabled", Qt::DirectConnection, Q_ARG(bool, false));
    });
    connect(keyBindPage, &SettingsKeyBindPage::captureFinished, w, [w]() {
      QMetaObject::invokeMethod(w, "setShortcutsEnabled", Qt::DirectConnection, Q_ARG(bool, true));
    });
  }
}

void SettingsPage::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QWidget::paintEvent(event);
}