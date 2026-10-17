/**
 * @file   WorldConvertDialog.cpp
 * @brief  存档版本转换弹窗实现
 * @author BlockBox Team
 * @date   2026-10-17
 */
#include "WorldConvertDialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "utils/ThemeManager.h"

using WorldConverter::Direction;
using WorldConverter::WorldEdition;

namespace {

/** 「自动匹配」在下拉中的存储值 */
const char kAutoFormat[] = "INPUT";

} // namespace

WorldConvertDialog::WorldConvertDialog(const QString &worldDir, const QString &worldName,
                                       const QString &instancePath, QWidget *parent)
    : AppDialogBase(parent)
    , m_worldDir(worldDir)
    , m_worldName(worldName)
    , m_instancePath(instancePath)
{
    setObjectName(QStringLiteral("worldConvertDialog"));
    setWindowTitle(tr("存档版本转换"));
    setMinimumSize(720, 640);

    m_task = new WorldConverter::WorldConvertTask(this);
    connect(m_task, &WorldConverter::WorldConvertTask::progress,
            this, &WorldConvertDialog::onTaskProgress);
    connect(m_task, &WorldConverter::WorldConvertTask::statusChanged,
            this, &WorldConvertDialog::onTaskStatus);
    connect(m_task, &WorldConverter::WorldConvertTask::logLine,
            this, &WorldConvertDialog::onTaskLog);
    connect(m_task, &WorldConverter::WorldConvertTask::finished,
            this, &WorldConvertDialog::onTaskFinished);

    m_engineDownloader = new WorldConverter::WorldEngineDownloader(this);
    connect(m_engineDownloader, &WorldConverter::WorldEngineDownloader::progress,
            this, &WorldConvertDialog::onEngineProgress);
    connect(m_engineDownloader, &WorldConverter::WorldEngineDownloader::finished,
            this, &WorldConvertDialog::onEngineDownloaded);

    initUI();

    // 根据存档内容自动识别方向（db/ = 基岩，region 等 = Java）
    const WorldEdition edition = WorldConverter::detectWorldEdition(m_worldDir);
    if (edition == WorldEdition::Bedrock)
        m_rbB2J->setChecked(true);
    else
        m_rbJ2B->setChecked(true);
    onDirectionChanged();

    m_outputEdit->setText(defaultOutputDir());
    onRescanJava();
}

WorldConvertDialog::~WorldConvertDialog() = default;

void WorldConvertDialog::initUI()
{
    // 居中卡片固定白底浅色配色（不随深色主题变暗），灰底遮罩由基类绘制
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString cardBg = QStringLiteral("#ffffff");
    const QString cardBorder = QStringLiteral("#e3e3e8");
    const QString textColor = QStringLiteral("#1c1c1e");
    const QString subColor = QStringLiteral("#6e6e73");
    const QString inputBg = QStringLiteral("#f4f4f7");
    const QString warnBg = QStringLiteral("#fff8db");
    const QString warnFg = QStringLiteral("#965a00");

    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("worldConvertCard"));
    card->setMinimumWidth(640);
    card->setMaximumWidth(820);
    card->setStyleSheet(QStringLiteral(
        "QWidget#worldConvertCard { background: %1; border: 1px solid %2;"
        " border-radius: 16px; }").arg(cardBg, cardBorder));

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 24, 28, 22);
    cardLayout->setSpacing(12);

    // ── 标题 ──
    QLabel *titleLabel = new QLabel(tr("Java 版 ⇄ 基岩版 存档互转"), card);
    titleLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 16px; font-weight: 700;").arg(textColor));
    cardLayout->addWidget(titleLabel);

    QLabel *subTitle = new QLabel(
        tr("基于 Chunker 转换引擎 · 需 Java 17+ · 首次使用会在线下载引擎（约 32MB）"), card);
    subTitle->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 12px;").arg(subColor));
    cardLayout->addWidget(subTitle);

    // ── 安全提示横幅 ──
    QLabel *warnLabel = new QLabel(
        tr("⚠ 请在转换前先备份存档，谨防存档丢失；转换不保证稳定性，"
           "可能导致方块/内容丢失或存档异常。"), card);
    warnLabel->setWordWrap(true);
    warnLabel->setStyleSheet(QStringLiteral(
        "background: %1; color: %2; border: 1px solid rgba(150, 90, 0, 70);"
        " border-radius: 8px; padding: 8px 12px; font-size: 12px; font-weight: 600;")
        .arg(warnBg, warnFg));
    cardLayout->addWidget(warnLabel);

    // ── 转换方向 ──
    QHBoxLayout *dirLayout = new QHBoxLayout();
    dirLayout->setSpacing(16);
    QLabel *dirTitle = new QLabel(tr("转换方向"), card);
    dirTitle->setFixedWidth(72);
    dirTitle->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(textColor));
    dirLayout->addWidget(dirTitle);

    m_rbJ2B = new QRadioButton(tr("Java 版 → 基岩版"), card);
    m_rbB2J = new QRadioButton(tr("基岩版 → Java 版"), card);
    const QString radioStyle = QStringLiteral(
        "QRadioButton { color: %1; font-size: 13px; spacing: 6px; }"
        "QRadioButton::indicator { width: 16px; height: 16px; }"
        "QRadioButton::indicator:checked { background: %2; border: 4px solid %2; }"
        "QRadioButton::indicator:unchecked { border: 1px solid #c7c7cc; border-radius: 8px;"
        " background: #ffffff; }")
        .arg(textColor, themeColor);
    m_rbJ2B->setStyleSheet(radioStyle);
    m_rbB2J->setStyleSheet(radioStyle);
    dirLayout->addWidget(m_rbJ2B);
    dirLayout->addWidget(m_rbB2J);
    dirLayout->addStretch();
    cardLayout->addLayout(dirLayout);
    connect(m_rbJ2B, &QRadioButton::toggled, this,
            &WorldConvertDialog::onDirectionChanged);

    // ── 输入存档 ──
    QHBoxLayout *inLayout = new QHBoxLayout();
    inLayout->setSpacing(8);
    QLabel *inTitle = new QLabel(tr("输入存档"), card);
    inTitle->setFixedWidth(72);
    inTitle->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(textColor));
    inLayout->addWidget(inTitle);

    m_inputEdit = new QLineEdit(m_worldName + QStringLiteral("  ·  ") + m_worldDir, card);
    m_inputEdit->setReadOnly(true);
    m_inputEdit->setToolTip(m_worldDir);
    m_inputEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3;"
        " border-radius: 8px; padding: 6px 10px; font-size: 12px; }")
        .arg(inputBg, textColor, cardBorder));
    inLayout->addWidget(m_inputEdit, 1);
    cardLayout->addLayout(inLayout);

    // ── 输出位置 ──
    QHBoxLayout *outLayout = new QHBoxLayout();
    outLayout->setSpacing(8);
    QLabel *outTitle = new QLabel(tr("输出位置"), card);
    outTitle->setFixedWidth(72);
    outTitle->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(textColor));
    outLayout->addWidget(outTitle);

    m_outputEdit = new QLineEdit(card);
    m_outputEdit->setPlaceholderText(tr("选择空文件夹或新路径"));
    m_outputEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3;"
        " border-radius: 8px; padding: 6px 10px; font-size: 12px; }")
        .arg(inputBg, textColor, cardBorder));
    outLayout->addWidget(m_outputEdit, 1);

    auto makeFlatBtn = [&](const QString &text) {
        QPushButton *b = new QPushButton(text, card);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
            " border-radius: 8px; padding: 6px 14px; font-size: 12px; }"
            "QPushButton:hover { background-color: rgba(128, 128, 128, 40); }")
            .arg(cardBg, textColor, cardBorder));
        return b;
    };

    QPushButton *browseBtn = makeFlatBtn(tr("浏览…"));
    connect(browseBtn, &QPushButton::clicked, this, &WorldConvertDialog::onBrowseOutput);
    outLayout->addWidget(browseBtn);
    cardLayout->addLayout(outLayout);

    // ── 目标版本 ──
    QHBoxLayout *fmtLayout = new QHBoxLayout();
    fmtLayout->setSpacing(8);
    QLabel *fmtTitle = new QLabel(tr("目标版本"), card);
    fmtTitle->setFixedWidth(72);
    fmtTitle->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(textColor));
    fmtLayout->addWidget(fmtTitle);

    m_formatCombo = new QComboBox(card);
    m_formatCombo->setMinimumHeight(32);
    m_formatCombo->setStyleSheet(QStringLiteral(
        "QComboBox { background: %1; color: %2; border: 1px solid %3;"
        " border-radius: 8px; padding: 4px 10px; font-size: 12px; }"
        "QComboBox QAbstractItemView { background: %1; color: %2;"
        " selection-background-color: %4; }")
        .arg(cardBg, textColor, cardBorder, themeColor));
    fmtLayout->addWidget(m_formatCombo, 1);
    cardLayout->addLayout(fmtLayout);

    // ── 打包 + 质量选项 ──
    QHBoxLayout *optLayout = new QHBoxLayout();
    optLayout->setSpacing(14);
    const QString checkStyle = QStringLiteral(
        "QCheckBox { color: %1; font-size: 12px; spacing: 5px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px;"
        " border: 1px solid #c7c7cc; background: #ffffff; }"
        "QCheckBox::indicator:checked { background: %2; border-color: %2; }")
        .arg(textColor, themeColor);

    m_pkgBox = new QCheckBox(tr("打包 .mcworld/.zip"), card);
    m_pkgBox->setChecked(true);
    m_pkgBox->setToolTip(tr("转基岩版打包为 .mcworld（可直接导入游戏），转 Java 版打包为 .zip"));
    m_pkgBox->setStyleSheet(checkStyle);
    optLayout->addWidget(m_pkgBox);

    m_emptyBox = new QCheckBox(tr("丢弃空区块"), card);
    m_emptyBox->setChecked(true);
    m_emptyBox->setToolTip(tr("空区块被丢弃，游戏加载后自动重新生成地形，避免输出出现空洞"));
    m_emptyBox->setStyleSheet(checkStyle);
    optLayout->addWidget(m_emptyBox);

    m_compactBox = new QCheckBox(tr("LevelDB 压缩"), card);
    m_compactBox->setChecked(true);
    m_compactBox->setToolTip(tr("转基岩版时压缩 LevelDB 数据库，输出更紧凑健康"));
    m_compactBox->setStyleSheet(checkStyle);
    optLayout->addWidget(m_compactBox);

    m_contentBox = new QCheckBox(tr("保留内容"), card);
    m_contentBox->setChecked(true);
    m_contentBox->setToolTip(tr("转换地图、物品、方块连接与战利品表，减少内容丢失"));
    m_contentBox->setStyleSheet(checkStyle);
    optLayout->addWidget(m_contentBox);

    m_yBlendBox = new QCheckBox(tr("生物群系修正"), card);
    m_yBlendBox->setChecked(false);
    m_yBlendBox->setToolTip(tr("避免 Java 1.18+ Y 轴生物群系混合导致的空洞（Java→基岩版生效）"));
    m_yBlendBox->setStyleSheet(checkStyle);
    optLayout->addWidget(m_yBlendBox);
    optLayout->addStretch();
    cardLayout->addLayout(optLayout);

    // ── Java 运行时 ──
    QHBoxLayout *javaLayout = new QHBoxLayout();
    javaLayout->setSpacing(8);
    QLabel *javaTitle = new QLabel(tr("Java 运行时"), card);
    javaTitle->setFixedWidth(72);
    javaTitle->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(textColor));
    javaLayout->addWidget(javaTitle);

    m_javaLabel = new QLabel(card);
    m_javaLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 12px; background: %2; border: 1px solid %3;"
        " border-radius: 8px; padding: 6px 10px;").arg(subColor, inputBg, cardBorder));
    m_javaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    javaLayout->addWidget(m_javaLabel, 1);

    m_javaRescanBtn = makeFlatBtn(tr("重新检测"));
    connect(m_javaRescanBtn, &QPushButton::clicked,
            this, &WorldConvertDialog::onRescanJava);
    javaLayout->addWidget(m_javaRescanBtn);

    m_javaPickBtn = makeFlatBtn(tr("手动选择"));
    connect(m_javaPickBtn, &QPushButton::clicked,
            this, &WorldConvertDialog::onPickJava);
    javaLayout->addWidget(m_javaPickBtn);
    cardLayout->addLayout(javaLayout);

    // ── 进度 ──
    m_progressBar = new QProgressBar(card);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFixedHeight(18);
    m_progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: %1; border: none; border-radius: 9px;"
        " text-align: center; font-size: 11px; color: %2; }"
        "QProgressBar::chunk { background: %3; border-radius: 9px; }")
        .arg(inputBg, textColor, themeColor));
    cardLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(tr("就绪"), card);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 12px;").arg(subColor));
    cardLayout->addWidget(m_statusLabel);

    // ── 日志 ──
    m_logEdit = new QTextEdit(card);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumHeight(140);
    m_logEdit->setStyleSheet(QStringLiteral(
        "QTextEdit { background: %1; color: #2c2c2e; border: 1px solid %2;"
        " border-radius: 8px; padding: 8px; font-family: Consolas, 'Courier New', monospace;"
        " font-size: 11px; }").arg(inputBg, cardBorder));
    cardLayout->addWidget(m_logEdit, 1);

    // ── 按钮行 ──
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);
    btnLayout->setContentsMargins(0, 4, 0, 0);

    m_openOutBtn = makeFlatBtn(tr("打开输出文件夹"));
    m_openOutBtn->setEnabled(false);
    connect(m_openOutBtn, &QPushButton::clicked,
            this, &WorldConvertDialog::onOpenOutputClicked);
    btnLayout->addWidget(m_openOutBtn);
    btnLayout->addStretch();

    m_closeBtn = makeFlatBtn(tr("关闭"));
    connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::close);
    btnLayout->addWidget(m_closeBtn);

    m_cancelBtn = new QPushButton(tr("取消"), card);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 12px; padding: 8px 22px; font-size: 13px; }"
        "QPushButton:disabled { color: %4; }"
        "QPushButton:hover:enabled { background-color: rgba(128, 128, 128, 40); }")
        .arg(cardBg, textColor, cardBorder, subColor));
    connect(m_cancelBtn, &QPushButton::clicked,
            this, &WorldConvertDialog::onCancelClicked);
    btnLayout->addWidget(m_cancelBtn);

    m_startBtn = new QPushButton(tr("开始转换"), card);
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: #ffffff; border: none;"
        " border-radius: 12px; padding: 8px 26px; font-size: 13px; font-weight: 600; }"
        "QPushButton:disabled { background-color: #c9c9c9; color: #f5f5f5; }")
        .arg(themeColor));
    connect(m_startBtn, &QPushButton::clicked,
            this, &WorldConvertDialog::onStartClicked);
    btnLayout->addWidget(m_startBtn);

    cardLayout->addLayout(btnLayout);
    rootLayout->addWidget(card, 0, Qt::AlignCenter);
}

void WorldConvertDialog::rebuildFormatCombo()
{
    m_formatCombo->clear();
    m_formatCombo->addItem(tr("自动匹配（与输入版本一致）"), QLatin1String(kAutoFormat));

    const bool j2b = m_rbJ2B->isChecked();
    if (j2b)
    {
        const QStringList versions = WorldConverter::bedrockVersions();
        for (const QString &v : versions)
        {
            const QString id = WorldConverter::formatIdForBedrock(v);
            m_formatCombo->addItem(
                tr("基岩版 %1").arg(v), id);
        }
    }
    else
    {
        const QStringList versions = WorldConverter::javaVersions();
        for (const QString &v : versions)
        {
            const QString id = WorldConverter::formatIdForJava(v);
            m_formatCombo->addItem(tr("Java版 %1").arg(v), id);
        }
    }
}

void WorldConvertDialog::onDirectionChanged()
{
    if (!m_rbJ2B || !m_rbB2J)
        return;
    rebuildFormatCombo();
    // 生物群系修正仅 Java→基岩 有意义
    m_yBlendBox->setEnabled(m_rbJ2B->isChecked());
    // LevelDB 压缩仅转基岩版有意义
    m_compactBox->setEnabled(m_rbJ2B->isChecked());
}

void WorldConvertDialog::updateJavaLabel()
{
    if (m_javaExe.isEmpty())
    {
        m_javaLabel->setText(tr("未找到 Java 17+，请安装 Java 或点击「手动选择」"));
    }
    else
    {
        m_javaLabel->setText(m_javaExe);
        m_javaLabel->setToolTip(m_javaExe);
    }
}

void WorldConvertDialog::onRescanJava()
{
    m_javaExe = WorldConverter::findJavaRuntime();
    updateJavaLabel();
    if (m_javaExe.isEmpty())
        appendLog(tr("未找到 Java 17+。若转换失败，请点击「手动选择」指定 java.exe。"),
                  QStringLiteral("#e05a5a"));
}

void WorldConvertDialog::onPickJava()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择 java.exe"), QString(),
#ifdef Q_OS_WIN
        tr("Java 可执行文件 (java.exe);;所有文件 (*.*)")
#else
        tr("Java 可执行文件 (java);;所有文件 (*.*)")
#endif
    );
    if (path.isEmpty())
        return;
    // 简单校验：存在且名为 java / java.exe
    const QString name = QFileInfo(path).fileName().toLower();
    if (name != QLatin1String("java") && name != QLatin1String("java.exe"))
    {
        AppMessageBox::warning(this, tr("选择 Java"),
                               tr("所选文件不是 java 可执行文件。"));
        return;
    }
    m_javaExe = path;
    updateJavaLabel();
    appendLog(tr("已选择 Java：%1").arg(path));
}

QString WorldConvertDialog::resolvedFormatId() const
{
    const QString fmt =
        m_formatCombo->currentData().toString();
    if (fmt.isEmpty() || fmt == QLatin1String(kAutoFormat))
    {
        // 自动匹配 = 目标侧最新格式
        if (m_rbJ2B->isChecked())
        {
            const QStringList bedrock = WorldConverter::bedrockVersions();
            if (!bedrock.isEmpty())
                return WorldConverter::formatIdForBedrock(bedrock.last());
        }
        else
        {
            const QStringList java = WorldConverter::javaVersions();
            if (!java.isEmpty())
                return WorldConverter::formatIdForJava(java.last());
        }
        return fmt;
    }
    return fmt;
}

QString WorldConvertDialog::defaultOutputDir() const
{
    const Direction dir = m_rbB2J->isChecked() ? Direction::BedrockToJava
                                               : Direction::JavaToBedrock;
    const QString suffix =
        (dir == Direction::JavaToBedrock) ? QStringLiteral("_基岩版")
                                          : QStringLiteral("_Java版");
    QString base = m_instancePath;
    if (base.isEmpty())
        base = QFileInfo(m_worldDir).absolutePath();
    return base + QStringLiteral("/converted_worlds/")
        + m_worldName + suffix;
}

void WorldConvertDialog::onBrowseOutput()
{
    const QString start = m_outputEdit->text().isEmpty()
        ? QFileInfo(m_worldDir).absolutePath()
        : m_outputEdit->text();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择输出文件夹（需为空或不存在的路径）"), start);
    if (!dir.isEmpty())
        m_outputEdit->setText(dir);
}

void WorldConvertDialog::setRunning(bool running)
{
    m_startBtn->setEnabled(!running);
    m_cancelBtn->setEnabled(running);
    m_rbJ2B->setEnabled(!running);
    m_rbB2J->setEnabled(!running);
    m_outputEdit->setEnabled(!running);
    m_formatCombo->setEnabled(!running);
    m_pkgBox->setEnabled(!running);
    m_emptyBox->setEnabled(!running);
    m_compactBox->setEnabled(!running && m_rbJ2B->isChecked());
    m_contentBox->setEnabled(!running);
    m_yBlendBox->setEnabled(!running && m_rbJ2B->isChecked());
    m_javaRescanBtn->setEnabled(!running);
    m_javaPickBtn->setEnabled(!running);
}

void WorldConvertDialog::onStartClicked()
{
    if (m_task->isRunning() || m_engineDownloader->isRunning())
        return;

    // 备份确认
    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("存档版本转换"),
        tr("请确认已备份存档 \"%1\"。\n\n"
           "转换不保证稳定性，可能导致方块/内容丢失或存档异常。"
           "输出不会覆盖原存档。\n\n"
           "我已了解风险，开始转换。").arg(m_worldName),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    if (m_javaExe.isEmpty())
    {
        AppMessageBox::warning(this, tr("存档版本转换"),
                               tr("未找到 Java 17+，请安装 Java 或手动选择 java.exe。"));
        return;
    }

    const QString outputDir = m_outputEdit->text().trimmed();
    const QString outErr = WorldConverter::checkOutputDir(outputDir);
    if (!outErr.isEmpty())
    {
        AppMessageBox::warning(this, tr("存档版本转换"), outErr);
        return;
    }

    const QString jar = WorldConverter::findEngineJar();
    if (!jar.isEmpty())
    {
        startConversion(jar);
        return;
    }

    // 引擎尚未下载：询问后在线下载，完成再自动开始转换
    const AppMessageBox::StandardButton dlReply = AppMessageBox::question(
        this, tr("存档版本转换"),
        tr("转换引擎尚未下载（约 32MB，来自 Chunker 官方发布页）。\n"
           "需要现在下载吗？下载完成后会自动开始转换。"),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
    if (dlReply != AppMessageBox::Yes)
        return;

    m_logEdit->clear();
    m_progressBar->setValue(0);
    appendLog(tr("正在下载转换引擎（约 32MB）…"));
    m_statusLabel->setText(tr("正在下载转换引擎…"));
    setRunning(true);
    m_openOutBtn->setEnabled(false);
    m_engineDownloader->start();
}

void WorldConvertDialog::startConversion(const QString &jarPath)
{
    const QString outputDir = m_outputEdit->text().trimmed();

    WorldConverter::WorldConvertTask::Request req;
    req.javaExe = m_javaExe;
    req.jarPath = jarPath;
    req.inputWorldDir = m_worldDir;
    req.outputDir = outputDir;
    req.formatId = resolvedFormatId();
    req.direction = m_rbB2J->isChecked() ? Direction::BedrockToJava
                                         : Direction::JavaToBedrock;
    req.options.discardEmpty = m_emptyBox->isChecked();
    req.options.enableCompact = m_compactBox->isChecked();
    req.options.keepContent = m_contentBox->isChecked();
    req.options.preventYBlend = m_yBlendBox->isChecked();
    req.options.packageOutput = m_pkgBox->isChecked();

    m_lastOutputPath = outputDir;
    m_progressBar->setValue(0);
    appendLog(tr("输入存档：%1").arg(m_worldDir));
    appendLog(tr("输出目录：%1").arg(outputDir));
    appendLog(tr("目标格式：%1").arg(req.formatId));
    appendLog(tr("引擎：%1").arg(jarPath));
    appendLog(tr("Java：%1").arg(m_javaExe));

    setRunning(true);
    m_openOutBtn->setEnabled(false);
    m_task->start(req);
}

void WorldConvertDialog::onEngineProgress(qint64 received, qint64 total)
{
    if (total > 0)
    {
        const int pct = qBound(0, static_cast<int>(received * 100 / total), 100);
        m_progressBar->setValue(pct);
        m_statusLabel->setText(
            tr("正在下载转换引擎… %1%").arg(pct));
    }
    else
    {
        m_statusLabel->setText(
            tr("正在下载转换引擎… %1 MB")
                .arg(received / (1024 * 1024)));
    }
}

void WorldConvertDialog::onEngineDownloaded(bool ok, const QString &message,
                                            const QString &jarPath)
{
    if (!ok)
    {
        setRunning(false);
        m_statusLabel->setText(tr("引擎下载失败"));
        appendLog(tr("—— %1 ——").arg(message), QStringLiteral("#e05a5a"));
        AppMessageBox::warning(this, tr("存档版本转换"), message);
        return;
    }
    appendLog(message);
    appendLog(tr("引擎已就绪，开始转换…"));
    startConversion(jarPath);
}

void WorldConvertDialog::onCancelClicked()
{
    if (m_engineDownloader->isRunning())
    {
        m_engineDownloader->cancel();
        return;
    }
    m_task->cancel();
    m_statusLabel->setText(tr("已取消"));
    appendLog(tr("—— 已取消 ——"), QStringLiteral("#e05a5a"));
}

void WorldConvertDialog::onOpenOutputClicked()
{
    if (!m_lastOutputPath.isEmpty() && QDir(m_lastOutputPath).exists())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastOutputPath));
}

void WorldConvertDialog::onTaskProgress(int percent)
{
    m_progressBar->setValue(percent);
}

void WorldConvertDialog::onTaskStatus(const QString &text)
{
    m_statusLabel->setText(text);
}

void WorldConvertDialog::onTaskLog(const QString &line)
{
    const bool isError = line.contains(QLatin1String("Failed"))
        || line.contains(QLatin1String("exception"), Qt::CaseInsensitive)
        || line.startsWith(QLatin1String("[错误]"))
        || line.startsWith(QLatin1String("[警告]"));
    appendLog(line, isError ? QStringLiteral("#e05a5a") : QString());
}

void WorldConvertDialog::appendLog(const QString &text, const QString &colorTag)
{
    if (colorTag.isEmpty())
    {
        m_logEdit->append(text);
    }
    else
    {
        m_logEdit->append(QStringLiteral("<span style=\"color:%1;\">%2</span>")
                              .arg(colorTag, text.toHtmlEscaped()));
    }
}

void WorldConvertDialog::onTaskFinished(bool ok, int exitCode,
                                        const QString &outputPath,
                                        const QString &message)
{
    setRunning(false);
    m_lastOutputPath = outputPath;
    if (ok)
    {
        m_progressBar->setValue(100);
        m_statusLabel->setText(tr("转换完成"));
        appendLog(tr("—— 转换完成 ——"), QStringLiteral("#3dbd6e"));
        m_openOutBtn->setEnabled(true);
        AppMessageBox::information(
            this, tr("存档版本转换"),
            tr("存档转换完成！\n输出目录：%1").arg(outputPath));
    }
    else
    {
        m_statusLabel->setText(tr("转换失败"));
        appendLog(tr("—— 转换失败（退出码 %1）：%2 ——")
                      .arg(exitCode).arg(message),
                  QStringLiteral("#e05a5a"));
    }
}

void WorldConvertDialog::closeEvent(QCloseEvent *event)
{
    if (m_task->isRunning() || m_engineDownloader->isRunning())
    {
        const AppMessageBox::StandardButton reply = AppMessageBox::question(
            this, tr("存档版本转换"),
            tr("转换/下载仍在进行中，关闭将中止。确定关闭吗？"),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
        if (reply != AppMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        m_engineDownloader->cancel();
        m_task->cancel();
    }
    AppDialogBase::closeEvent(event);
}
