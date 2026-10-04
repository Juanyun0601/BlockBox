/**
 * @file   BedrockInstanceHomePage.cpp
 * @brief  基岩版实例助手 - 首页实现
 * @author BlockBox Team
 * @date   2026-08-25
 */
#include "BedrockInstanceHomePage.h"

#include <QDateTime>
#include <QDir>
#include <QFont>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "components/OutlinedLabel.h"
#include "utils/ThemeManager.h"
#include "utils/bedrock/BedrockInstanceManager.h"

namespace {
/**
 * @brief 格式化时间为友好显示（如 "3 分钟前"、"2 小时前"、"昨天" 等）
 */
QString formatTimeAgo(const QDateTime &dt)
{
    if (!dt.isValid())
        return QStringLiteral("--");

    qint64 secsAgo = dt.secsTo(QDateTime::currentDateTime());
    if (secsAgo < 0)
        secsAgo = 0;

    if (secsAgo < 60)
        return QStringLiteral("刚刚");
    if (secsAgo < 3600)
        return QStringLiteral("%1 分钟前").arg(secsAgo / 60);
    if (secsAgo < 86400)
        return QStringLiteral("%1 小时前").arg(secsAgo / 3600);
    if (secsAgo < 86400 * 2)
        return QStringLiteral("昨天");
    if (secsAgo < 86400 * 7)
        return QStringLiteral("%1 天前").arg(secsAgo / 86400);

    return dt.toString(QStringLiteral("MM-dd HH:mm"));
}

/**
 * @brief 统计指定目录下的子目录数量
 */
int countSubdirs(const QString &path)
{
    QDir dir(path);
    if (!dir.exists())
        return 0;
    return dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
}
} // namespace

BedrockInstanceHomePage::BedrockInstanceHomePage(QWidget *parent)
    : QWidget(parent)
    , m_versionValue(nullptr)
    , m_lastPlayedValue(nullptr)
    , m_worldsCountValue(nullptr)
    , m_launchBtn(nullptr)
    , m_openDataBtn(nullptr)
    , m_openMojangBtn(nullptr)
    , m_resourcePacksCountValue(nullptr)
    , m_behaviorPacksCountValue(nullptr)
    , m_skinPacksCountValue(nullptr)
    , m_tickTimer(nullptr)
{
    initUI();
    applyThemeStyles();

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &BedrockInstanceHomePage::applyThemeStyles);
    connect(ThemeManager::instance(), &ThemeManager::textColorChanged,
            this, &BedrockInstanceHomePage::applyThemeStyles);

    // 每 5 秒刷新一次（比 Java 版首页间隔更长，因为基岩版不需要高频监控）
    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(5000);
    connect(m_tickTimer, &QTimer::timeout, this, &BedrockInstanceHomePage::onSecondTick);
    m_tickTimer->start();
}

BedrockInstanceHomePage::~BedrockInstanceHomePage() = default;

void BedrockInstanceHomePage::setInstanceId(const QString &instanceId)
{
    m_instanceId = instanceId;
    refreshInstanceInfo();
    refreshStorageStats();
}

QWidget *BedrockInstanceHomePage::createInfoCell(const QString &title, QLabel *&valueLabel)
{
    auto *cell = new QWidget(this);
    cell->setObjectName("homeInfoCell");
    auto *v = new QVBoxLayout(cell);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(4);

    auto *titleLabel = new QLabel(title, cell);
    titleLabel->setObjectName("homeInfoTitle");
    titleLabel->setAlignment(Qt::AlignCenter);

    valueLabel = new QLabel(QStringLiteral("--"), cell);
    valueLabel->setObjectName("homeInfoValue");
    valueLabel->setAlignment(Qt::AlignCenter);
    QFont valueFont = valueLabel->font();
    valueFont.setPointSize(14);
    valueFont.setBold(true);
    valueLabel->setFont(valueFont);

    v->addWidget(titleLabel);
    v->addWidget(valueLabel);
    return cell;
}

void BedrockInstanceHomePage::initUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // ---- 实例信息卡片：3 个并列子项 ----
    auto *infoContainer = new QWidget(this);
    infoContainer->setObjectName("homeInfoContainer");
    auto *infoLayout = new QHBoxLayout(infoContainer);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(6);

    infoLayout->addWidget(createInfoCell(tr("游戏版本"), m_versionValue), 1);
    infoLayout->addWidget(createInfoCell(tr("上次游玩"), m_lastPlayedValue), 1);
    infoLayout->addWidget(createInfoCell(tr("世界数"), m_worldsCountValue), 1);

    mainLayout->addWidget(infoContainer);

    // ---- 快捷操作区标题 ----
    auto *actionTitle = new OutlinedLabel(tr("快捷操作"), this);
    actionTitle->setObjectName("homeSectionTitle");
    mainLayout->addWidget(actionTitle);

    // ---- 快捷操作按钮 ----
    m_launchBtn = new QPushButton(tr("启动基岩版"), this);
    m_launchBtn->setObjectName("bedrockHomeLaunchBtn");
    m_launchBtn->setCursor(Qt::PointingHandCursor);
    m_launchBtn->setFixedHeight(40);
    connect(m_launchBtn, &QPushButton::clicked, this, &BedrockInstanceHomePage::launchGameRequested);
    mainLayout->addWidget(m_launchBtn);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);

    m_openDataBtn = new QPushButton(tr("打开数据目录"), this);
    m_openDataBtn->setObjectName("bedrockHomeOpenDirBtn");
    m_openDataBtn->setCursor(Qt::PointingHandCursor);
    m_openDataBtn->setFixedHeight(36);
    connect(m_openDataBtn, &QPushButton::clicked, this, &BedrockInstanceHomePage::openDataDirRequested);

    m_openMojangBtn = new QPushButton(tr("打开 com.mojang"), this);
    m_openMojangBtn->setObjectName("bedrockHomeOpenDirBtn");
    m_openMojangBtn->setCursor(Qt::PointingHandCursor);
    m_openMojangBtn->setFixedHeight(36);
    connect(m_openMojangBtn, &QPushButton::clicked, this, &BedrockInstanceHomePage::openMojangDirRequested);

    btnRow->addWidget(m_openDataBtn);
    btnRow->addWidget(m_openMojangBtn);
    mainLayout->addLayout(btnRow);

    // ---- 存储概览区标题 ----
    auto *storageTitle = new OutlinedLabel(tr("存储概览"), this);
    storageTitle->setObjectName("homeSectionTitle");
    mainLayout->addWidget(storageTitle);

    // ---- 存储概览卡片 ----
    auto *storageContainer = new QWidget(this);
    storageContainer->setObjectName("homeInfoContainer");
    auto *storageLayout = new QHBoxLayout(storageContainer);
    storageLayout->setContentsMargins(0, 0, 0, 0);
    storageLayout->setSpacing(6);

    storageLayout->addWidget(createInfoCell(tr("资源包"), m_resourcePacksCountValue), 1);
    storageLayout->addWidget(createInfoCell(tr("行为包"), m_behaviorPacksCountValue), 1);
    storageLayout->addWidget(createInfoCell(tr("皮肤包"), m_skinPacksCountValue), 1);

    mainLayout->addWidget(storageContainer);

    mainLayout->addStretch(1);
}

void BedrockInstanceHomePage::applyThemeStyles()
{
    QString themeColor = ThemeManager::instance()->currentThemeColor();
    QString textColor = ThemeManager::instance()->currentTextColor();
    QString borderColor = ThemeManager::instance()->currentBorderColor();
    QColor tc(themeColor);
    QString hoverBg = QString("rgba(%1, %2, %3, 0.12)")
        .arg(tc.red()).arg(tc.green()).arg(tc.blue());

    // 信息单元格样式
    QString infoStyle = QString(
        "#homeInfoCell {"
        "  background: palette(base);"
        "  border: 1px solid %1;"
        "  border-radius: 6px;"
        "}"
        "#homeInfoTitle {"
        "  color: %2;"
        "  font-size: 11px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "#homeInfoValue {"
        "  color: %3;"
        "  background: transparent;"
        "  border: none;"
        "}"
    ).arg(borderColor, textColor, themeColor);

    auto cells = findChildren<QWidget *>("homeInfoCell");
    for (auto *cell : cells)
        cell->setStyleSheet(infoStyle);

    // 启动按钮
    m_launchBtn->setStyleSheet(
        QString(
            "#bedrockHomeLaunchBtn {"
            "  background: %1;"
            "  color: white;"
            "  border: none;"
            "  border-radius: 8px;"
            "  font-size: 13px;"
            "  font-weight: 600;"
            "}"
            "#bedrockHomeLaunchBtn:hover {"
            "  background: %2;"
            "}"
            "#bedrockHomeLaunchBtn:pressed {"
            "  background: %3;"
            "}"
        ).arg(themeColor,
              QString("rgba(%1, %2, %3, 0.85)").arg(tc.red()).arg(tc.green()).arg(tc.blue()),
              QString("rgba(%1, %2, %3, 0.7)").arg(tc.red()).arg(tc.green()).arg(tc.blue()))
    );

    // 次级按钮
    QString dirBtnStyle = QString(
        "#bedrockHomeOpenDirBtn {"
        "  background: palette(base);"
        "  color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 6px;"
        "  font-size: 12px;"
        "}"
        "#bedrockHomeOpenDirBtn:hover {"
        "  background: %3;"
        "}"
    ).arg(textColor, borderColor, hoverBg);

    m_openDataBtn->setStyleSheet(dirBtnStyle);
    m_openMojangBtn->setStyleSheet(dirBtnStyle);
}

void BedrockInstanceHomePage::refreshInstanceInfo()
{
    auto *mgr = BedrockInstanceManager::instance();
    BedrockInstance inst = mgr->instanceById(m_instanceId);

    if (m_versionValue)
        m_versionValue->setText(inst.version.isEmpty() ? QStringLiteral("--") : inst.version);

    if (m_lastPlayedValue)
        m_lastPlayedValue->setText(formatTimeAgo(inst.lastPlayed));

    // 统计世界数量
    QString mojangDir = inst.dataDir + QStringLiteral("/com.mojang");
    int worldCount = countSubdirs(mojangDir + QStringLiteral("/minecraftWorlds"));
    if (m_worldsCountValue)
        m_worldsCountValue->setText(QString::number(worldCount));
}

void BedrockInstanceHomePage::refreshStorageStats()
{
    auto *mgr = BedrockInstanceManager::instance();
    BedrockInstance inst = mgr->instanceById(m_instanceId);
    QString mojangDir = inst.dataDir + QStringLiteral("/com.mojang");

    int rpCount = countSubdirs(mojangDir + QStringLiteral("/resource_packs"));
    int bpCount = countSubdirs(mojangDir + QStringLiteral("/behavior_packs"));
    int spCount = countSubdirs(mojangDir + QStringLiteral("/skin_packs"));

    if (m_resourcePacksCountValue)
        m_resourcePacksCountValue->setText(QString::number(rpCount));
    if (m_behaviorPacksCountValue)
        m_behaviorPacksCountValue->setText(QString::number(bpCount));
    if (m_skinPacksCountValue)
        m_skinPacksCountValue->setText(QString::number(spCount));
}

void BedrockInstanceHomePage::onSecondTick()
{
    // 低频刷新存储统计（每 5 秒一次）
    refreshStorageStats();
}
