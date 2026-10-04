/**
 * @file   BedrockCommandAssistantPage.cpp
 * @brief  基岩版实例助手 - 快捷指令页面实现
 * @author BlockBox Team
 * @date   2026-08-25
 */
#include "BedrockCommandAssistantPage.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"

BedrockCommandAssistantPage::BedrockCommandAssistantPage(QWidget *parent)
    : QWidget(parent)
    , m_contextLabel(nullptr)
    , m_commandInput(nullptr)
    , m_completionList(nullptr)
    , m_englishOutput(nullptr)
    , m_copyBtn(nullptr)
    , m_injectBtn(nullptr)
    , m_historyBtn(nullptr)
    , m_presetBtn(nullptr)
    , m_commandDb(nullptr)
    , m_translator(nullptr)
    , m_completer(nullptr)
    , m_inputDebounceTimer(nullptr)
{
    // 加载基岩版指令数据库
    m_commandDb = new CommandDatabase();
    if (!m_commandDb->loadFromFile(QStringLiteral(":/resources/bedrock_command_database.json")))
    {
        qWarning() << "BedrockCommandAssistantPage: failed to load bedrock command database";
    }

    m_translator = new CommandTranslator(m_commandDb);
    m_completer = new CommandCompleter(m_commandDb);

    m_commandContext.version = QString();
    m_commandContext.opLevel = 4;
    m_commandContext.isServer = false;

    initUI();
    applyThemeStyles();

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &BedrockCommandAssistantPage::applyThemeStyles);
    connect(ThemeManager::instance(), &ThemeManager::textColorChanged,
            this, &BedrockCommandAssistantPage::applyThemeStyles);

    loadHistory();
}

BedrockCommandAssistantPage::~BedrockCommandAssistantPage()
{
    delete m_translator;
    delete m_completer;
    delete m_commandDb;
}

void BedrockCommandAssistantPage::setInstancePath(const QString &path)
{
    m_instancePath = path;
    if (m_contextLabel)
        m_contextLabel->setText(tr("基岩版 | 单机 | OP: 4"));
}

void BedrockCommandAssistantPage::initUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // 1. 上下文状态栏
    auto *contextBar = new QHBoxLayout();
    contextBar->setSpacing(6);

    m_contextLabel = new QLabel(this);
    m_contextLabel->setObjectName(QStringLiteral("contextLabel"));
    m_contextLabel->setText(tr("基岩版 | 单机 | OP: 4"));

    contextBar->addWidget(m_contextLabel, 1);
    layout->addLayout(contextBar);

    // 2. 中文输入框
    m_commandInput = new QLineEdit(this);
    m_commandInput->setObjectName(QStringLiteral("commandInput"));
    m_commandInput->setPlaceholderText(tr("输入中文指令，如：给予 钻石剑 1"));
    layout->addWidget(m_commandInput);

    // 3. 候选列表
    m_completionList = new QListWidget(this);
    m_completionList->setObjectName(QStringLiteral("completionList"));
    m_completionList->setMinimumHeight(180);
    m_completionList->setMouseTracking(true);
    m_completionList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_completionList, 1);

    // 4. 英文输出框 + 操作按钮
    auto *outputBar = new QHBoxLayout();
    outputBar->setSpacing(6);

    m_englishOutput = new QLineEdit(this);
    m_englishOutput->setObjectName(QStringLiteral("englishOutput"));
    m_englishOutput->setReadOnly(true);
    m_englishOutput->setPlaceholderText(tr("英文指令输出"));

    m_copyBtn = new QPushButton(tr("复制"), this);
    m_copyBtn->setObjectName(QStringLiteral("copyBtn"));
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    m_copyBtn->setEnabled(false);

    m_injectBtn = new QPushButton(tr("一键注入"), this);
    m_injectBtn->setObjectName(QStringLiteral("injectBtn"));
    m_injectBtn->setCursor(Qt::PointingHandCursor);
    m_injectBtn->setEnabled(false);
    m_injectBtn->setToolTip(tr("将指令注入到运行中的基岩版游戏窗口"));

    m_historyBtn = new QPushButton(tr("历史"), this);
    m_historyBtn->setObjectName(QStringLiteral("historyBtn"));
    m_historyBtn->setCursor(Qt::PointingHandCursor);

    m_presetBtn = new QPushButton(tr("预设"), this);
    m_presetBtn->setObjectName(QStringLiteral("presetBtn"));
    m_presetBtn->setCursor(Qt::PointingHandCursor);
    m_presetBtn->setToolTip(tr("常用指令句子预设：按分类一键填入"));

    outputBar->addWidget(m_englishOutput, 1);
    outputBar->addWidget(m_copyBtn);
    outputBar->addWidget(m_injectBtn);
    outputBar->addWidget(m_presetBtn);
    outputBar->addWidget(m_historyBtn);
    layout->addLayout(outputBar);

    // ---- 信号连接 ----
    m_inputDebounceTimer = new QTimer(this);
    m_inputDebounceTimer->setSingleShot(true);
    m_inputDebounceTimer->setInterval(60);
    connect(m_inputDebounceTimer, &QTimer::timeout,
            this, &BedrockCommandAssistantPage::flushDebouncedInput);

    connect(m_commandInput, &QLineEdit::textChanged, this, [this](const QString &) {
        m_inputDebounceTimer->start();
    });

    connect(m_completionList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) return;
        QString insertText = item->data(Qt::UserRole).toString();
        if (insertText.isEmpty()) return;

        QString text = m_commandInput->text();
        int cursor = m_commandInput->cursorPosition();
        int wordStart = cursor;
        while (wordStart > 0 && !text.at(wordStart - 1).isSpace())
            --wordStart;
        QString currentWord = text.mid(wordStart, cursor - wordStart);

        if (currentWord.isEmpty() || insertText.startsWith(currentWord, Qt::CaseInsensitive))
        {
            QString newText = text.left(wordStart) + insertText + text.mid(cursor);
            m_commandInput->setText(newText);
            m_commandInput->setCursorPosition(wordStart + insertText.length());
        }
        else
        {
            QString insertion = QStringLiteral(" ") + insertText + QStringLiteral(" ");
            QString newText = text.left(cursor) + insertion + text.mid(cursor);
            m_commandInput->setText(newText);
            m_commandInput->setCursorPosition(cursor + insertion.length());
        }
        m_commandInput->setFocus();
    });

    connect(m_copyBtn, &QPushButton::clicked, this, &BedrockCommandAssistantPage::onCopyClicked);
    connect(m_injectBtn, &QPushButton::clicked, this, &BedrockCommandAssistantPage::onInjectClicked);

    connect(m_historyBtn, &QPushButton::clicked, this, &BedrockCommandAssistantPage::showHistoryDialog);
    connect(m_presetBtn, &QPushButton::clicked, this, &BedrockCommandAssistantPage::showPresetsMenu);
}

void BedrockCommandAssistantPage::applyThemeStyles()
{
    QString themeColor = ThemeManager::instance()->currentThemeColor();
    QString textColor = ThemeManager::instance()->currentTextColor();
    QString borderColor = ThemeManager::instance()->currentBorderColor();
    QColor tc(themeColor);
    QString hoverBg = QString("rgba(%1, %2, %3, 0.12)")
        .arg(tc.red()).arg(tc.green()).arg(tc.blue());

    // 上下文标签
    m_contextLabel->setStyleSheet(
        QString("QLabel { color: %1; font-size: 11px; background: transparent; border: none; }")
            .arg(textColor));

    // 操作按钮
    QString btnStyle = QString(
        "QPushButton {"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 6px 12px;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "  color: %1;"
        "  background: palette(base);"
        "  border: 1px solid %2;"
        "}"
        "QPushButton:hover {"
        "  background: %3;"
        "}"
        "QPushButton:pressed {"
        "  background: %4;"
        "}"
        "QPushButton:disabled {"
        "  color: %5;"
        "  background: palette(base);"
        "}"
    ).arg(textColor, borderColor, hoverBg,
          QString("rgba(%1, %2, %3, 0.18)").arg(tc.red()).arg(tc.green()).arg(tc.blue()),
          QString("rgba(%1, %2, %3, 0.4)").arg(tc.red()).arg(tc.green()).arg(tc.blue()));

    for (auto *btn : {m_copyBtn, m_injectBtn, m_historyBtn, m_presetBtn})
        btn->setStyleSheet(btnStyle);
}

void BedrockCommandAssistantPage::flushDebouncedInput()
{
    QString input = m_commandInput->text().trimmed();
    if (input == m_lastCompletionInput)
        return;
    m_lastCompletionInput = input;
    refreshCompletion();
    refreshTranslation();
}

void BedrockCommandAssistantPage::refreshCompletion()
{
    m_completionList->clear();
    QString input = m_commandInput->text().trimmed();
    if (input.isEmpty())
        return;

    // 中文指令补全
    QList<CommandInfo> cmdMatches = m_commandDb->findCommandsByChinesePrefix(input);
    for (const CommandInfo &cmd : cmdMatches)
    {
        QString display = cmd.chinese.isEmpty() ? cmd.name : cmd.chinese.first();
        QString tooltip = cmd.syntax;
        auto *item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, display);
        item->setToolTip(tooltip);
        m_completionList->addItem(item);
    }

    // 物品补全
    QList<ItemInfo> itemMatches = m_commandDb->findItemsByChinesePrefix(input);
    for (const ItemInfo &item : itemMatches)
    {
        QString display = item.chinese.isEmpty() ? item.english : item.chinese.first();
        auto *listItem = new QListWidgetItem(display);
        listItem->setData(Qt::UserRole, display);
        listItem->setToolTip(item.english);
        m_completionList->addItem(listItem);
    }

    // 实体补全
    QList<EntityInfo> entityMatches = m_commandDb->findEntitiesByChinesePrefix(input);
    for (const EntityInfo &ent : entityMatches)
    {
        QString display = ent.chinese.isEmpty() ? ent.english : ent.chinese.first();
        auto *item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, display);
        item->setToolTip(ent.english);
        m_completionList->addItem(item);
    }

    // 效果补全
    QList<EffectInfo> effectMatches = m_commandDb->findEffectsByChinesePrefix(input);
    for (const EffectInfo &eff : effectMatches)
    {
        QString display = eff.chinese.isEmpty() ? eff.english : eff.chinese.first();
        auto *item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, display);
        item->setToolTip(eff.english);
        m_completionList->addItem(item);
    }
}

void BedrockCommandAssistantPage::refreshTranslation()
{
    QString input = m_commandInput->text().trimmed();
    if (input.isEmpty())
    {
        m_englishOutput->clear();
        m_copyBtn->setEnabled(false);
        m_injectBtn->setEnabled(false);
        return;
    }

    TranslateResult result = m_translator->translate(input, m_commandContext);
    if (result.success)
    {
        m_englishOutput->setText(result.englishCommand);
        m_copyBtn->setEnabled(true);
        m_injectBtn->setEnabled(true);
    }
    else
    {
        m_englishOutput->clear();
        m_copyBtn->setEnabled(false);
        m_injectBtn->setEnabled(false);
    }
}

void BedrockCommandAssistantPage::onCopyClicked()
{
    QString text = m_englishOutput->text();
    if (!text.isEmpty())
    {
        QApplication::clipboard()->setText(text);
        addHistory(m_commandInput->text().trimmed(), text);
    }
}

void BedrockCommandAssistantPage::onInjectClicked()
{
    QString text = m_englishOutput->text();
    if (text.isEmpty())
        return;

    // 基岩版注入：将指令写入剪贴板，提示用户在游戏中粘贴
    QApplication::clipboard()->setText(text);
    addHistory(m_commandInput->text().trimmed(), text);

    QMessageBox::information(this, tr("指令已复制"),
        tr("基岩版指令已复制到剪贴板。\n请在游戏中按 T 打开聊天栏，粘贴（Ctrl+V）后按回车执行。"));
}

void BedrockCommandAssistantPage::showHistoryDialog()
{
    if (m_history.isEmpty())
    {
        QMessageBox::information(this, tr("历史记录"), tr("暂无历史记录"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("历史记录"));
    dialog.setMinimumSize(350, 300);

    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QListWidget(&dialog);

    for (int i = m_history.size() - 1; i >= 0; --i)
    {
        const auto &pair = m_history[i];
        auto *item = new QListWidgetItem(pair.first + " → " + pair.second);
        item->setData(Qt::UserRole, i);
        list->addItem(item);
    }

    layout->addWidget(list);

    auto *btnLayout = new QHBoxLayout();
    auto *clearBtn = new QPushButton(tr("清空历史"), &dialog);
    auto *closeBtn = new QPushButton(tr("关闭"), &dialog);
    btnLayout->addStretch();
    btnLayout->addWidget(clearBtn);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    connect(list, &QListWidget::itemDoubleClicked, this, [this, &dialog](QListWidgetItem *item) {
        int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_history.size())
        {
            m_commandInput->setText(m_history[idx].first);
            m_englishOutput->setText(m_history[idx].second);
            m_copyBtn->setEnabled(true);
            m_injectBtn->setEnabled(true);
        }
        dialog.accept();
    });

    connect(clearBtn, &QPushButton::clicked, this, [this, &dialog]() {
        m_history.clear();
        saveHistory();
        dialog.accept();
    });

    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();
}

void BedrockCommandAssistantPage::showPresetsMenu()
{
    if (!m_commandDb->isLoaded())
    {
        QMessageBox::information(this, tr("预设"), tr("指令数据库未加载"));
        return;
    }

    QMenu menu(this);
    QStringList categories = m_commandDb->presetCategories();

    for (const QString &cat : categories)
    {
        QMenu *catMenu = menu.addMenu(cat);
        QList<CommandPreset> presets = m_commandDb->findPresetsByCategory(cat);
        for (const CommandPreset &preset : presets)
        {
            QAction *action = catMenu->addAction(preset.chinese);
            action->setToolTip(preset.description);
            connect(action, &QAction::triggered, this, [this, preset]() {
                applyPreset(preset);
            });
        }
    }

    if (categories.isEmpty())
    {
        menu.addAction(tr("暂无可用预设"))->setEnabled(false);
    }

    menu.exec(QCursor::pos());
}

void BedrockCommandAssistantPage::applyPreset(const CommandPreset &preset)
{
    m_commandInput->setText(preset.chinese);
    m_englishOutput->setText(preset.english);
    m_copyBtn->setEnabled(true);
    m_injectBtn->setEnabled(true);
}

void BedrockCommandAssistantPage::addHistory(const QString &input, const QString &output)
{
    if (input.isEmpty() || output.isEmpty())
        return;

    // 去重：移除相同输入的历史
    for (int i = m_history.size() - 1; i >= 0; --i)
    {
        if (m_history[i].first == input)
            m_history.removeAt(i);
    }

    m_history.append({input, output});

    // 限制最大历史记录数
    while (m_history.size() > 100)
        m_history.removeFirst();

    saveHistory();
}

void BedrockCommandAssistantPage::loadHistory()
{
    m_history.clear();
    QString path = QCoreApplication::applicationDirPath()
        + QStringLiteral("/bedrock_command_history.json");
    QFile file(path);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray())
        return;

    QJsonArray arr = doc.array();
    for (const QJsonValue &val : arr)
    {
        QJsonObject obj = val.toObject();
        QString input = obj.value("input").toString();
        QString output = obj.value("output").toString();
        if (!input.isEmpty() && !output.isEmpty())
            m_history.append({input, output});
    }
}

void BedrockCommandAssistantPage::saveHistory()
{
    QJsonArray arr;
    for (const auto &pair : m_history)
    {
        QJsonObject obj;
        obj["input"] = pair.first;
        obj["output"] = pair.second;
        arr.append(obj);
    }

    QString path = QCoreApplication::applicationDirPath()
        + QStringLiteral("/bedrock_command_history.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;

    file.write(QJsonDocument(arr).toJson());
    file.close();
}
