/**
 * @file   DataPackManagerDialog.cpp
 * @brief  本地数据包管理弹窗实现
 * @author BlockBox Team
 * @date   2026-11-25
 */
#include "DataPackManagerDialog.h"

#include "AppMessageBox.h"
#include "CustomCheckBox.h"
#include "utils/GameLauncher.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"

#include <QAction>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "utils/McimHelper.h"
#include "utils/mod/ModrinthAPI.h"

#include <algorithm>
#include <functional>

namespace {

/// 文件大小（B / KB / MB / GB）
QString formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024LL * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
}

/// 数据包的唯一标识（level.dat 条目名优先，缺失时退回路径）
QString packKeyOf(const DataPackInfo &info)
{
    return info.id.isEmpty() ? info.filePath : info.id;
}

/**
 * @brief 给长文本插入零宽空格，让 QLabel 能在路径分隔符处换行
 *
 * QLabel 的自动换行只在空格 / 中文处断行，像
 * "data/demo/worldgen/biome/test" 这样的长路径会被当成一个整词，
 * 把右侧详情栏的最小宽度撑到几百像素，导致文字被裁掉。
 * 这里在常见分隔符后与连续 12 个字符处补一个零宽空格，只影响换行，
 * 肉眼看不到，长度也很小。
 */
QString softBreakText(const QString &text)
{
    static const QString separators = QStringLiteral("/\\:._-,");
    QString out;
    out.reserve(text.size() + text.size() / 8 + 8);
    int run = 0;
    for (const QChar ch : text)
    {
        out.append(ch);
        if (ch == QLatin1Char('\n'))
        {
            run = 0;
            continue;
        }
        ++run;
        if (separators.contains(ch) || run >= 12)
        {
            out.append(QChar(0x200B)); // zero width space
            run = 0;
        }
    }
    return out;
}

/**
 * @brief 可整行点击的卡片
 *
 * 行内的自定义开关自己消费鼠标事件，点开关只会切换启停；
 * 点行内其它位置（文字、留白）走这里的回调选中该行。
 */
class PackRowFrame : public QFrame
{
public:
    explicit PackRowFrame(QWidget *parent = nullptr)
        : QFrame(parent)
    {
    }

    std::function<void()> clicked;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        // 开关先处理点击并可能重建列表：被隐藏的旧行不再响应，避免重复选中错行
        if (event->button() == Qt::LeftButton && clicked && isVisible() && isEnabled())
            clicked();
        QFrame::mouseReleaseEvent(event);
    }
};

} // namespace

void DataPackManagerDialog::openForWorld(QWidget *parent, const QString &instancePath,
                                         const QString &worldName,
                                         const QString &gameVersionHint)
{
    if (instancePath.isEmpty() || worldName.isEmpty())
        return;
    DataPackManagerDialog dialog(parent, instancePath, worldName, gameVersionHint);
    dialog.exec();
}

DataPackManagerDialog::DataPackManagerDialog(QWidget *parent, const QString &instancePath,
                                             const QString &worldName,
                                             const QString &gameVersionHint)
    : AppDialogBase(parent)
    , m_instancePath(instancePath)
    , m_worldName(worldName)
    , m_worldDir(instancePath + QStringLiteral("/saves/") + worldName)
    , m_gameVersionHint(gameVersionHint)
{
    m_updateApi = new ModrinthAPI(this);
    connect(m_updateApi, &ModrinthAPI::hashesMatched,
            this, &DataPackManagerDialog::onHashesMatched);
    connect(m_updateApi, &ModrinthAPI::hashesMatchFailed,
            this, &DataPackManagerDialog::onUpdateCheckFailed);
    connect(m_updateApi, &ModrinthAPI::updatesChecked,
            this, &DataPackManagerDialog::onUpdatesChecked);
    connect(m_updateApi, &ModrinthAPI::updatesCheckFailed,
            this, &DataPackManagerDialog::onUpdateCheckFailed);
    initUI();
    reload();
    layoutBottomBar();
    // 窄屏/宽屏的判断留给第一次 resizeEvent（构造期还没有真实窗口宽度）
}

void DataPackManagerDialog::initUI()
{
    // 居中卡片（卡片外观由全局 QSS 提供，行与提示条用本弹窗的内联样式）
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addStretch(1);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setMinimumSize(UiMetrics::fitToScreen(this, QSize(820, 600)));
    card->setMaximumWidth(900);
    rootLayout->addWidget(card, 0, Qt::AlignHCenter);
    rootLayout->addStretch(1);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 18);
    cardLayout->setSpacing(10);

    // 标题
    QLabel *titleLabel = new QLabel(tr("数据包管理"), card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));
    cardLayout->addWidget(titleLabel);

    // 副标题：世界名 · 世界版本 · 数量统计
    m_subtitleLabel = new QLabel(card);
    m_subtitleLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
    m_subtitleLabel->setWordWrap(true);
    cardLayout->addWidget(m_subtitleLabel);

    // 加载顺序说明
    m_hintLabel = new QLabel(
        tr("数据包按顺序加载，越靠下的越晚加载，同名内容会被后面的覆盖；改动在下次进入世界时生效。"),
        card);
    m_hintLabel->setObjectName(QStringLiteral("datapackHintLabel"));
    m_hintLabel->setWordWrap(true);
    cardLayout->addWidget(m_hintLabel);

    // 游戏运行中警告（只在游戏真的在跑时出现）
    m_runningLabel = new QLabel(
        tr("游戏正在运行，请先退出游戏再修改，否则改动可能被游戏覆盖。"), card);
    m_runningLabel->setObjectName(QStringLiteral("datapackDangerLabel"));
    m_runningLabel->setWordWrap(true);
    GameLauncher *launcher = GameLauncher::instance();
    m_runningLabel->setVisible(launcher && launcher->isGameRunning());
    cardLayout->addWidget(m_runningLabel);

    // 工具条：搜索 + 导入 + 打开文件夹
    QHBoxLayout *toolbarLayout = new QHBoxLayout();
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(8);

    m_searchEdit = new QLineEdit(card);
    m_searchEdit->setObjectName(QStringLiteral("appDialogLineEdit"));
    m_searchEdit->setPlaceholderText(tr("搜索数据包..."));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_search = text.trimmed();
        rebuildRows();
    });
    toolbarLayout->addWidget(m_searchEdit, 1);

    const QColor themeColor(ThemeManager::instance()->currentThemeColor());

    m_importBtn = new QPushButton(tr("导入数据包"), card);
    m_importBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    m_importBtn->setCursor(Qt::PointingHandCursor);
    m_importBtn->setToolTip(tr("从实例数据包库或电脑上选择数据包导入这个世界"));
    connect(m_importBtn, &QPushButton::clicked, this, &DataPackManagerDialog::showImportMenu);
    toolbarLayout->addWidget(m_importBtn);

    // 在线检查更新：按 SHA-1 去 Modrinth 比对，有新版的数据包会标出来
    m_checkUpdateBtn = new QPushButton(tr("检查更新"), card);
    m_checkUpdateBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_checkUpdateBtn->setCursor(Qt::PointingHandCursor);
    m_checkUpdateBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/update.svg",
                                                         themeColor, 16));
    m_checkUpdateBtn->setIconSize(QSize(16, 16));
    m_checkUpdateBtn->setToolTip(tr("联网比对这个世界的数据包，看看有没有新版本"));
    connect(m_checkUpdateBtn, &QPushButton::clicked, this, &DataPackManagerDialog::checkUpdates);
    toolbarLayout->addWidget(m_checkUpdateBtn);

    m_updateBtn = new QPushButton(tr("更新数据包"), card);
    m_updateBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    m_updateBtn->setCursor(Qt::PointingHandCursor);
    m_updateBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/download.svg",
                                                     QColor("#ffffff"), 16));
    m_updateBtn->setIconSize(QSize(16, 16));
    m_updateBtn->setToolTip(tr("把有更新的数据包换成本地同名的新文件，启用状态与加载顺序不变"));
    m_updateBtn->hide();
    connect(m_updateBtn, &QPushButton::clicked, this,
            &DataPackManagerDialog::updateCheckedPacks);
    toolbarLayout->addWidget(m_updateBtn);

    m_toolbarFolderBtn = new QPushButton(tr("打开文件夹"), card);
    m_toolbarFolderBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_toolbarFolderBtn->setCursor(Qt::PointingHandCursor);
    m_toolbarFolderBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/folder.svg",
                                                           themeColor, 16));
    m_toolbarFolderBtn->setIconSize(QSize(16, 16));
    connect(m_toolbarFolderBtn, &QPushButton::clicked, this,
            &DataPackManagerDialog::openDatapacksFolder);
    toolbarLayout->addWidget(m_toolbarFolderBtn);

    cardLayout->addLayout(toolbarLayout);

    // 主体：左列表 + 右详情（窄屏改上下堆叠）
    m_splitter = new QSplitter(Qt::Horizontal, card);
    m_splitter->setObjectName(QStringLiteral("datapackSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(8);

    // 左侧：数据包列表
    m_leftPane = new QWidget(m_splitter);
    m_leftPane->setObjectName(QStringLiteral("datapackPane"));
    m_leftPane->setMinimumWidth(260);
    QVBoxLayout *leftLayout = new QVBoxLayout(m_leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_listScroll = new QScrollArea(m_leftPane);
    m_listScroll->setObjectName(QStringLiteral("datapackListScroll"));
    m_listScroll->setWidgetResizable(true);
    m_listScroll->setFrameShape(QFrame::NoFrame);
    m_listScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_listHost = new QWidget;
    m_listHost->setObjectName(QStringLiteral("datapackListHost"));
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(0, 0, 6, 0);
    m_listLayout->setSpacing(8);
    m_listLayout->addStretch(1);
    m_listScroll->setWidget(m_listHost);

    leftLayout->addWidget(m_listScroll, 1);
    m_splitter->addWidget(m_leftPane);

    // 右侧：只读详情
    m_rightPane = new QWidget(m_splitter);
    m_rightPane->setObjectName(QStringLiteral("datapackPane"));
    m_rightPane->setMinimumWidth(260);
    QVBoxLayout *rightLayout = new QVBoxLayout(m_rightPane);
    rightLayout->setContentsMargins(5, 0, 0, 0);
    rightLayout->setSpacing(6);

    QLabel *detailTitle = new QLabel(tr("数据包详情"), m_rightPane);
    detailTitle->setObjectName(QStringLiteral("datapackMetaLabel"));
    rightLayout->addWidget(detailTitle);

    m_detailScroll = new QScrollArea(m_rightPane);
    m_detailScroll->setObjectName(QStringLiteral("datapackDetailScroll"));
    m_detailScroll->setWidgetResizable(true);
    m_detailScroll->setFrameShape(QFrame::NoFrame);
    m_detailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightLayout->addWidget(m_detailScroll, 1);
    m_splitter->addWidget(m_rightPane);

    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes(QList<int>() << 480 << 300);
    cardLayout->addWidget(m_splitter, 1);

    // 空世界提示（世界没有任何数据包时插进列表）
    m_emptyWidget = new QWidget(m_listHost);
    QVBoxLayout *emptyLayout = new QVBoxLayout(m_emptyWidget);
    emptyLayout->setContentsMargins(12, 36, 12, 36);
    emptyLayout->setSpacing(10);
    QLabel *emptyLabel = new QLabel(tr("这个世界还没有数据包"), m_emptyWidget);
    emptyLabel->setObjectName(QStringLiteral("datapackEmptyLabel"));
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setWordWrap(true);
    emptyLayout->addWidget(emptyLabel);
    QPushButton *emptyImportBtn = new QPushButton(tr("导入数据包"), m_emptyWidget);
    emptyImportBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    emptyImportBtn->setCursor(Qt::PointingHandCursor);
    connect(emptyImportBtn, &QPushButton::clicked, this,
            &DataPackManagerDialog::showImportMenu);
    emptyLayout->addWidget(emptyImportBtn, 0, Qt::AlignHCenter);
    m_emptyWidget->hide();

    // 搜索无结果提示
    m_noMatchLabel = new QLabel(tr("没有匹配的数据包"), m_listHost);
    m_noMatchLabel->setObjectName(QStringLiteral("datapackEmptyLabel"));
    m_noMatchLabel->setAlignment(Qt::AlignCenter);
    m_noMatchLabel->setWordWrap(true);
    m_noMatchLabel->hide();

    // 卡片内联样式：行卡片半透明叠加、选中用主题色描边
    card->setStyleSheet(
        QStringLiteral(
            "QScrollArea#datapackListScroll, QScrollArea#datapackDetailScroll {"
            "  background: transparent; border: none; }"
            "QWidget#datapackListHost, QWidget#datapackPane { background: transparent; }"
            "QWidget#datapackDetailHost { background: transparent; }"
            "QFrame#datapackRow { background-color: rgba(128,128,128,0.14);"
            "  border: 1px solid rgba(128,128,128,0.24); border-radius: 10px; }"
            "QFrame#datapackRow:hover { background-color: rgba(128,128,128,0.22); }"
            "QFrame#datapackRow[packSelected=\"true\"] {"
            "  border: 1px solid %1; background-color: rgba(128,128,128,0.24); }"
            "QLabel#datapackNameLabel { background: transparent; border: none;"
            "  font-size: 14px; font-weight: 600; }"
            "QLabel#datapackMetaLabel { background: transparent; border: none;"
            "  color: rgba(140,140,140,0.95); font-size: 12px; }"
            "QLabel#datapackMutedLabel { background: transparent; border: none;"
            "  color: rgba(128,128,128,0.92); font-size: 12px; }"
            "QLabel#datapackEmptyLabel { background: transparent; border: none;"
            "  color: rgba(128,128,128,0.9); font-size: 13px; }"
            "QLabel#datapackHintLabel { color: rgba(150,90,0,0.95);"
            "  background: rgba(255,193,7,0.12); border-radius: 8px;"
            "  padding: 6px 10px; font-size: 12px; }"
            "QLabel#datapackDangerLabel { color: #EF4444;"
            "  background: rgba(239,68,68,0.12); border-radius: 8px;"
            "  padding: 6px 10px; font-size: 12px; font-weight: 600; }"
            "QLabel#datapackStatusLabel { background: transparent; border: none;"
            "  color: rgba(128,128,128,0.95); font-size: 12px; }"
            "QSplitter#datapackSplitter::handle { background-color: rgba(128,128,128,0.18);"
            "  border-radius: 3px; }"
            "QSplitter#datapackSplitter::handle:hover {"
            "  background-color: rgba(128,128,128,0.32); }")
            .arg(themeColor.name()));

    // 状态行（保存结果 / 错误提示）
    m_statusLabel = new QLabel(card);
    m_statusLabel->setObjectName(QStringLiteral("datapackStatusLabel"));
    m_statusLabel->setWordWrap(true);
    cardLayout->addWidget(m_statusLabel);

    // 底部按钮：上移 / 下移 / 删除 / 打开文件夹 / 关闭（窄屏由 QGridLayout 换行）
    m_bottomGrid = new QGridLayout();
    m_bottomGrid->setContentsMargins(0, 0, 0, 0);
    m_bottomGrid->setHorizontalSpacing(8);
    m_bottomGrid->setVerticalSpacing(8);

    m_upBtn = new QPushButton(tr("上移"), card);
    m_upBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_upBtn->setCursor(Qt::PointingHandCursor);
    m_upBtn->setToolTip(tr("把选中的数据包往上挪一位，更早加载"));
    connect(m_upBtn, &QPushButton::clicked, this, [this]() { movePack(m_selected, true); });

    m_downBtn = new QPushButton(tr("下移"), card);
    m_downBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_downBtn->setCursor(Qt::PointingHandCursor);
    m_downBtn->setToolTip(tr("把选中的数据包往下挪一位，更晚加载"));
    connect(m_downBtn, &QPushButton::clicked, this, [this]() { movePack(m_selected, false); });

    m_deleteBtn = new QPushButton(tr("删除"), card);
    m_deleteBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/delete.svg",
                                                     QColor("#EF4444"), 16));
    m_deleteBtn->setIconSize(QSize(16, 16));
    connect(m_deleteBtn, &QPushButton::clicked, this,
            &DataPackManagerDialog::removeSelectedPack);

    m_folderBtn = new QPushButton(tr("打开文件夹"), card);
    m_folderBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_folderBtn->setCursor(Qt::PointingHandCursor);
    m_folderBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/folder.svg",
                                                     themeColor, 16));
    m_folderBtn->setIconSize(QSize(16, 16));
    connect(m_folderBtn, &QPushButton::clicked, this,
            &DataPackManagerDialog::openDatapacksFolder);

    m_closeBtn = new QPushButton(tr("关闭"), card);
    m_closeBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    cardLayout->addLayout(m_bottomGrid);

    refreshActionState();
}

void DataPackManagerDialog::reload()
{
    m_world = DataPackManager::scanWorld(m_worldDir, m_gameVersionHint);
    m_packs = m_world.packs;
    m_selected = -1;

    // 世界数据包格式：判断数据包是偏新还是偏旧
    QString worldVersion = m_world.worldVersion.trimmed();
    if (worldVersion.isEmpty())
        worldVersion = m_gameVersionHint.trimmed();
    m_worldFormatMajor = -1;
    m_worldFormatMinor = 0;
    const QString worldFormat = DataPackManager::packFormatForVersion(worldVersion);
    if (!worldFormat.isEmpty())
    {
        const QStringList parts = worldFormat.split(QLatin1Char('.'));
        m_worldFormatMajor = parts.value(0).toInt();
        m_worldFormatMinor = parts.value(1).toInt();
    }

    if (m_statusLabel)
    {
        m_statusLabel->clear();
        if (!m_world.ok && !m_world.error.isEmpty())
            m_statusLabel->setText(tr("读取世界存档信息失败：%1（启停状态可能无法保存）")
                                       .arg(m_world.error));
    }

    rebuildRows();
    rebuildDetailPanel();
}

void DataPackManagerDialog::rebuildRows()
{
    if (!m_listLayout)
        return;

    m_rebuilding = true;

    // 清空旧行（空态与无结果提示复用，只隐藏不销毁）
    while (QLayoutItem *item = m_listLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            if (widget != m_emptyWidget && widget != m_noMatchLabel)
            {
                widget->hide();
                widget->deleteLater();
            }
        }
        delete item;
    }
    m_rowWidgets.clear();
    m_rowNameLabels.clear();
    if (m_emptyWidget)
        m_emptyWidget->hide();
    if (m_noMatchLabel)
        m_noMatchLabel->hide();

    int orderNumber = 0;   // 启用包的加载序号
    int visibleCount = 0;
    const int packCount = static_cast<int>(m_packs.size());
    for (int i = 0; i < packCount; ++i)
    {
        const bool enabled = m_packs.at(i).enabled;
        if (enabled)
            ++orderNumber;
        if (!packMatchesFilter(i))
            continue;
        QWidget *row = createPackRow(i, enabled ? orderNumber : 0);
        m_listLayout->addWidget(row);
        m_rowWidgets.append(row);
        ++visibleCount;
    }

    if (m_packs.isEmpty() && m_emptyWidget)
    {
        m_listLayout->addWidget(m_emptyWidget);
        m_emptyWidget->show();
    }
    else if (visibleCount == 0 && m_noMatchLabel)
    {
        m_noMatchLabel->setText(tr("没有匹配「%1」的数据包").arg(m_search));
        m_listLayout->addWidget(m_noMatchLabel);
        m_noMatchLabel->show();
    }
    m_listLayout->addStretch(1);

    m_rebuilding = false;

    updateRowSelectionStyles();
    refreshHeader();
    refreshActionState();

    // 行卡片排完版后按真实宽度省略过长的名字
    QTimer::singleShot(0, this, [this]() { updateNameElide(); });
}

QWidget *DataPackManagerDialog::createPackRow(int index, int orderNumber)
{
    const DataPackInfo &info = m_packs.at(index);

    PackRowFrame *row = new PackRowFrame(m_listHost);
    row->setObjectName(QStringLiteral("datapackRow"));
    row->setCursor(Qt::PointingHandCursor);
    row->setMinimumHeight(62);
    row->setProperty("packIndex", index);
    row->setProperty("packSelected", index == m_selected);

    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(12, 8, 12, 8);
    rowLayout->setSpacing(10);

    // 左侧：启用开关（构建期用 QSignalBlocker 阻止误触发）
    CustomCheckBox *check = new CustomCheckBox(QString(), row);
    check->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    {
        QSignalBlocker blocker(check);
        check->setChecked(info.enabled);
    }
    rowLayout->addWidget(check, 0, Qt::AlignVCenter);

    // 中间：名称 / 大小与格式 / 徽标
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    QString displayName = info.fileName;
    if (orderNumber > 0)
        displayName = tr("#%1 %2").arg(orderNumber).arg(info.fileName);

    QLabel *nameLabel = new QLabel(row);
    nameLabel->setObjectName(QStringLiteral("datapackNameLabel"));
    nameLabel->setProperty("fullName", displayName);
    nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    nameLabel->setText(displayName);
    nameLabel->setToolTip(displayName);
    textLayout->addWidget(nameLabel);
    m_rowNameLabels.append(nameLabel);

    QLabel *metaLabel = new QLabel(row);
    metaLabel->setObjectName(QStringLiteral("datapackMetaLabel"));
    metaLabel->setText(sizeText(info) + QStringLiteral(" · ") + formatText(info));
    metaLabel->setWordWrap(true);
    textLayout->addWidget(metaLabel);

    QWidget *badgeHost = new QWidget(row);
    QHBoxLayout *badgeLayout = new QHBoxLayout(badgeHost);
    badgeLayout->setContentsMargins(0, 2, 0, 0);
    badgeLayout->setSpacing(6);

    auto addBadge = [&](const QString &text, const QColor &color) {
        if (text.isEmpty())
            return;
        QLabel *chip = new QLabel(text, badgeHost);
        chip->setStyleSheet(chipStyle(color));
        chip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        badgeLayout->addWidget(chip);
    };

    addBadge(compatBadgeText(info), compatColor(info.compat));
    if (info.hasExperimental)
        addBadge(tr("实验性"), QColor("#8B5CF6"));
    if (!info.conflictPaths.isEmpty())
        addBadge(tr("冲突 %1").arg(info.conflictPaths.size()), QColor("#EF4444"));
    if (!info.exists)
        addBadge(tr("文件缺失"), QColor("#EF4444"));
    // 在线检查更新后：标出这个包能升到哪个版本
    {
        const auto updateIt = m_updateByPath.constFind(info.filePath);
        if (updateIt != m_updateByPath.constEnd())
        {
            const QString targetVersion = updateIt->version.isEmpty()
                ? tr("新版本") : updateIt->version;
            addBadge(tr("可更新到 %1").arg(targetVersion), QColor("#16A34A"));
        }
    }

    badgeLayout->addStretch(1);
    textLayout->addWidget(badgeHost);

    rowLayout->addLayout(textLayout, 1);

    // 整行点击选中；点开关只切换启停（同时选中该行）
    row->clicked = [this, index]() { selectPack(index); };
    connect(check, &CustomCheckBox::toggled, this, [this, index](bool enabled) {
        if (m_rebuilding || index < 0 || index >= static_cast<int>(m_packs.size()))
            return;
        selectPack(index);
        togglePack(index, enabled);
    });

    return row;
}

void DataPackManagerDialog::rebuildDetailPanel()
{
    if (!m_detailScroll)
        return;

    // 换掉整块内容（旧内容交给事件循环回收，避免在 setWidget 里同步销毁）
    if (QWidget *oldHost = m_detailScroll->takeWidget())
        oldHost->deleteLater();

    QWidget *host = new QWidget;
    host->setObjectName(QStringLiteral("datapackDetailHost"));
    QVBoxLayout *layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 6, 8);
    layout->setSpacing(6);

    auto addSectionTitle = [&](const QString &text) {
        QLabel *label = new QLabel(text, host);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        label->setContentsMargins(0, 4, 0, 0);
        layout->addWidget(label);
    };
    auto addBody = [&](const QString &text, const QColor &color = QColor(),
                       bool muted = false) -> QLabel * {
        QLabel *label = new QLabel(softBreakText(text), host);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (muted)
            label->setObjectName(QStringLiteral("datapackMutedLabel"));
        else if (color.isValid())
            label->setStyleSheet(QStringLiteral("color: %1; background: transparent;"
                                                " border: none;")
                                     .arg(color.name()));
        layout->addWidget(label);
        return label;
    };

    const DataPackInfo *info = selectedPack();
    if (!info)
    {
        QLabel *placeholder = new QLabel(tr("在左侧选择一个数据包查看详情"), host);
        placeholder->setObjectName(QStringLiteral("datapackMutedLabel"));
        placeholder->setWordWrap(true);
        layout->addWidget(placeholder);
        layout->addStretch(1);
        m_detailScroll->setWidget(host);
        return;
    }

    // 标题 + 启停状态
    QLabel *nameLabel = new QLabel(info->fileName, host);
    nameLabel->setWordWrap(true);
    nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    layout->addWidget(nameLabel);

    QHBoxLayout *stateLayout = new QHBoxLayout();
    stateLayout->setContentsMargins(0, 0, 0, 0);
    stateLayout->setSpacing(6);
    QLabel *stateChip = new QLabel(info->enabled ? tr("已启用") : tr("已禁用"), host);
    stateChip->setStyleSheet(chipStyle(info->enabled ? QColor("#22C55E") : QColor()));
    stateChip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    stateLayout->addWidget(stateChip);
    if (!info->exists)
    {
        QLabel *missingChip = new QLabel(tr("文件缺失"), host);
        missingChip->setStyleSheet(chipStyle(QColor("#EF4444")));
        missingChip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        stateLayout->addWidget(missingChip);
    }
    stateLayout->addStretch(1);
    layout->addLayout(stateLayout);

    // 描述
    addSectionTitle(tr("描述"));
    if (info->description.trimmed().isEmpty())
        addBody(tr("（无描述）"), QColor(), true);
    else
        addBody(info->description.trimmed());

    // 兼容性
    addSectionTitle(tr("兼容性"));
    QString compatText = info->compatDetail.trimmed();
    if (compatText.isEmpty())
        compatText = compatBadgeText(*info);
    addBody(compatText, compatColor(info->compat));
    // 无效原因（兼容说明里已经写过的不重复显示）
    if (!info->metaOk && !info->invalidReason.trimmed().isEmpty()
        && info->invalidReason.trimmed() != compatText)
        addBody(info->invalidReason.trimmed(), QColor("#EF4444"));

    // 内容清单
    addSectionTitle(tr("内容"));
    if (info->contentFileCount > 0)
        addBody(tr("共 %1 个数据文件").arg(info->contentFileCount), QColor(), true);

    int categoryCount = 0;
    const QStringList categoryOrder = DataPackManager::categoryOrder();
    for (const QString &category : categoryOrder)
    {
        if (!info->contents.contains(category))
            continue;
        const QStringList items = info->contents.value(category);
        if (items.isEmpty())
            continue;
        ++categoryCount;

        QStringList shortNames;
        const int limit = qMin(10, static_cast<int>(items.size()));
        for (int i = 0; i < limit; ++i)
            shortNames << items.at(i);
        QString line = tr("%1 %2：%3")
                           .arg(category)
                           .arg(items.size())
                           .arg(shortNames.join(QStringLiteral(", ")));
        if (items.size() > limit)
            line += tr(" 等 %1 个").arg(items.size() - limit);
        QLabel *lineLabel = addBody(line, QColor(), true);
        lineLabel->setToolTip(items.join(QStringLiteral("\n")));
    }
    if (categoryCount == 0)
        addBody(tr("未发现可预览的数据文件"), QColor(), true);

    // 冲突文件
    if (!info->conflictPaths.isEmpty())
    {
        addSectionTitle(tr("冲突（%1）").arg(info->conflictPaths.size()));
        const int limit = qMin(10, static_cast<int>(info->conflictPaths.size()));
        for (int i = 0; i < limit; ++i)
            addBody(info->conflictPaths.at(i), QColor(), true);
        if (info->conflictPaths.size() > limit)
            addBody(tr("…… 共 %1 个冲突文件")
                        .arg(info->conflictPaths.size()),
                    QColor("#EF4444"));
    }

    // 文件信息
    addSectionTitle(tr("文件信息"));
    QFileInfo fileInfo(info->filePath);
    addBody(tr("文件名：%1").arg(info->fileName), QColor(), true);
    if (!info->filePath.isEmpty())
        addBody(tr("路径：%1").arg(QDir::toNativeSeparators(info->filePath)), QColor(), true);
    addBody(tr("大小：%1").arg(sizeText(*info)), QColor(), true);
    const QDateTime modifiedAt = fileInfo.exists() ? fileInfo.lastModified() : info->lastModified;
    if (modifiedAt.isValid())
        addBody(tr("修改时间：%1").arg(modifiedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))),
                QColor(), true);

    const QString packPath = info->filePath;
    QPushButton *revealBtn = new QPushButton(tr("在文件夹中显示"), host);
    revealBtn->setObjectName(QStringLiteral("appDialogBtn"));
    revealBtn->setCursor(Qt::PointingHandCursor);
    connect(revealBtn, &QPushButton::clicked, this, [this, packPath]() {
        QString dir = packPath;
        if (dir.isEmpty())
        {
            dir = m_worldDir + QStringLiteral("/datapacks");
        }
        else
        {
            const QFileInfo target(dir);
            if (!target.isDir())
                dir = target.absolutePath();
        }
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    layout->addWidget(revealBtn, 0, Qt::AlignLeft);

    layout->addStretch(1);
    m_detailScroll->setWidget(host);
}

void DataPackManagerDialog::refreshHeader()
{
    if (!m_subtitleLabel)
        return;

    QString versionText = m_world.worldVersion.trimmed();
    if (versionText.isEmpty())
        versionText = tr("版本未知");

    int enabledCount = 0;
    const int packCount = static_cast<int>(m_packs.size());
    for (int i = 0; i < packCount; ++i)
    {
        if (m_packs.at(i).enabled)
            ++enabledCount;
    }

    m_subtitleLabel->setText(tr("%1 · %2 · 共 %3 个 · 已启用 %4 个")
                                 .arg(m_worldName, versionText)
                                 .arg(packCount)
                                 .arg(enabledCount));
}

void DataPackManagerDialog::refreshActionState()
{
    const DataPackInfo *info = selectedPack();
    const bool hasSelection = info != nullptr;

    bool canMoveUp = false;
    bool canMoveDown = false;
    if (hasSelection && info->enabled)
    {
        QList<int> enabledIndices;
        const int packCount = static_cast<int>(m_packs.size());
        for (int i = 0; i < packCount; ++i)
        {
            if (m_packs.at(i).enabled)
                enabledIndices.append(i);
        }
        const int pos = enabledIndices.indexOf(m_selected);
        canMoveUp = pos > 0;
        canMoveDown = pos >= 0 && pos + 1 < static_cast<int>(enabledIndices.size());
    }

    if (m_upBtn)
        m_upBtn->setEnabled(canMoveUp);
    if (m_downBtn)
        m_downBtn->setEnabled(canMoveDown);
    // 选中任意一行都能删：文件缺失的残留记录也允许从这里清理
    if (m_deleteBtn)
        m_deleteBtn->setEnabled(hasSelection);
}

void DataPackManagerDialog::updateRowSelectionStyles()
{
    for (QWidget *row : m_rowWidgets)
    {
        if (!row)
            continue;
        const bool selected = row->property("packIndex").toInt() == m_selected;
        if (row->property("packSelected").toBool() == selected)
            continue;
        row->setProperty("packSelected", selected);
        row->style()->unpolish(row);
        row->style()->polish(row);
        row->update();
    }
}

void DataPackManagerDialog::updateNameElide()
{
    for (QLabel *label : m_rowNameLabels)
    {
        if (!label)
            continue;
        const QString fullName = label->property("fullName").toString();
        const int available = label->width();
        if (available <= 0 || fullName.isEmpty())
            continue;
        const QFontMetrics metrics(label->font());
        label->setText(metrics.elidedText(fullName, Qt::ElideMiddle, available));
        label->setToolTip(fullName);
    }
}

void DataPackManagerDialog::applyResponsiveLayout()
{
    const int currentWidth = width();
    if (currentWidth <= 0)
        return;

    // 手机 / 窄窗口：左右分栏改上下堆叠，底部按钮换行
    const bool compact = currentWidth < 680;

    if (m_splitter && m_splitterVertical != compact)
    {
        m_splitterVertical = compact;
        m_splitter->setOrientation(compact ? Qt::Vertical : Qt::Horizontal);
        if (m_leftPane)
            m_leftPane->setMinimumWidth(compact ? 0 : 260);
        if (m_rightPane)
            m_rightPane->setMinimumWidth(compact ? 0 : 260);
        m_splitter->setSizes(compact ? (QList<int>() << 320 << 240)
                                     : (QList<int>() << 480 << 280));
    }

    if (m_compact != compact)
    {
        m_compact = compact;
        if (m_importBtn)
            m_importBtn->setText(compact ? tr("导入") : tr("导入数据包"));
        if (m_toolbarFolderBtn)
            m_toolbarFolderBtn->setText(compact ? tr("文件夹") : tr("打开文件夹"));
        layoutBottomBar();
        QTimer::singleShot(0, this, [this]() { updateNameElide(); });
    }
}

void DataPackManagerDialog::layoutBottomBar()
{
    if (!m_bottomGrid)
        return;

    // 只清空格子，不销毁按钮
    while (QLayoutItem *item = m_bottomGrid->takeAt(0))
        delete item;
    for (int column = 0; column < 6; ++column)
        m_bottomGrid->setColumnStretch(column, 0);

    const QList<QPushButton *> actions = {m_upBtn, m_downBtn, m_deleteBtn};
    for (int i = 0; i < actions.size(); ++i)
    {
        if (actions.at(i))
            m_bottomGrid->addWidget(actions.at(i), 0, i);
    }

    if (!m_compact)
    {
        // 宽屏：操作按钮靠左，文件夹与关闭靠右
        m_bottomGrid->setColumnStretch(3, 1);
        if (m_folderBtn)
            m_bottomGrid->addWidget(m_folderBtn, 0, 4);
        if (m_closeBtn)
            m_bottomGrid->addWidget(m_closeBtn, 0, 5);
    }
    else
    {
        // 窄屏：第二行放文件夹与关闭，避免按钮被挤扁
        if (m_folderBtn)
            m_bottomGrid->addWidget(m_folderBtn, 1, 0, 1, 2, Qt::AlignLeft);
        if (m_closeBtn)
            m_bottomGrid->addWidget(m_closeBtn, 1, 2, 1, 1, Qt::AlignLeft);
    }
}

void DataPackManagerDialog::resizeEvent(QResizeEvent *event)
{
    AppDialogBase::resizeEvent(event);
    applyResponsiveLayout();
    QTimer::singleShot(0, this, [this]() { updateNameElide(); });
}

void DataPackManagerDialog::selectPack(int index)
{
    if (index < 0 || index >= static_cast<int>(m_packs.size()))
        return;
    if (m_selected == index)
    {
        updateRowSelectionStyles();
        return;
    }
    m_selected = index;
    updateRowSelectionStyles();
    rebuildDetailPanel();
    refreshActionState();
}

void DataPackManagerDialog::togglePack(int index, bool enabled)
{
    if (index < 0 || index >= static_cast<int>(m_packs.size()))
        return;

    const QString key = packKeyOf(m_packs.at(index));
    const QString displayName = m_packs.at(index).fileName;

    m_packs[index].enabled = enabled;
    // 启用包必须排在禁用包之前：写回 level.dat 的顺序就是加载顺序
    std::stable_partition(m_packs.begin(), m_packs.end(),
                          [](const DataPackInfo &pack) { return pack.enabled; });

    m_selected = indexOfPack(key);
    rebuildRows();
    rebuildDetailPanel();

    persistStates(enabled ? tr("已启用「%1」").arg(displayName)
                          : tr("已禁用「%1」").arg(displayName));
}

void DataPackManagerDialog::movePack(int index, bool up)
{
    if (index < 0 || index >= static_cast<int>(m_packs.size()))
        return;
    if (!m_packs.at(index).enabled)
        return;

    QList<int> enabledIndices;
    const int packCount = static_cast<int>(m_packs.size());
    for (int i = 0; i < packCount; ++i)
    {
        if (m_packs.at(i).enabled)
            enabledIndices.append(i);
    }
    const int pos = enabledIndices.indexOf(index);
    if (pos < 0)
        return;
    const int targetPos = up ? pos - 1 : pos + 1;
    if (targetPos < 0 || targetPos >= static_cast<int>(enabledIndices.size()))
        return;

    const QString key = packKeyOf(m_packs.at(index));
    const QString displayName = m_packs.at(index).fileName;
    m_packs.swapItemsAt(index, enabledIndices.at(targetPos));

    m_selected = indexOfPack(key);
    rebuildRows();
    rebuildDetailPanel();

    persistStates(up ? tr("已上移「%1」").arg(displayName)
                     : tr("已下移「%1」").arg(displayName));
}

void DataPackManagerDialog::removeSelectedPack()
{
    const DataPackInfo *info = selectedPack();
    if (!info)
        return;
    const DataPackInfo target = *info;

    const QString questionText =
        target.exists
            ? tr("确定要删除数据包「%1」吗？\n文件会被永久删除，无法恢复。").arg(target.fileName)
            : tr("数据包「%1」的文件已经不存在了，要从数据包列表里移除这条记录吗？")
                  .arg(target.fileName);
    if (AppMessageBox::question(this, tr("删除数据包"), questionText,
                                AppMessageBox::Yes | AppMessageBox::No,
                                AppMessageBox::No)
        != AppMessageBox::Yes)
        return;

    if (target.exists)
    {
        QString error;
        if (!DataPackManager::removePack(m_worldDir, target, &error))
        {
            AppMessageBox::warning(this, tr("删除失败"),
                                   error.isEmpty() ? tr("无法删除这个数据包。") : error);
            reload();
            return;
        }
    }

    // 文件删掉后还要把 level.dat 里的条目一起去掉，否则会留下「文件缺失」的残留记录
    const QString key = packKeyOf(target);
    for (int i = 0; i < static_cast<int>(m_packs.size()); ++i)
    {
        if (packKeyOf(m_packs.at(i)) == key)
        {
            m_packs.removeAt(i);
            break;
        }
    }
    m_selected = -1;

    QString stateError;
    if (!DataPackManager::applyStates(m_worldDir, m_packs, &stateError))
    {
        reload();
        if (m_statusLabel)
            m_statusLabel->setText(tr("已删除「%1」的文件，但存档里的数据包列表没能更新。")
                                       .arg(target.fileName));
        return;
    }

    reload();
    if (m_statusLabel)
        m_statusLabel->setText(tr("已删除「%1」").arg(target.fileName));
}

void DataPackManagerDialog::showImportMenu()
{
    if (!m_importBtn)
        return;

    QMenu menu(this);
    menu.setToolTipsVisible(true);

    const QStringList library = DataPackManager::listInstanceLibrary(m_instancePath);
    const int maxItems = 25;
    const int itemCount = qMin(maxItems, static_cast<int>(library.size()));
    for (int i = 0; i < itemCount; ++i)
    {
        const QString path = library.at(i);
        QAction *action = menu.addAction(QFileInfo(path).fileName());
        action->setToolTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path]() { importPackFrom(path); });
    }
    if (library.size() > maxItems)
    {
        QAction *moreAction = menu.addAction(tr("还有 %1 个未列出…")
                                                 .arg(library.size() - maxItems));
        moreAction->setEnabled(false);
    }
    if (library.isEmpty())
    {
        QAction *emptyAction = menu.addAction(tr("实例数据包库为空"));
        emptyAction->setEnabled(false);
    }

    if (!library.isEmpty())
        menu.addSeparator();

    QAction *pickAction = menu.addAction(tr("从电脑选择文件…"));
    connect(pickAction, &QAction::triggered, this, &DataPackManagerDialog::chooseImportFile);

    menu.exec(m_importBtn->mapToGlobal(QPoint(0, m_importBtn->height())));
}

void DataPackManagerDialog::chooseImportFile()
{
    const QString libraryDir = m_instancePath + QStringLiteral("/datapacks");
    const QString startDir = QDir(libraryDir).exists() ? libraryDir : QDir::homePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择数据包文件"), startDir,
        tr("数据包文件 (*.zip *.jar);;所有文件 (*.*)"));
    if (!path.isEmpty())
        importPackFrom(path);
}

void DataPackManagerDialog::importPackFrom(const QString &sourcePath)
{
    if (sourcePath.isEmpty())
        return;

    QString destName;
    QString error;
    if (!DataPackManager::importPack(m_worldDir, sourcePath, &destName, &error))
    {
        AppMessageBox::warning(this, tr("导入失败"),
                               error.isEmpty() ? tr("无法导入这个数据包。") : error);
        return;
    }

    reload();

    // 选中刚导入的那个数据包
    int newIndex = -1;
    if (!destName.isEmpty())
    {
        const int packCount = static_cast<int>(m_packs.size());
        for (int i = 0; i < packCount; ++i)
        {
            if (m_packs.at(i).fileName == destName)
            {
                newIndex = i;
                break;
            }
        }
    }
    if (newIndex >= 0)
        selectPack(newIndex);

    // 新复制的文件还没登记进 level.dat：顺手写回，游戏里才会按这里的顺序加载
    const QString shownName =
        destName.isEmpty() ? QFileInfo(sourcePath).fileName() : destName;
    persistStates(tr("已导入并启用「%1」").arg(shownName));
}

void DataPackManagerDialog::openDatapacksFolder()
{
    const QString folder = m_worldDir + QStringLiteral("/datapacks");
    if (!QDir().mkpath(folder))
    {
        if (m_statusLabel)
            m_statusLabel->setText(tr("无法创建数据包文件夹：%1")
                                       .arg(QDir::toNativeSeparators(folder)));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

bool DataPackManagerDialog::packMatchesFilter(int index) const
{
    if (m_search.isEmpty())
        return true;
    if (index < 0 || index >= static_cast<int>(m_packs.size()))
        return false;
    const DataPackInfo &info = m_packs.at(index);
    return info.fileName.contains(m_search, Qt::CaseInsensitive)
           || info.description.contains(m_search, Qt::CaseInsensitive);
}

void DataPackManagerDialog::persistStates(const QString &successText)
{
    QString error;
    if (!DataPackManager::applyStates(m_worldDir, m_packs, &error))
    {
        AppMessageBox::warning(this, tr("保存失败"),
                               error.isEmpty() ? tr("无法把数据包状态写回世界存档。") : error);
        // 界面回滚到磁盘上的真实状态
        reload();
        if (m_statusLabel)
            m_statusLabel->setText(tr("改动未保存，已重新读取世界里的真实状态。"));
        return;
    }
    if (m_statusLabel)
        m_statusLabel->setText(successText);
}

const DataPackInfo *DataPackManagerDialog::selectedPack() const
{
    if (m_selected < 0 || m_selected >= static_cast<int>(m_packs.size()))
        return nullptr;
    return &m_packs.at(m_selected);
}

int DataPackManagerDialog::indexOfPack(const QString &key) const
{
    if (key.isEmpty())
        return -1;
    const int packCount = static_cast<int>(m_packs.size());
    for (int i = 0; i < packCount; ++i)
    {
        if (packKeyOf(m_packs.at(i)) == key)
            return i;
    }
    return -1;
}

QString DataPackManagerDialog::formatText(const DataPackInfo &info)
{
    if (!info.hasPackFormat || info.packFormat <= 0)
        return tr("格式未知");

    const QString formatName = DataPackManager::formatDisplayName(info.packFormat,
                                                                  info.packFormatMinor);
    if (formatName.isEmpty())
        return tr("格式未知");

    QString range = DataPackManager::versionRangeForFormat(info.packFormat,
                                                           info.packFormatMinor);
    if (range.isEmpty() && info.hasFormatRange)
    {
        // 没收录主格式时退回它声明的支持区间端点
        if (info.minFormat > 0)
            range = DataPackManager::versionRangeForFormat(info.minFormat,
                                                           info.minFormatMinor);
        if (range.isEmpty() && info.maxFormat > 0)
            range = DataPackManager::versionRangeForFormat(info.maxFormat,
                                                           info.maxFormatMinor);
    }
    if (range.isEmpty())
        return tr("格式 %1").arg(formatName);
    return tr("格式 %1（%2）").arg(formatName, range);
}

QString DataPackManagerDialog::sizeText(const DataPackInfo &info)
{
    if (!info.exists)
        return QStringLiteral("—");
    if (info.fileSize > 0)
        return formatSize(info.fileSize);
    // 压缩包即便异常为空也按文件显示，只有文件夹型数据包才标「文件夹」
    const QString lowerPath = info.filePath.toLower();
    if (lowerPath.endsWith(QStringLiteral(".zip")) || lowerPath.endsWith(QStringLiteral(".jar")))
        return formatSize(0);
    return tr("文件夹");
}

QString DataPackManagerDialog::chipStyle(const QColor &color)
{
    if (!color.isValid())
    {
        return QStringLiteral("color: rgba(128,128,128,0.95);"
                              " background-color: rgba(128,128,128,0.16);"
                              " border-radius: 8px; padding: 1px 7px; font-size: 11px;");
    }
    return QStringLiteral("color: %1; background-color: rgba(%2,%3,%4,0.16);"
                          " border-radius: 8px; padding: 1px 7px; font-size: 11px;")
        .arg(color.name())
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue());
}

QColor DataPackManagerDialog::compatColor(DataPackCompat compat)
{
    switch (compat)
    {
    case DataPackCompat::Compatible:
        return QColor("#22C55E");
    case DataPackCompat::VersionMismatch:
        return QColor("#F59E0B");
    case DataPackCompat::Invalid:
        return QColor("#EF4444");
    case DataPackCompat::Unknown:
    default:
        return QColor();
    }
}

QString DataPackManagerDialog::compatBadgeText(const DataPackInfo &info) const
{
    switch (info.compat)
    {
    case DataPackCompat::Compatible:
        return tr("兼容");
    case DataPackCompat::VersionMismatch:
    {
        // 有支持的格式区间时按区间判断偏新还是偏旧
        const bool hasRange = info.hasFormatRange && info.minFormat > 0 && info.maxFormat > 0;
        int newest = info.packFormat > 0 ? info.packFormat
                                         : (hasRange ? info.maxFormat : 0);
        int newestMinor = info.packFormat > 0 ? info.packFormatMinor
                                              : (hasRange ? info.maxFormatMinor : 0);
        int oldest = info.packFormat > 0 ? info.packFormat
                                         : (hasRange ? info.minFormat : 0);
        int oldestMinor = info.packFormat > 0 ? info.packFormatMinor
                                              : (hasRange ? info.minFormatMinor : 0);
        if (hasRange)
        {
            if (info.maxFormat > newest)
            {
                newest = info.maxFormat;
                newestMinor = info.maxFormatMinor;
            }
            if (info.minFormat < oldest || oldest <= 0)
            {
                oldest = info.minFormat;
                oldestMinor = info.minFormatMinor;
            }
        }
        if (m_worldFormatMajor > 0 && newest > 0
            && (newest < m_worldFormatMajor
                || (newest == m_worldFormatMajor && newestMinor < m_worldFormatMinor)))
            return tr("版本偏旧");
        if (m_worldFormatMajor > 0 && oldest > 0
            && (oldest > m_worldFormatMajor
                || (oldest == m_worldFormatMajor && oldestMinor > m_worldFormatMinor)))
            return tr("版本偏新");
        return tr("版本不匹配");
    }
    case DataPackCompat::Invalid:
        return tr("无效");
    case DataPackCompat::Unknown:
    default:
        return tr("无法判断");
    }
}

// ============================
// 在线检查更新
// ============================

void DataPackManagerDialog::checkUpdates()
{
    if (m_checkingUpdate || m_updatingPacks)
        return;

    m_sha1ByPath.clear();
    m_installedBySha1.clear();
    m_updateByPath.clear();

    QList<QString> sha1s;
    const int packCount = static_cast<int>(m_packs.size());
    for (int i = 0; i < packCount; ++i)
    {
        const DataPackInfo &info = m_packs.at(i);
        if (!info.isFile || !info.exists || info.filePath.isEmpty())
            continue;
        const QString lower = info.filePath.toLower();
        if (!lower.endsWith(QStringLiteral(".zip")) && !lower.endsWith(QStringLiteral(".jar")))
            continue;
        QFile file(info.filePath);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(&file);
        const QString sha1 = QString::fromLatin1(hash.result().toHex());
        file.close();
        if (sha1.isEmpty())
            continue;
        m_sha1ByPath.insert(info.filePath, sha1);
        sha1s.append(sha1);
    }
    sha1s.removeDuplicates();

    if (sha1s.isEmpty())
    {
        if (m_statusLabel)
            m_statusLabel->setText(tr("这个世界里没有可联网检查更新的数据包文件"));
        return;
    }

    m_checkingUpdate = true;
    if (m_checkUpdateBtn)
    {
        m_checkUpdateBtn->setEnabled(false);
        m_checkUpdateBtn->setText(tr("检查中..."));
    }
    if (m_updateBtn)
        m_updateBtn->hide();
    if (m_statusLabel)
        m_statusLabel->setText(tr("正在检查 %1 个数据包的更新...").arg(sha1s.size()));

    // 与模组 / 资源包页走同一条通路：跟随全局镜像设置，官方接口连不上时自动回退
    m_updateApi->setBaseUrl(McimHelper::modrinthApiBaseUrl());
    m_updateApi->matchHashes(sha1s);
}

void DataPackManagerDialog::onHashesMatched(const QMap<QString, ModVersionFile> &versions)
{
    m_installedBySha1 = versions;
    QStringList sha1s = m_sha1ByPath.values();
    sha1s.removeDuplicates();
    // 数据包不按加载器与游戏版本过滤，只要同项目有更新的文件就算更新
    m_updateApi->checkModUpdates(sha1s, QStringList(), QStringList());
}

void DataPackManagerDialog::onUpdatesChecked(const QMap<QString, ModVersionFile> &versions)
{
    m_checkingUpdate = false;
    if (m_checkUpdateBtn)
    {
        m_checkUpdateBtn->setEnabled(true);
        m_checkUpdateBtn->setText(tr("检查更新"));
    }

    m_updateByPath.clear();
    for (auto it = versions.constBegin(); it != versions.constEnd(); ++it)
    {
        const QString &sha1 = it.key();
        if (!m_installedBySha1.contains(sha1))
            continue;
        const ModVersionFile &latest = it.value();
        const ModVersionFile installed = m_installedBySha1.value(sha1);

        bool hasUpdate = false;
        if (latest.sha1.isEmpty() || installed.sha1.isEmpty())
        {
            hasUpdate = latest.sha1 != installed.sha1;
        }
        else if (latest.sha1 != installed.sha1
                 && installed.datePublished.isValid()
                 && latest.datePublished > installed.datePublished)
        {
            hasUpdate = true;
        }
        if (!hasUpdate)
            continue;

        // 同一个 SHA-1 可能对应多个文件（复制过一份），都要标出来
        for (auto pathIt = m_sha1ByPath.constBegin(); pathIt != m_sha1ByPath.constEnd(); ++pathIt)
        {
            if (pathIt.value() == sha1)
                m_updateByPath.insert(pathIt.key(), latest);
        }
    }

    // 重建行卡片，让「可更新到 x」徽标显示出来
    rebuildRows();
    refreshUpdateState();

    if (m_statusLabel)
    {
        if (m_updateByPath.isEmpty())
            m_statusLabel->setText(tr("所有数据包都是最新版本"));
        else
            m_statusLabel->setText(tr("发现有 %1 个数据包可更新，点「更新数据包」一次换好")
                                       .arg(m_updateByPath.size()));
    }
}

void DataPackManagerDialog::onUpdateCheckFailed(const QString &error)
{
    Q_UNUSED(error);
    m_checkingUpdate = false;
    if (m_checkUpdateBtn)
    {
        m_checkUpdateBtn->setEnabled(true);
        m_checkUpdateBtn->setText(tr("检查更新"));
    }
    if (m_statusLabel)
        m_statusLabel->setText(tr("检查更新失败，请检查网络后重试"));
}

void DataPackManagerDialog::refreshUpdateState()
{
    if (m_updateBtn)
    {
        m_updateBtn->setVisible(!m_updateByPath.isEmpty());
        m_updateBtn->setEnabled(!m_updateByPath.isEmpty() && !m_updatingPacks);
        m_updateBtn->setText(m_updatingPacks ? tr("更新中...") : tr("更新数据包"));
    }
}

void DataPackManagerDialog::updateCheckedPacks()
{
    if (m_updatingPacks || m_updateByPath.isEmpty())
        return;
    if (m_runningLabel && m_runningLabel->isVisible())
    {
        AppMessageBox::warning(this, tr("数据包管理"),
                               tr("游戏正在运行，请先退出游戏再更新数据包。"));
        return;
    }

    m_pendingUpdatePaths = m_updateByPath.keys();
    m_updatingPacks = true;
    if (m_checkUpdateBtn)
        m_checkUpdateBtn->setEnabled(false);
    refreshUpdateState();
    startNextPackUpdate();
}

void DataPackManagerDialog::startNextPackUpdate()
{
    if (m_pendingUpdatePaths.isEmpty())
    {
        m_updatingPacks = false;
        const int done = m_updateByPath.size();
        m_updateByPath.clear();
        if (m_checkUpdateBtn)
            m_checkUpdateBtn->setEnabled(true);
        refreshUpdateState();
        reload();
        if (m_statusLabel)
        {
            m_statusLabel->setText(done > 0
                ? tr("%1 个数据包已更新完成，进入世界后生效").arg(done)
                : tr("数据包更新已完成"));
        }
        return;
    }

    const QString path = m_pendingUpdatePaths.takeFirst();
    const auto updateIt = m_updateByPath.constFind(path);
    if (updateIt == m_updateByPath.constEnd() || updateIt->downloadUrl.isEmpty()
        || !QFile::exists(path))
    {
        startNextPackUpdate();
        return;
    }

    if (!m_net)
        m_net = new QNetworkAccessManager(this);

    const int total = m_pendingUpdatePaths.size() + 1;
    if (m_statusLabel)
        m_statusLabel->setText(tr("正在下载 %1（还剩 %2 个）...")
                                   .arg(QFileInfo(path).fileName()).arg(total));

    QNetworkRequest request{QUrl(updateIt->downloadUrl)};
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || body.isEmpty())
        {
            if (m_statusLabel)
                m_statusLabel->setText(tr("下载失败：%1").arg(reply->errorString()));
            startNextPackUpdate();
            return;
        }

        // 先落到临时文件，确认写完整后再覆盖原文件，避免下载中断把旧数据包写坏
        const QString suffix = QFileInfo(path).suffix();
        const QString tempPath = QDir(QDir::tempPath()).filePath(
            QStringLiteral("blockbox_datapack_update.%1")
                .arg(suffix.isEmpty() ? QStringLiteral("zip") : suffix));
        QFile tempFile(tempPath);
        bool ok = tempFile.open(QIODevice::WriteOnly);
        if (ok)
        {
            ok = tempFile.write(body) == body.size();
            tempFile.close();
        }
        if (ok)
        {
            QFile::remove(path);
            ok = QFile::copy(tempPath, path);
        }
        QFile::remove(tempPath);

        if (!ok && m_statusLabel)
            m_statusLabel->setText(tr("写入数据包失败：%1").arg(QFileInfo(path).fileName()));

        // 同名覆盖，level.dat 里的条目、启用状态和加载顺序都不用动
        m_updateByPath.remove(path);
        startNextPackUpdate();
    });
}

