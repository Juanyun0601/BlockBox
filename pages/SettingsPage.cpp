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
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/MemoryAllocator.h"
#include "components/CollapsibleSectionCard.h"
#include "components/OutlinedLabel.h"
#include "settings/SettingsKeyBindPage.h"
#include "settings/SettingsBedrockGamePage.h"

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
    // 按当前版本模式重建顺序，保证页签索引与侧边栏一致
    rebuildEditionStack();
    m_rightLayout->addWidget(m_contentStack);
}

void SettingsPage::onNavItemClicked(int index)
{
    m_contentStack->setCurrentIndex(index);
    m_currentIndex = index;
    resetSettingsScroll();
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
    if (m_bedrockMode) {
        switch (m_currentIndex) {
        case 0: qDebug() << "[SettingsPage]" << "恢复常规设置默认值"; break;
        case 1:
            qDebug() << "[SettingsPage]" << "恢复界面设置默认值";
            ThemeManager::instance()->setSidebarBgColor("");
            ThemeManager::instance()->resetColorOverrides();
            break;
    case 2: qDebug() << "[SettingsPage]" << "恢复全局游戏设置默认值"; break;
    case 3: qDebug() << "[SettingsPage]" << "恢复按键绑定默认值"; break;
    case 4: qDebug() << "[SettingsPage]" << "恢复系统信息设置默认值"; break;
    case 5:
        qDebug() << "[SettingsPage]" << "恢复AI助手设置默认值";
        SettingsManager::instance()->setProperty("ai/customSystemPrompt/chat", QString());
        SettingsManager::instance()->setProperty("ai/customSystemPrompt/work", QString());
        SettingsManager::instance()->setProperty("ai/systemPromptLibrary", QString());
        break;
        default: break;
        }
        return;
    }

    switch (m_currentIndex) {
    case 0: qDebug() << "[SettingsPage]" << "恢复常规设置默认值"; break;
    case 1:
        qDebug() << "[SettingsPage]" << "恢复界面设置默认值";
        ThemeManager::instance()->setSidebarBgColor("");
        ThemeManager::instance()->resetColorOverrides();
        break;
    case 2: qDebug() << "[SettingsPage]" << "恢复全局游戏设置默认值"; break;
    case 3: qDebug() << "[SettingsPage]" << "恢复实例设置默认值"; break;
    case 4: qDebug() << "[SettingsPage]" << "恢复Java管理设置默认值"; break;
    case 5: qDebug() << "[SettingsPage]" << "恢复高级设置默认值"; break;
    case 6: qDebug() << "[SettingsPage]" << "恢复按键绑定默认值"; break;
    case 7: qDebug() << "[SettingsPage]" << "恢复系统信息设置默认值"; break;
    case 8:
        qDebug() << "[SettingsPage]" << "恢复AI助手设置默认值";
        SettingsManager::instance()->setProperty("ai/customSystemPrompt/chat", QString());
        SettingsManager::instance()->setProperty("ai/customSystemPrompt/work", QString());
        SettingsManager::instance()->setProperty("ai/systemPromptLibrary", QString());
        break;
    default: break;
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
    }
    // 切换设置子页时重置外层滚动位置，避免新页面停留在上一子页的滚动位置
    resetSettingsScroll();
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

    m_contentStack->addWidget(m_generalSettings);
    m_contentStack->addWidget(m_interfaceSettings);
    if (m_bedrockMode) {
        m_contentStack->addWidget(m_bedrockGameSettings);
    } else {
        m_contentStack->addWidget(m_globalGameSettings);
    }
    if (!m_bedrockMode) {
        m_contentStack->addWidget(m_instanceSettings);
        m_contentStack->addWidget(m_javaManagerSettings);
        m_contentStack->addWidget(m_advancedSettings);
    }
    m_contentStack->addWidget(m_keyBindSettings);
    m_contentStack->addWidget(m_systemInfoSettings);
    m_contentStack->addWidget(m_aiAssistantSettings);
    m_contentStack->addWidget(m_aboutSettings);

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