#include "ProjectionManagePage.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/plugin/PluginManager.h"
#include "components/ProjectionBlockEditorDialog.h"
#include "components/AppFileDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QMimeData>
#include "components/AppMessageBox.h"
#include <QSpacerItem>
#include <QUrl>

ProjectionManagePage::ProjectionManagePage(QWidget *parent)
    : QWidget(parent)
    , m_searchEdit(nullptr)
    , m_statsLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGridLayout(nullptr)
    , m_emptyLabel(nullptr)
    , m_bottomBar(nullptr)
    , m_openFolderBtn(nullptr)
    , m_addProjectionBtn(nullptr)
    , m_deleteProjectionBtn(nullptr)
    , m_blockEditorBtn(nullptr)
{
    initUI();
}

ProjectionManagePage::~ProjectionManagePage()
{
}

void ProjectionManagePage::setInstancePath(const QString &path)
{
    if (m_instancePath != path)
    {
        m_instancePath = path;
        if (!path.isEmpty())
        {
            loadProjections();
            placeCards();
        }
    }
}

void ProjectionManagePage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // 顶部搜索/统计条：微透明玻璃态卡片
    QWidget *filterCard = new QWidget(this);
    filterCard->setObjectName("filterCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(8);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("modSearchEdit");
    m_searchEdit->setPlaceholderText(tr("搜索投影名称..."));
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->setClearButtonEnabled(true);
    cardLayout->addWidget(m_searchEdit);

    QHBoxLayout *statsBar = new QHBoxLayout();
    statsBar->setSpacing(8);
    m_statsLabel = new QLabel();
    m_statsLabel->setObjectName("statusLabel");
    statsBar->addWidget(m_statsLabel);
    statsBar->addStretch();
    cardLayout->addLayout(statsBar);
    mainLayout->addWidget(filterCard);

    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_cardContainer = new QWidget();
    m_cardGridLayout = new QGridLayout(m_cardContainer);
    m_cardGridLayout->setContentsMargins(14, 0, 14, 0);
    m_cardGridLayout->setSpacing(8);
    m_scrollArea->setWidget(m_cardContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    m_emptyLabel = new QLabel(tr("暂无投影文件，可点击底部「添加投影」按钮导入"));
    m_emptyLabel->setObjectName("exampleContentLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    m_cardGridLayout->addWidget(m_emptyLabel, 0, 0, 1, 1, Qt::AlignCenter);

    m_bottomBar = new QWidget();
    m_bottomBar->setObjectName("modBottomBar");
    m_bottomBar->setFixedHeight(48);
    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomBar);
    bottomLayout->setContentsMargins(14, 6, 14, 6);
    bottomLayout->setSpacing(10);

    QColor themeColor = ThemeManager::instance()->currentThemeColor();

    m_openFolderBtn = new QPushButton(tr("打开文件夹"));
    m_openFolderBtn->setObjectName("bottomActionBtn");
    m_openFolderBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/folder.svg", themeColor, 18));
    m_openFolderBtn->setIconSize(QSize(18, 18));

    m_addProjectionBtn = new QPushButton(tr("添加投影"));
    m_addProjectionBtn->setObjectName("bottomActionBtn");
    m_addProjectionBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/install.svg", themeColor, 18));
    m_addProjectionBtn->setIconSize(QSize(18, 18));

    m_deleteProjectionBtn = new QPushButton(tr("删除投影"));
    m_deleteProjectionBtn->setObjectName("bottomActionBtn");
    m_deleteProjectionBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    m_deleteProjectionBtn->setIconSize(QSize(18, 18));

    m_blockEditorBtn = new QPushButton(tr("编辑投影方块"));
    m_blockEditorBtn->setObjectName("bottomActionBtn");
    m_blockEditorBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/brush.svg", QColor("#4CAF50"), 18));
    m_blockEditorBtn->setIconSize(QSize(18, 18));
    m_blockEditorBtn->setToolTip(tr("3D 立体编辑投影方块（放置/破坏/批处理）\n也可从方块盒子文件资源管理器选择投影"));

    bottomLayout->addWidget(m_openFolderBtn);
    bottomLayout->addWidget(m_addProjectionBtn);
    bottomLayout->addWidget(m_deleteProjectionBtn);
    bottomLayout->addWidget(m_blockEditorBtn);
    bottomLayout->addStretch();
    mainLayout->addWidget(m_bottomBar);

    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        QString dirPath = projectionsDirPath();
        QDir dir(dirPath);
        if (!dir.exists())
            dir.mkpath(".");
        QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
    });

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ProjectionManagePage::onSearchTextChanged);
    connect(m_addProjectionBtn, &QPushButton::clicked,
            this, &ProjectionManagePage::onAddProjectionClicked);
    connect(m_deleteProjectionBtn, &QPushButton::clicked,
            this, &ProjectionManagePage::onDeleteProjectionClicked);
    connect(m_blockEditorBtn, &QPushButton::clicked,
            this, &ProjectionManagePage::onBlockEditorClicked);

    // 原生插件（投影方块编辑器）可用性联动
    connect(PluginManager::instance(), &PluginManager::pluginEnabledChanged, this,
            [this](const QString &, bool) { updateNativeEditorButton(); });
    updateNativeEditorButton();
}

void ProjectionManagePage::updateNativeEditorButton()
{
    // 仅当「投影方块编辑器」原生插件已安装并启用时显示该按钮
    const bool available = PluginManager::instance()
        ->nativeAvailable(QStringLiteral("projection_editor"));
    if (m_blockEditorBtn)
        m_blockEditorBtn->setVisible(available);
}

QString ProjectionManagePage::projectionsDirPath() const
{
    if (m_instancePath.isEmpty())
        return QString();
    return m_instancePath + "/schematics";
}

void ProjectionManagePage::loadProjections()
{
    m_projectionFiles.clear();
    QString dirPath = projectionsDirPath();
    if (dirPath.isEmpty())
        return;

    QDir dir(dirPath);
    if (!dir.exists())
        return;

    QStringList nameFilters;
    nameFilters << "*.litematic" << "*.nbt" << "*.schematic" << "*.schem";
    QFileInfoList files = dir.entryInfoList(nameFilters, QDir::Files, QDir::Name);

    for (const QFileInfo &fi : files)
        m_projectionFiles.append(fi);
}

void ProjectionManagePage::clearCards()
{
    QLayoutItem *item;
    while ((item = m_cardGridLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
        {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
}

void ProjectionManagePage::placeCards()
{
    clearCards();

    if (m_projectionFiles.isEmpty())
    {
        showEmptyHint(true);
        m_statsLabel->setText(tr("共 0 个投影"));
        return;
    }
    showEmptyHint(false);

    QColor themeColor = ThemeManager::instance()->currentThemeColor();
    int row = 0;
    int visibleCount = 0;

    for (int i = 0; i < m_projectionFiles.size(); ++i)
    {
        const QFileInfo &fi = m_projectionFiles[i];
        QString fileName = fi.completeBaseName();

        if (!m_currentSearch.isEmpty())
        {
            if (!fileName.contains(m_currentSearch, Qt::CaseInsensitive))
                continue;
        }

        QWidget *card = new QWidget(m_cardContainer);
        card->setObjectName("modCardListItem");
        card->setProperty("cardRole", "container");
        card->setFixedHeight(76);
        card->setCursor(Qt::PointingHandCursor);
        card->setToolTip(tr("双击编辑投影"));
        card->setProperty("projectionFilePath", fi.absoluteFilePath());
        card->installEventFilter(this);

        QHBoxLayout *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->setSpacing(14);

        QLabel *iconLabel = new QLabel(card);
        iconLabel->setObjectName("modCardIcon");
        iconLabel->setProperty("cardRole", "icon");
        iconLabel->setFixedSize(48, 48);
        QIcon bpIcon = IconHelper::loadColoredIcon(
            ":/Images/Icons/list.svg", themeColor, 36);
        iconLabel->setPixmap(bpIcon.pixmap(36, 36));
        iconLabel->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(iconLabel);

        QVBoxLayout *infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(6);
        infoLayout->setContentsMargins(0, 0, 0, 0);

        QLabel *nameLabel = new QLabel(fileName, card);
        nameLabel->setObjectName("modCardName");
        nameLabel->setProperty("cardRole", "name");
        infoLayout->addWidget(nameLabel);

        QWidget *chipsWidget = new QWidget(card);
        QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
        chipsLayout->setContentsMargins(0, 0, 0, 0);
        chipsLayout->setSpacing(6);

        qint64 size = fi.size();
        QString sizeStr;
        if (size < 1024)
            sizeStr = QString("%1 B").arg(size);
        else if (size < 1024 * 1024)
            sizeStr = QString("%1 KB").arg(size / 1024.0, 0, 'f', 1);
        else
            sizeStr = QString("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 1);

        QLabel *sizeChip = new QLabel(sizeStr, chipsWidget);
        sizeChip->setObjectName("modChip");
        sizeChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(sizeChip);

        if (fi.lastModified().isValid())
        {
            QLabel *dateChip = new QLabel(
                fi.lastModified().toString("yyyy-MM-dd"), chipsWidget);
            dateChip->setObjectName("modChip");
            dateChip->setProperty("cardRole", "chip");
            chipsLayout->addWidget(dateChip);
        }

        QLabel *extChip = new QLabel(fi.suffix(), chipsWidget);
        extChip->setObjectName("modChip");
        extChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(extChip);

        chipsLayout->addStretch();
        infoLayout->addWidget(chipsWidget);
        cardLayout->addLayout(infoLayout, 1);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(4);
        btnLayout->setContentsMargins(0, 0, 0, 0);

        auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton* {
            QPushButton *btn = new QPushButton(card);
            btn->setObjectName("modCardActionBtn");
            btn->setProperty("cardRole", "actionBtn");
            btn->setFixedSize(32, 32);
            btn->setToolTip(tip);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 18));
            btn->setIconSize(QSize(18, 18));
            return btn;
        };

        QPushButton *editBtn = createBtn(":/Images/Icons/modify.svg", tr("编辑投影"));
        connect(editBtn, &QPushButton::clicked, this, [this, fi]() {
            emit projectionEditRequested(fi.absoluteFilePath());
        });
        btnLayout->addWidget(editBtn);

        QPushButton *blockBtn = createBtn(":/Images/Icons/brush.svg", tr("3D 编辑投影方块"));
        connect(blockBtn, &QPushButton::clicked, this, [this, fi]() {
            const bool changed = ProjectionBlockEditorDialog::editProjection(this, fi.absoluteFilePath(), m_instancePath);
            if (changed)
            {
                loadProjections();
                placeCards();
            }
        });
        btnLayout->addWidget(blockBtn);

        QPushButton *folderBtn = createBtn(":/Images/Icons/folder.svg", tr("打开文件位置"));
        connect(folderBtn, &QPushButton::clicked, this, [fi]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
        });
        btnLayout->addWidget(folderBtn);

        QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除"));
        deleteBtn->setIcon(IconHelper::loadColoredIcon(
            ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
        connect(deleteBtn, &QPushButton::clicked, this, [this, fi]() {
            deleteProjectionFile(fi.absoluteFilePath());
        });
        btnLayout->addWidget(deleteBtn);

        cardLayout->addLayout(btnLayout);

        m_cardGridLayout->addWidget(card, row, 0, 1, 1);
        row++;
        visibleCount++;
    }

    QSpacerItem *spacer = new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding);
    m_cardGridLayout->addItem(spacer, row + 1, 0);

    m_statsLabel->setText(tr("共 %1 个投影").arg(visibleCount));
}

void ProjectionManagePage::showEmptyHint(bool show)
{
    m_emptyLabel->setVisible(show);
}

void ProjectionManagePage::onSearchTextChanged(const QString &text)
{
    m_currentSearch = text;
    placeCards();
}

void ProjectionManagePage::onAddProjectionClicked()
{
    QString dirPath = projectionsDirPath();
    QDir dir(dirPath);
    if (!dir.exists())
        dir.mkpath(".");

    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    if (!mimeData->hasUrls())
    {
        AppMessageBox::information(this, tr("添加投影"),
            tr("请先复制投影文件（.litematic / .nbt / .schematic / .schem）到剪贴板，然后点击「添加投影」。\n\n"
               "也可以点击「打开文件夹」手动放入文件。\n\n"
               "投影文件由 Litematica 等开源模组生成，存放于 schematics 目录下。"));
        return;
    }

    QStringList validSuffixes = { ".litematic", ".nbt", ".schematic", ".schem" };
    int copiedCount = 0;

    for (const QUrl &url : mimeData->urls())
    {
        QString srcPath = url.toLocalFile();
        if (srcPath.isEmpty())
            continue;

        QFileInfo fi(srcPath);
        bool isValid = false;
        for (const QString &suf : validSuffixes)
        {
            if (fi.fileName().endsWith(suf, Qt::CaseInsensitive))
            {
                isValid = true;
                break;
            }
        }
        if (!isValid)
            continue;

        QString destPath = dirPath + "/" + fi.fileName();
        if (QFile::exists(destPath))
        {
            int ret = AppMessageBox::question(this, tr("文件已存在"),
                tr("文件 \"%1\" 已存在，是否覆盖？").arg(fi.fileName()),
                AppMessageBox::Yes | AppMessageBox::No | AppMessageBox::Cancel);
            if (ret == AppMessageBox::Cancel)
                break;
            if (ret == AppMessageBox::No)
                continue;
            QFile::remove(destPath);
        }

        if (QFile::copy(srcPath, destPath))
            copiedCount++;
        else
            AppMessageBox::warning(this, tr("导入失败"),
                tr("无法复制文件 \"%1\"").arg(fi.fileName()));
    }

    if (copiedCount > 0)
    {
        loadProjections();
        placeCards();
    }
}

void ProjectionManagePage::onDeleteProjectionClicked()
{
    if (m_projectionFiles.isEmpty())
        return;

    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("删除投影"),
        tr("确定要删除所有投影文件吗？此操作不可撤销。"),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);

    if (reply == AppMessageBox::Yes)
    {
        for (const QFileInfo &fi : m_projectionFiles)
            QFile::remove(fi.absoluteFilePath());
        loadProjections();
        placeCards();
    }
}

void ProjectionManagePage::deleteProjectionFile(const QString &filePath)
{
    QFileInfo fi(filePath);
    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("删除投影"),
        tr("确定要删除投影 \"%1\" 吗？此操作不可撤销。").arg(fi.completeBaseName()),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);

    if (reply == AppMessageBox::Yes)
    {
        QFile::remove(filePath);
        loadProjections();
        placeCards();
    }
}

bool ProjectionManagePage::eventFilter(QObject *watched, QEvent *event)
{
    // 双击卡片进入投影编辑
    if (event->type() == QEvent::MouseButtonDblClick)
    {
        QWidget *card = qobject_cast<QWidget *>(watched);
        if (card && card->property("projectionFilePath").isValid())
        {
            emit projectionEditRequested(card->property("projectionFilePath").toString());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ProjectionManagePage::onBlockEditorClicked()
{
    QString dirPath = projectionsDirPath();
    QDir dir(dirPath);
    if (!dir.exists())
        dir.mkpath(".");

    // 优先用方块盒子文件资源管理器选择 .litematic 投影
    const QString filter = tr("投影文件 (*.litematic *.nbt *.schematic *.schem);;Litematica (*.litematic);;全部文件 (*)");
    const QString file = AppFileDialog::getOpenFileName(this, tr("选择投影以编辑方块"),
                                                        dirPath, filter);
    if (file.isEmpty())
        return;

    const QString lowered = file.toLower();
    if (!lowered.endsWith(QStringLiteral(".litematic")))
    {
        // 非 litematic：先转换为 litematic 临时文件再编辑
        AppMessageBox::information(this, tr("提示"),
            tr("仅支持编辑 .litematic 格式的投影。\n请先选择 .litematic 文件，或使用现有「编辑投影」功能转换格式。"));
        return;
    }

    const bool changed = ProjectionBlockEditorDialog::editProjection(this, file, m_instancePath);
    if (changed)
    {
        // 刷新列表（文件大小/修改时间可能变化）
        loadProjections();
        placeCards();
    }
}

