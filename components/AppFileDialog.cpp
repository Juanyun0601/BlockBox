#include "AppFileDialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "components/LocalCategoryDialog.h"
#include "utils/LocalCategoryManager.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/bedrock/BedrockInstanceManager.h"
#include "utils/mod/CurseForgeAPI.h"
#include "utils/mod/ModrinthAPI.h"

namespace {

// 可点击卡片的事件过滤器（不需要 moc）
class ClickableFilter : public QObject {
public:
    using Fn = std::function<void()>;
    ClickableFilter(QWidget *w, Fn fn)
        : QObject(w), m_fn(std::move(fn)) {
        w->setCursor(Qt::PointingHandCursor);
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(e);
            if (me->button() == Qt::LeftButton) {
                if (m_fn)
                    m_fn();
                return true;
            }
        }
        return QObject::eventFilter(o, e);
    }

private:
    Fn m_fn;
};

// 右键弹出菜单的事件过滤器（不需要 moc）
class ContextMenuFilter : public QObject {
public:
    using Fn = std::function<void()>;
    ContextMenuFilter(QWidget *w, Fn fn)
        : QObject(w), m_fn(std::move(fn)) {}

protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(e);
            if (me->button() == Qt::RightButton) {
                if (m_fn)
                    m_fn();
                return true;
            }
        }
        return QObject::eventFilter(o, e);
    }

private:
    Fn m_fn;
};

QString driveDisplayName(const QString &path)
{
    if (path.isEmpty())
        return QString();
    const QString p = path;
    if (p.length() >= 2 && p.at(1) == QLatin1Char(':')) {
        return QStringLiteral("本地磁盘 (%1)").arg(p.left(1).toUpper());
    }
    return p;
}

} // namespace

AppFileDialog::AppFileDialog(QWidget *parent, Mode mode)
    : AppDialogBase(parent)
    , m_mode(mode)
{
    setObjectName(QStringLiteral("appFileDialog"));
    setWindowTitle(tr("选择文件"));
    m_catManager = new LocalCategoryManager(this);
    initUI();
    initStyle();
    applySelectionToVisibleWidget();
}

void AppFileDialog::setInitialDirectory(const QString &path)
{
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return;
    goTo(path);
}

void AppFileDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)
        && (event->key() == Qt::Key_L)) {
        if (m_browseMode == BrowseMode::System) {
            enterAddrEditing();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_addrEditing) {
            exitAddrEditing();
            event->accept();
            return;
        }
    }
    AppDialogBase::keyPressEvent(event);
}

void AppFileDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setMinimumSize(760, 540);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 18, 24, 18);
    cardLayout->setSpacing(10);

    cardLayout->addWidget(buildHeader());

    QWidget *toolbar = buildToolbar();
    cardLayout->addWidget(toolbar);

    m_chipBar = buildFilterBar();
    cardLayout->addWidget(m_chipBar);

    m_instCatBar = buildInstCategoryBar();
    m_instCatBar->hide();
    cardLayout->addWidget(m_instCatBar);

    m_stepperBar = buildStepperBar();
    m_stepperBar->hide();
    cardLayout->addWidget(m_stepperBar);

    cardLayout->addWidget(m_resStepper);

    m_bodyStack = new QStackedWidget(card);
    m_bodyStack->setObjectName(QStringLiteral("fdBodyStack"));
    m_bodyStack->addWidget(buildBrowserPage());
    m_bodyStack->addWidget(buildInstPages());
    m_bodyStack->addWidget(buildNetPage());
    m_bodyStack->addWidget(buildResPages());
    cardLayout->addWidget(m_bodyStack, 1);

    cardLayout->addWidget(buildFooter());

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(card, 0, 0);

    // 系统式默认进入
    m_browseMode = BrowseMode::System;
    loadQuickAccess();
    applyFilter(QString());
    setupResApis();
    enterSystemMode();

    connect(m_closeBtn, &QToolButton::clicked, this, &QDialog::reject);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_okBtn, &QPushButton::clicked, this, &AppFileDialog::acceptResult);

    connect(m_backBtn, &QToolButton::clicked, this, &AppFileDialog::goBack);
    connect(m_forwardBtn, &QToolButton::clicked, this, &AppFileDialog::goForward);
    connect(m_upBtn, &QToolButton::clicked, this, &AppFileDialog::goUpDir);

    connect(m_addrBtn, &QToolButton::clicked, this, &AppFileDialog::enterAddrEditing);
    connect(m_addrEdit, &QLineEdit::returnPressed, this, &AppFileDialog::addrNavigate);
    connect(m_addrEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_addrEditing)
            exitAddrEditing();
    });

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_searchText = t.trimmed();
        populate();
    });

    connect(m_netEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        updateNetStatus();
    });
    connect(m_netEdit, &QLineEdit::returnPressed, this, &AppFileDialog::acceptResult);

    connect(m_resSearchEdit, &QLineEdit::returnPressed, this, [this]() {
        m_resPage = 0;
        performResSearch(0);
    });
    connect(m_resSearchBtn, &QPushButton::clicked, this, [this]() {
        m_resPage = 0;
        performResSearch(0);
    });
    connect(m_resPrevBtn, &QPushButton::clicked, this, [this]() {
        if (m_resPage > 0)
            performResSearch(m_resPage - 1);
    });
    connect(m_resNextBtn, &QPushButton::clicked, this, [this]() {
        performResSearch(m_resPage + 1);
    });

    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        switch (idx) {
        case 1: m_sortKey = QStringLiteral("date"); break;
        case 2: m_sortKey = QStringLiteral("type"); break;
        case 3: m_sortKey = QStringLiteral("size"); break;
        default: m_sortKey = QStringLiteral("name"); break;
        }
        populate();
    });
    connect(m_tree->header(), &QHeaderView::sectionClicked, this, [this](int logicalIndex) {
        switch (logicalIndex) {
        case 1: m_sortKey = QStringLiteral("type"); break;
        case 2: m_sortKey = QStringLiteral("size"); break;
        case 3: m_sortKey = QStringLiteral("date"); break;
        default: m_sortKey = QStringLiteral("name"); break;
        }
        m_sortAsc = !m_sortAsc;
        m_sortCombo->setCurrentIndex(m_sortKey == QStringLiteral("name") ? 0
                                      : m_sortKey == QStringLiteral("date") ? 1
                                      : m_sortKey == QStringLiteral("type") ? 2 : 3);
        populate();
    });

    connect(m_listViewBtn, &QToolButton::clicked, this, [this]() {
        m_listViewBtn->setChecked(true);
        m_gridViewBtn->setChecked(false);
        m_tree->setVisible(true);
        m_grid->setVisible(false);
        applySelectionToVisibleWidget();
    });
    connect(m_gridViewBtn, &QToolButton::clicked, this, [this]() {
        m_listViewBtn->setChecked(false);
        m_gridViewBtn->setChecked(true);
        m_tree->setVisible(false);
        m_grid->setVisible(true);
        applySelectionToVisibleWidget();
    });

    connect(m_previewBtn, &QToolButton::clicked, this, [this]() {
        m_previewBtn->setChecked(!m_previewBtn->isChecked());
        m_previewPanel->setVisible(m_previewBtn->isChecked());
    });

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &AppFileDialog::onSelectionChanged);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        QTreeWidgetItem *item = m_tree->itemAt(p);
        const QString path = item ? item->data(0, Qt::UserRole).toString() : QString();
        showContextMenu(m_tree->viewport()->mapToGlobal(p), path);
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item)
            return;
        const bool isDir = item->data(0, Qt::UserRole + 1).toBool();
        const QString path = item->data(0, Qt::UserRole).toString();
        if (isDir) {
            goTo(path);
        } else if (m_mode != ExistingDirectory && m_mode != SaveFile) {
            m_fileNameEdit->setText(item->text(0));
            acceptResult();
        }
    });
    connect(m_grid, &QListWidget::itemSelectionChanged, this, &AppFileDialog::onSelectionChanged);
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_grid, &QListWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        QListWidgetItem *item = m_grid->itemAt(p);
        const QString path = item ? item->data(Qt::UserRole).toString() : QString();
        showContextMenu(m_grid->viewport()->mapToGlobal(p), path);
    });
    connect(m_grid, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item)
            return;
        const bool isDir = item->data(Qt::UserRole + 1).toBool();
        if (isDir) {
            goTo(item->data(Qt::UserRole).toString());
        } else if (m_mode != ExistingDirectory && m_mode != SaveFile) {
            m_fileNameEdit->setText(item->text());
            acceptResult();
        }
    });
    connect(m_sidebar, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item->flags() & Qt::ItemIsEnabled) {
            const QString target = item->data(Qt::UserRole).toString();
            goTo(target);
        }
    });
    connect(m_fileNameEdit, &QLineEdit::returnPressed, this, &AppFileDialog::acceptResult);

    if (QLineEdit *le = m_fileNameEdit)
        le->setClearButtonEnabled(false);
}

QWidget *AppFileDialog::buildHeader()
{
    QWidget *header = new QWidget(this);
    header->setObjectName(QStringLiteral("fdHeader"));

    QHBoxLayout *row = new QHBoxLayout(header);
    row->setContentsMargins(2, 2, 2, 2);
    row->setSpacing(10);

    QLabel *icon = new QLabel(header);
    icon->setObjectName(QStringLiteral("fdHeaderIcon"));
    icon->setFixedSize(32, 32);
    icon->setAlignment(Qt::AlignCenter);
    QPixmap folderIcon = style()->standardIcon(QStyle::SP_DirIcon).pixmap(18, 18);
    icon->setPixmap(folderIcon);
    icon->setAttribute(Qt::WA_StyledBackground, true);

    QWidget *txt = new QWidget(header);
    QVBoxLayout *vl = new QVBoxLayout(txt);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    m_titleLabel = new QLabel(tr("选择文件"), txt);
    m_titleLabel->setObjectName(QStringLiteral("fdTitle"));
    m_subtitleLabel = new QLabel(tr("从本地磁盘选择要打开的文件"), txt);
    m_subtitleLabel->setObjectName(QStringLiteral("fdSubtitle"));
    vl->addWidget(m_titleLabel);
    vl->addWidget(m_subtitleLabel);

    row->addWidget(icon);
    row->addWidget(txt, 1);

    // 模式选择
    m_systemBtn = new QPushButton(tr("系统式"), header);
    m_systemBtn->setObjectName(QStringLiteral("fdModeBtn"));
    m_systemBtn->setCheckable(true);
    m_systemBtn->setCursor(Qt::PointingHandCursor);
    m_systemBtn->setToolTip(tr("浏览操作系统文件系统"));
    m_instanceBtn = new QPushButton(tr("实例式"), header);
    m_instanceBtn->setObjectName(QStringLiteral("fdModeBtn"));
    m_instanceBtn->setCheckable(true);
    m_instanceBtn->setCursor(Qt::PointingHandCursor);
    m_instanceBtn->setToolTip(tr("按版本 / 实例 / 资源类型浏览游戏本地资源"));

    m_netBtn = new QPushButton(tr("网络链接"), header);
    m_netBtn->setObjectName(QStringLiteral("fdModeBtn"));
    m_netBtn->setCheckable(true);
    m_netBtn->setCursor(Qt::PointingHandCursor);
    m_netBtn->setToolTip(tr("输入网络下载链接"));
    m_netBtn->setVisible(m_mode == OpenFile || m_mode == OpenFileNames);

    m_resBtn = new QPushButton(tr("资源"), header);
    m_resBtn->setObjectName(QStringLiteral("fdModeBtn"));
    m_resBtn->setCheckable(true);
    m_resBtn->setCursor(Qt::PointingHandCursor);
    m_resBtn->setToolTip(tr("从 CurseForge / Modrinth 获取 Java 版或基岩版资源"));
    m_resBtn->setVisible(m_mode == OpenFile || m_mode == OpenFileNames);

    QButtonGroup *modeGroup = new QButtonGroup(header);
    modeGroup->setExclusive(true);
    modeGroup->addButton(m_systemBtn);
    modeGroup->addButton(m_instanceBtn);
    modeGroup->addButton(m_netBtn);
    modeGroup->addButton(m_resBtn);
    connect(m_systemBtn, &QPushButton::clicked, this, &AppFileDialog::enterSystemMode);
    connect(m_instanceBtn, &QPushButton::clicked, this, &AppFileDialog::enterInstanceMode);
    connect(m_netBtn, &QPushButton::clicked, this, &AppFileDialog::enterNetworkMode);
    connect(m_resBtn, &QPushButton::clicked, this, &AppFileDialog::enterResourceMode);

    row->addWidget(m_systemBtn);
    row->addWidget(m_instanceBtn);
    row->addWidget(m_netBtn);
    row->addWidget(m_resBtn);

    m_closeBtn = new QToolButton(header);
    m_closeBtn->setObjectName(QStringLiteral("fdCloseBtn"));
    m_closeBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    m_closeBtn->setAutoRaise(true);
    m_closeBtn->setToolTip(tr("关闭 (Esc)"));
    row->addWidget(m_closeBtn);

    return header;
}

QWidget *AppFileDialog::buildToolbar()
{
    QWidget *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("fdToolbar"));

    QHBoxLayout *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    auto navBtn = [&](QStyle::StandardPixmap sp, const QString &tip) {
        QToolButton *b = new QToolButton(bar);
        b->setObjectName(QStringLiteral("fdNavBtn"));
        b->setIcon(style()->standardIcon(sp));
        b->setAutoRaise(true);
        b->setToolTip(tip);
        row->addWidget(b);
        return b;
    };
    m_backBtn = navBtn(QStyle::SP_ArrowBack, tr("后退 (Alt+←)"));
    m_forwardBtn = navBtn(QStyle::SP_ArrowForward, tr("前进 (Alt+→)"));
    m_upBtn = navBtn(QStyle::SP_ArrowUp, tr("上一级目录 (Backspace)"));

    // 面包屑
    m_crumbBar = new QFrame(bar);
    m_crumbBar->setObjectName(QStringLiteral("fdCrumbBar"));
    m_crumbLayout = new QHBoxLayout(m_crumbBar);
    m_crumbLayout->setContentsMargins(8, 0, 8, 0);
    m_crumbLayout->setSpacing(2);
    row->addWidget(m_crumbBar, 1);

    // 地址栏输入框（默认隐藏，点地址按钮切换）
    m_addrEdit = new QLineEdit(bar);
    m_addrEdit->setObjectName(QStringLiteral("fdAddrEdit"));
    m_addrEdit->setPlaceholderText(tr("输入路径，如 /home 或 C:\\BlockBox"));
    m_addrEdit->setClearButtonEnabled(false);
    m_addrEdit->hide();
    row->addWidget(m_addrEdit, 1);

    // 地址编辑按钮
    m_addrBtn = new QToolButton(bar);
    m_addrBtn->setObjectName(QStringLiteral("fdAddrBtn"));
    m_addrBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogInfoView));
    m_addrBtn->setAutoRaise(true);
    m_addrBtn->setToolTip(tr("编辑地址 (Ctrl+L)"));
    row->addWidget(m_addrBtn);

    m_searchEdit = new QLineEdit(bar);
    m_searchEdit->setObjectName(QStringLiteral("fdSearch"));
    m_searchEdit->setPlaceholderText(tr("搜索当前文件夹"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMaximumWidth(200);
    row->addWidget(m_searchEdit);

    m_sortCombo = new QComboBox(bar);
    m_sortCombo->setObjectName(QStringLiteral("fdSortCombo"));
    m_sortCombo->addItems({ tr("名称"), tr("修改日期"), tr("类型"), tr("大小") });
    m_sortCombo->setCursor(Qt::PointingHandCursor);
    row->addWidget(m_sortCombo);

    m_listViewBtn = new QToolButton(bar);
    m_listViewBtn->setObjectName(QStringLiteral("fdViewBtn"));
    m_listViewBtn->setCheckable(true);
    m_listViewBtn->setChecked(true);
    m_listViewBtn->setAutoRaise(true);
    m_listViewBtn->setToolTip(tr("列表视图"));
    m_listViewBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    m_gridViewBtn = new QToolButton(bar);
    m_gridViewBtn->setObjectName(QStringLiteral("fdViewBtn"));
    m_gridViewBtn->setCheckable(true);
    m_gridViewBtn->setAutoRaise(true);
    m_gridViewBtn->setToolTip(tr("网格视图"));
    m_gridViewBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    row->addWidget(m_listViewBtn);
    row->addWidget(m_gridViewBtn);

    m_previewBtn = new QToolButton(bar);
    m_previewBtn->setObjectName(QStringLiteral("fdViewBtn"));
    m_previewBtn->setCheckable(true);
    m_previewBtn->setChecked(true);
    m_previewBtn->setAutoRaise(true);
    m_previewBtn->setToolTip(tr("切换预览面板"));
    m_previewBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogInfoView));
    row->addWidget(m_previewBtn);

    return bar;
}

QWidget *AppFileDialog::buildFilterBar()
{
    QWidget *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("fdChipBar"));

    m_chipLayout = new QHBoxLayout(bar);
    m_chipLayout->setContentsMargins(0, 0, 0, 0);
    m_chipLayout->setSpacing(6);

    QLabel *hint = new QLabel(tr("筛选:"), bar);
    hint->setObjectName(QStringLiteral("fdBarHint"));
    m_chipLayout->addWidget(hint);

    struct Chip { QString id; QString label; };
    const Chip chips[] = {
        { QStringLiteral("all"), tr("全部") },
        { QStringLiteral("folder"), tr("文件夹") },
        { QStringLiteral("mod"), tr("模组") },
        { QStringLiteral("image"), tr("图片") },
        { QStringLiteral("save"), tr("存档") },
        { QStringLiteral("archive"), tr("压缩包") },
        { QStringLiteral("other"), tr("其他") },
    };
    QButtonGroup *group = new QButtonGroup(bar);
    group->setExclusive(true);
    for (const Chip &c : chips) {
        QPushButton *btn = new QPushButton(c.label, bar);
        btn->setObjectName(QStringLiteral("fdChip"));
        btn->setCheckable(true);
        btn->setProperty("cat", c.id);
        btn->setCursor(Qt::PointingHandCursor);
        group->addButton(btn);
        m_chipLayout->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, c]() {
            m_activeCat = c.id;
            populate();
        });
    }
    group->buttons().first()->setChecked(true);

    m_chipLayout->addStretch();
    m_countLabel = new QLabel(bar);
    m_countLabel->setObjectName(QStringLiteral("fdBarHint"));
    m_chipLayout->addWidget(m_countLabel);

    return bar;
}

/* ============ 实例式文件列表的「分类」筛选条 ============ */

QWidget *AppFileDialog::buildInstCategoryBar()
{
    QWidget *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("fdChipBar"));

    m_instCatLayout = new QHBoxLayout(bar);
    m_instCatLayout->setContentsMargins(0, 0, 0, 0);
    m_instCatLayout->setSpacing(6);

    QLabel *hint = new QLabel(tr("分类:"), bar);
    hint->setObjectName(QStringLiteral("fdBarHint"));
    m_instCatLayout->addWidget(hint);

    m_instCatLayout->addStretch(1);

    m_instCountLabel = new QLabel(bar);
    m_instCountLabel->setObjectName(QStringLiteral("fdBarHint"));
    m_instCatLayout->addWidget(m_instCountLabel);

    return bar;
}

void AppFileDialog::rebuildInstanceChips()
{
    if (!m_instCatLayout)
        return;

    for (QPushButton *b : m_instChips) {
        m_instCatLayout->removeWidget(b);
        b->deleteLater();
    }
    m_instChips.clear();

    auto addChip = [this](const QString &label, const QString &catId) {
        QPushButton *btn = new QPushButton(label, m_instCatBar);
        btn->setObjectName(QStringLiteral("fdChip"));
        btn->setCheckable(true);
        btn->setChecked(catId == m_activeInstCategoryId);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("instcat", catId);
        connect(btn, &QPushButton::clicked, this, [this, catId]() {
            m_activeInstCategoryId = catId;
            for (QPushButton *b : m_instChips) {
                const bool on = (b->property("instcat").toString() == catId);
                if (b->isChecked() != on) {
                    b->setChecked(on);
                    b->style()->unpolish(b);
                    b->style()->polish(b);
                }
            }
            populate();
        });
        m_instCatLayout->insertWidget(m_instChips.size() + 1, btn);
        m_instChips.append(btn);
    };

    addChip(tr("全部"), QString());

    const QList<LocalResourceCategory> cats =
        m_catManager ? m_catManager->categories() : QList<LocalResourceCategory>();
    for (const LocalResourceCategory &c : cats)
        addChip(c.name, c.id);

    addChip(tr("未分类"), QStringLiteral("__uncategorized__"));

    QPushButton *manageBtn = new QPushButton(tr("管理分类"), m_instCatBar);
    manageBtn->setObjectName(QStringLiteral("fdChip"));
    manageBtn->setCursor(Qt::PointingHandCursor);
    connect(manageBtn, &QPushButton::clicked, this, [this]() {
        if (!m_catManager)
            return;
        LocalCategoryDialog dialog(m_catManager, this);
        dialog.exec();
        rebuildInstanceChips();
        populate();
    });
    m_instCatLayout->insertWidget(m_instChips.size() + 1, manageBtn);
    m_instChips.append(manageBtn);
}

QFrame *AppFileDialog::buildStepperBar()
{
    QFrame *bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("fdStepperBar"));

    m_stepperLayout = new QHBoxLayout(bar);
    m_stepperLayout->setContentsMargins(0, 0, 0, 0);
    m_stepperLayout->setSpacing(6);

    const QStringList names = { tr("版本"), tr("实例"), tr("资源类型"), tr("文件列表") };
    for (int i = 0; i < names.size(); ++i) {
        if (i > 0) {
            QLabel *arrow = new QLabel(QStringLiteral("▸"), bar);
            arrow->setObjectName(QStringLiteral("fdStepArrow"));
            m_stepperLayout->addWidget(arrow);
        }
        QPushButton *step = new QPushButton(names[i], bar);
        step->setObjectName(QStringLiteral("fdStep"));
        step->setProperty("step", i);
        step->setCursor(Qt::PointingHandCursor);
        m_stepperLayout->addWidget(step);
        connect(step, &QPushButton::clicked, this, [this, i]() {
            if (m_browseMode != BrowseMode::Instance)
                return;
            if (i < static_cast<int>(m_instLevel))
                setInstLevel(static_cast<InstLevel>(i));
        });
    }
    m_stepperLayout->addStretch();

    // 资源模式步骤条
    m_resStepper = new QFrame(this);
    m_resStepper->setObjectName(QStringLiteral("fdStepperBar"));
    m_resStepperLayout = new QHBoxLayout(m_resStepper);
    m_resStepperLayout->setContentsMargins(0, 0, 0, 0);
    m_resStepperLayout->setSpacing(6);

    const QStringList resNames = { tr("版本平台"), tr("资源类型"), tr("选择资源"), tr("选择版本") };
    for (int i = 0; i < resNames.size(); ++i) {
        if (i > 0) {
            QLabel *arrow = new QLabel(QStringLiteral("▸"), m_resStepper);
            arrow->setObjectName(QStringLiteral("fdStepArrow"));
            m_resStepperLayout->addWidget(arrow);
        }
        QPushButton *step = new QPushButton(resNames[i], m_resStepper);
        step->setObjectName(QStringLiteral("fdStep"));
        step->setProperty("step", i);
        step->setCursor(Qt::PointingHandCursor);
        m_resStepperLayout->addWidget(step);
        connect(step, &QPushButton::clicked, this, [this, i]() {
            if (m_browseMode != BrowseMode::Resource)
                return;
            if (i < static_cast<int>(m_resLevel))
                setResLevel(static_cast<ResLevel>(i));
        });
    }
    m_resStepperLayout->addStretch();
    m_resStepper->hide();

    return bar;
}

QWidget *AppFileDialog::buildBrowserPage()
{
    m_browserPage = new QWidget(this);
    m_browserPage->setObjectName(QStringLiteral("fdBrowserPage"));

    QHBoxLayout *row = new QHBoxLayout(m_browserPage);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    // 侧边栏：快速访问
    m_sidebar = new QListWidget(m_browserPage);
    m_sidebar->setObjectName(QStringLiteral("fdSidebar"));
    m_sidebar->setFixedWidth(168);
    m_sidebar->setSpacing(1);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    row->addWidget(m_sidebar);

    // 主视图（列表 / 网格）
    QWidget *viewHost = new QWidget(m_browserPage);
    QVBoxLayout *vl = new QVBoxLayout(viewHost);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    m_tree = new QTreeWidget(viewHost);
    m_tree->setObjectName(QStringLiteral("fdListTree"));
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({ tr("名称"), tr("类型"), tr("大小"), tr("修改日期"), tr("分类") });
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setAlternatingRowColors(false);
    m_tree->setIndentation(8);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setColumnWidth(1, 150);
    m_tree->setColumnWidth(2, 92);
    m_tree->setColumnWidth(3, 130);
    m_tree->setColumnWidth(4, 90);
    m_tree->setSortingEnabled(false);
    vl->addWidget(m_tree);

    m_grid = new QListWidget(viewHost);
    m_grid->setObjectName(QStringLiteral("fdGridList"));
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setMovement(QListView::Static);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setIconSize(QSize(46, 46));
    m_grid->setGridSize(QSize(118, 96));
    m_grid->setUniformItemSizes(true);
    m_grid->setWordWrap(true);
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->setSpacing(4);
    m_grid->hide();
    vl->addWidget(m_grid);

    row->addWidget(viewHost, 1);

    // 预览面板
    m_previewPanel = buildPreviewPanel();
    row->addWidget(m_previewPanel);

    return m_browserPage;
}

QFrame *AppFileDialog::buildPreviewPanel()
{
    m_previewPanel = new QFrame(this);
    m_previewPanel->setObjectName(QStringLiteral("fdPreview"));
    m_previewPanel->setFixedWidth(250);

    QVBoxLayout *vl = new QVBoxLayout(m_previewPanel);
    vl->setContentsMargins(14, 14, 14, 14);
    vl->setSpacing(8);

    m_pvIcon = new QLabel(m_previewPanel);
    m_pvIcon->setObjectName(QStringLiteral("fdPvIcon"));
    m_pvIcon->setFixedSize(120, 88);
    m_pvIcon->setAlignment(Qt::AlignCenter);
    vl->addWidget(m_pvIcon, 0, Qt::AlignHCenter);

    m_pvName = new QLabel(m_previewPanel);
    m_pvName->setObjectName(QStringLiteral("fdPvName"));
    m_pvName->setWordWrap(true);
    m_pvName->setTextInteractionFlags(Qt::TextSelectableByMouse);
    vl->addWidget(m_pvName);

    m_pvType = new QLabel(m_previewPanel);
    m_pvType->setObjectName(QStringLiteral("fdPvType"));
    m_pvType->setWordWrap(true);
    vl->addWidget(m_pvType);

    m_pvRows = new QLabel(m_previewPanel);
    m_pvRows->setObjectName(QStringLiteral("fdPvRows"));
    m_pvRows->setWordWrap(true);
    m_pvRows->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pvRows->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    vl->addWidget(m_pvRows);

    QLabel *hint = new QLabel(tr("双击文件即可打开 · Ctrl/Shift 多选"), m_previewPanel);
    hint->setObjectName(QStringLiteral("fdPvHint"));
    hint->setWordWrap(true);
    vl->addWidget(hint);

    m_pvEmpty = new QLabel(tr("选择一个文件\n查看详细信息"), m_previewPanel);
    m_pvEmpty->setObjectName(QStringLiteral("fdPvEmpty"));
    m_pvEmpty->setAlignment(Qt::AlignCenter);
    m_pvEmpty->setWordWrap(true);
    vl->addWidget(m_pvEmpty, 1);

    return m_previewPanel;
}

QWidget *AppFileDialog::buildFooter()
{
    QWidget *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("fdFooter"));

    QHBoxLayout *row = new QHBoxLayout(footer);
    row->setContentsMargins(2, 4, 2, 0);
    row->setSpacing(8);

    m_selBadge = new QLabel(tr("0 项"), footer);
    m_selBadge->setObjectName(QStringLiteral("fdSelBadge"));
    m_selBadge->hide();
    row->addWidget(m_selBadge);

    m_statusLabel = new QLabel(footer);
    m_statusLabel->setObjectName(QStringLiteral("fdStatus"));
    m_statusLabel->hide();
    row->addWidget(m_statusLabel);

    m_fileNameLabel = new QLabel(tr("文件名:"), footer);
    m_fileNameLabel->setObjectName(QStringLiteral("fdFileNameLabel"));
    row->addWidget(m_fileNameLabel);

    m_fileNameEdit = new QLineEdit(footer);
    m_fileNameEdit->setObjectName(QStringLiteral("fdFileName"));
    m_fileNameEdit->setPlaceholderText(tr("输入文件名…"));
    m_fileNameEdit->setClearButtonEnabled(false);
    row->addWidget(m_fileNameEdit, 1);

    m_cancelBtn = new QPushButton(tr("取消"), footer);
    m_cancelBtn->setObjectName(QStringLiteral("fdBtn"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    row->addWidget(m_cancelBtn);

    m_okBtn = new QPushButton(footer);
    m_okBtn->setObjectName(QStringLiteral("fdBtnPrimary"));
    m_okBtn->setCursor(Qt::PointingHandCursor);
    m_okBtn->setDefault(true);
    switch (m_mode) {
    case SaveFile:
        m_okBtn->setText(tr("保存"));
        break;
    case ExistingDirectory:
        m_okBtn->setText(tr("选择文件夹"));
        break;
    default:
        m_okBtn->setText(tr("打开"));
        break;
    }
    row->addWidget(m_okBtn);

    return footer;
}

/* ============================ 实例式页面 ============================ */

QWidget *AppFileDialog::buildInstPages()
{
    m_instStack = new QStackedWidget(this);
    m_instStack->setObjectName(QStringLiteral("fdInstStack"));
    m_instStack->addWidget(buildEditionPage());
    m_instStack->addWidget(buildInstancePage());
    m_instStack->addWidget(buildTypePage());
    return m_instStack;
}

/* ============================ 网络链接页面 ============================ */

QWidget *AppFileDialog::buildNetPage()
{
    m_netPage = new QWidget(this);
    m_netPage->setObjectName(QStringLiteral("fdNetPage"));

    QVBoxLayout *vl = new QVBoxLayout(m_netPage);
    vl->setContentsMargins(40, 30, 40, 26);
    vl->setSpacing(10);

    QLabel *icon = new QLabel(m_netPage);
    icon->setObjectName(QStringLiteral("fdNetIcon"));
    icon->setFixedSize(64, 64);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(style()->standardIcon(QStyle::SP_DriveNetIcon).pixmap(30, 30));
    vl->addWidget(icon, 0, Qt::AlignHCenter);

    QLabel *title = new QLabel(tr("输入网络链接"), m_netPage);
    title->setObjectName(QStringLiteral("fdNetTitle"));
    title->setAlignment(Qt::AlignCenter);
    vl->addWidget(title);

    QLabel *desc = new QLabel(tr("粘贴一个下载链接（http / https），确认后将返回该链接。"), m_netPage);
    desc->setObjectName(QStringLiteral("fdNetDesc"));
    desc->setAlignment(Qt::AlignCenter);
    desc->setWordWrap(true);
    vl->addWidget(desc);

    m_netEdit = new QLineEdit(m_netPage);
    m_netEdit->setObjectName(QStringLiteral("fdNetEdit"));
    m_netEdit->setPlaceholderText(tr("https://example.com/download.zip"));
    m_netEdit->setClearButtonEnabled(true);
    m_netEdit->setMinimumHeight(44);
    vl->addWidget(m_netEdit);

    m_netStatus = new QLabel(m_netPage);
    m_netStatus->setObjectName(QStringLiteral("fdNetStatus"));
    m_netStatus->setAlignment(Qt::AlignCenter);
    m_netStatus->hide();
    vl->addWidget(m_netStatus);

    QWidget *chips = new QWidget(m_netPage);
    QHBoxLayout *chl = new QHBoxLayout(chips);
    chl->setContentsMargins(0, 0, 0, 0);
    chl->setSpacing(8);
    chl->addStretch(1);
    auto addChip = [this, chl](const QString &label, const QString &example) {
        QPushButton *b = new QPushButton(label, chl->parentWidget());
        b->setObjectName(QStringLiteral("fdNetChip"));
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, example]() {
            if (m_netEdit) {
                m_netEdit->setText(example);
                updateNetStatus();
            }
        });
        chl->addWidget(b);
        return b;
    };
    addChip(tr("Modrinth 示例"),
            QStringLiteral("https://cdn.modrinth.com/data/example/versions/1.0/example.jar"));
    addChip(tr("CurseForge 示例"),
            QStringLiteral("https://mediafilez.forgecdn.net/files/1234567/example.zip"));
    chl->addStretch(1);
    vl->addWidget(chips);

    QLabel *tips = new QLabel(tr("支持 http / https 链接。若链接指向整合包或资源压缩包，确认后交由对应功能下载安装。"), m_netPage);
    tips->setObjectName(QStringLiteral("fdNetTips"));
    tips->setAlignment(Qt::AlignCenter);
    tips->setWordWrap(true);
    vl->addWidget(tips);

    vl->addStretch(1);
    return m_netPage;
}

/* ============================ 资源模式页面 ============================ */

QWidget *AppFileDialog::buildResPages()
{
    m_resStack = new QStackedWidget(this);
    m_resStack->setObjectName(QStringLiteral("fdResStack"));
    m_resStack->addWidget(buildResEditionPage());
    m_resStack->addWidget(buildResTypePage());
    m_resStack->addWidget(buildResSearchPage());
    m_resStack->addWidget(buildResVersionPage());
    return m_resStack;
}

QWidget *AppFileDialog::buildResEditionPage()
{
    m_resEditionPage = new QWidget(this);
    m_resEditionPage->setObjectName(QStringLiteral("fdResPage"));
    QVBoxLayout *vl = new QVBoxLayout(m_resEditionPage);
    vl->setContentsMargins(24, 18, 24, 18);
    vl->setSpacing(8);

    QLabel *title = new QLabel(tr("选择版本与资源平台"), m_resEditionPage);
    title->setObjectName(QStringLiteral("fdResPageTitle"));
    vl->addWidget(title);

    QLabel *desc = new QLabel(tr("从 CurseForge 或 Modrinth 获取 Java 版 / 基岩版资源"), m_resEditionPage);
    desc->setObjectName(QStringLiteral("fdResPageDesc"));
    vl->addWidget(desc);

    m_resEditionList = new QListWidget(m_resEditionPage);
    m_resEditionList->setObjectName(QStringLiteral("fdEditionList"));
    m_resEditionList->setSpacing(8);
    m_resEditionList->setSelectionMode(QAbstractItemView::NoSelection);
    m_resEditionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    vl->addWidget(m_resEditionList, 1);

    auto addEditionCard = [this](const QString &letter, const QString &name,
                                 const QString &sub, const QColor &color,
                                 const QString &edition, const QString &platform) {
        QListWidgetItem *item = new QListWidgetItem(m_resEditionList);
        item->setSizeHint(QSize(0, 78));
        QFrame *card = new QFrame;
        card->setObjectName(QStringLiteral("fdResCard"));
        QHBoxLayout *hl = new QHBoxLayout(card);
        hl->setContentsMargins(14, 10, 14, 10);
        hl->setSpacing(14);
        QLabel *ico = new QLabel(card);
        ico->setObjectName(QStringLiteral("fdResCardIco"));
        ico->setFixedSize(46, 46);
        ico->setAlignment(Qt::AlignCenter);
        ico->setPixmap(makeTileIcon(letter, color, 46));
        hl->addWidget(ico);
        QWidget *txt = new QWidget(card);
        QVBoxLayout *tl = new QVBoxLayout(txt);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(2);
        QLabel *nm = new QLabel(name, txt);
        nm->setObjectName(QStringLiteral("fdResCardName"));
        QLabel *ds = new QLabel(sub, txt);
        ds->setObjectName(QStringLiteral("fdResCardDesc"));
        tl->addWidget(nm);
        tl->addWidget(ds);
        hl->addWidget(txt, 1);
        QLabel *arrow = new QLabel(QStringLiteral("→"), card);
        arrow->setObjectName(QStringLiteral("fdInstArrow"));
        hl->addWidget(arrow);
        new ClickableFilter(card, [this, edition, platform]() {
            resChooseEditionPlatform(edition, platform);
        });
        m_resEditionList->setItemWidget(item, card);
    };
    addEditionCard(QStringLiteral("J"), tr("Java 版 · CurseForge"), tr("Minecraft Java 版模组与整合包社区"),
                   QColor("#10B981"), QStringLiteral("java"), QStringLiteral("curseforge"));
    addEditionCard(QStringLiteral("J"), tr("Java 版 · Modrinth"), tr("开源、自由、现代的模组平台"),
                   QColor("#6366F1"), QStringLiteral("java"), QStringLiteral("modrinth"));
    addEditionCard(QStringLiteral("B"), tr("基岩版 · CurseForge"), tr("基岩版附加包 / 资源包 / 地图 / 皮肤等"),
                   QColor("#06B6D4"), QStringLiteral("bedrock"), QStringLiteral("curseforge"));
    addEditionCard(QStringLiteral("B"), tr("基岩版 · Modrinth"), tr("基岩版资源（可用内容相对有限）"),
                   QColor("#0E7490"), QStringLiteral("bedrock"), QStringLiteral("modrinth"));

    return m_resEditionPage;
}

QWidget *AppFileDialog::buildResTypePage()
{
    m_resTypePage = new QWidget(this);
    m_resTypePage->setObjectName(QStringLiteral("fdResPage"));
    QVBoxLayout *vl = new QVBoxLayout(m_resTypePage);
    vl->setContentsMargins(24, 18, 24, 18);
    vl->setSpacing(8);

    QLabel *title = new QLabel(tr("选择资源类型"), m_resTypePage);
    title->setObjectName(QStringLiteral("fdResPageTitle"));
    vl->addWidget(title);

    QLabel *desc = new QLabel(tr("选择要获取的资源类型"), m_resTypePage);
    desc->setObjectName(QStringLiteral("fdResPageDesc"));
    vl->addWidget(desc);

    m_resTypeList = new QListWidget(m_resTypePage);
    m_resTypeList->setObjectName(QStringLiteral("fdResTypeList"));
    m_resTypeList->setSpacing(8);
    m_resTypeList->setSelectionMode(QAbstractItemView::NoSelection);
    m_resTypeList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    vl->addWidget(m_resTypeList, 1);

    return m_resTypePage;
}

QWidget *AppFileDialog::buildResSearchPage()
{
    m_resSearchPage = new QWidget(this);
    m_resSearchPage->setObjectName(QStringLiteral("fdResPage"));
    QVBoxLayout *vl = new QVBoxLayout(m_resSearchPage);
    vl->setContentsMargins(16, 12, 16, 12);
    vl->setSpacing(8);

    QWidget *searchRow = new QWidget(m_resSearchPage);
    QHBoxLayout *sl = new QHBoxLayout(searchRow);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(8);
    m_resSearchEdit = new QLineEdit(searchRow);
    m_resSearchEdit->setObjectName(QStringLiteral("fdNetEdit"));
    m_resSearchEdit->setPlaceholderText(tr("搜索资源名称 / 作者…"));
    m_resSearchEdit->setClearButtonEnabled(true);
    sl->addWidget(m_resSearchEdit, 1);
    m_resSearchBtn = new QPushButton(tr("搜索"), searchRow);
    m_resSearchBtn->setObjectName(QStringLiteral("fdBtnPrimary"));
    m_resSearchBtn->setCursor(Qt::PointingHandCursor);
    m_resSearchBtn->setFixedWidth(90);
    sl->addWidget(m_resSearchBtn);
    vl->addWidget(searchRow);

    m_resStatusLabel = new QLabel(m_resSearchPage);
    m_resStatusLabel->setObjectName(QStringLiteral("fdResStatus"));
    m_resStatusLabel->setAlignment(Qt::AlignCenter);
    m_resStatusLabel->setWordWrap(true);
    m_resStatusLabel->hide();
    vl->addWidget(m_resStatusLabel);

    m_resResultList = new QListWidget(m_resSearchPage);
    m_resResultList->setObjectName(QStringLiteral("fdResResultList"));
    m_resResultList->setSpacing(2);
    m_resResultList->setSelectionMode(QAbstractItemView::NoSelection);
    m_resResultList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    vl->addWidget(m_resResultList, 1);

    QWidget *pageRow = new QWidget(m_resSearchPage);
    QHBoxLayout *pl = new QHBoxLayout(pageRow);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(8);
    pl->addStretch(1);
    m_resPrevBtn = new QPushButton(tr("上一页"), pageRow);
    m_resPrevBtn->setObjectName(QStringLiteral("fdBtn"));
    m_resPrevBtn->setCursor(Qt::PointingHandCursor);
    m_resPrevBtn->setEnabled(false);
    m_resPageLabel = new QLabel(tr("第 1 页"), pageRow);
    m_resPageLabel->setObjectName(QStringLiteral("fdBarHint"));
    m_resNextBtn = new QPushButton(tr("下一页"), pageRow);
    m_resNextBtn->setObjectName(QStringLiteral("fdBtn"));
    m_resNextBtn->setCursor(Qt::PointingHandCursor);
    m_resNextBtn->setEnabled(false);
    pl->addWidget(m_resPrevBtn);
    pl->addWidget(m_resPageLabel);
    pl->addWidget(m_resNextBtn);
    pl->addStretch(1);
    vl->addWidget(pageRow);

    return m_resSearchPage;
}

QWidget *AppFileDialog::buildResVersionPage()
{
    m_resVersionPage = new QWidget(this);
    m_resVersionPage->setObjectName(QStringLiteral("fdResPage"));
    QVBoxLayout *vl = new QVBoxLayout(m_resVersionPage);
    vl->setContentsMargins(16, 12, 16, 12);
    vl->setSpacing(8);

    m_resVersionHeader = new QLabel(m_resVersionPage);
    m_resVersionHeader->setObjectName(QStringLiteral("fdResVersionHeader"));
    m_resVersionHeader->setWordWrap(true);
    vl->addWidget(m_resVersionHeader);

    m_resVersionList = new QListWidget(m_resVersionPage);
    m_resVersionList->setObjectName(QStringLiteral("fdResVersionList"));
    m_resVersionList->setSpacing(2);
    m_resVersionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resVersionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    vl->addWidget(m_resVersionList, 1);

    connect(m_resVersionList, &QListWidget::itemSelectionChanged, this, [this]() {
        const int idx = m_resVersionList->currentRow();
        for (int i = 0; i < m_resVersionList->count(); ++i) {
            QListWidgetItem *it = m_resVersionList->item(i);
            if (QWidget *w = m_resVersionList->itemWidget(it)) {
                w->setProperty("selected", i == idx);
                w->style()->unpolish(w);
                w->style()->polish(w);
            }
        }
        if (idx >= 0 && idx < m_resInfo.versionFiles.size()) {
            m_resVersion = m_resInfo.versionFiles[idx];
            updateFooterState();
        }
    });

    return m_resVersionPage;
}

namespace {
QFrame *makeCard(const QString &objectName)
{
    QFrame *f = new QFrame;
    f->setObjectName(objectName);
    return f;
}
QFrame *makePageShell(const QString &stepText, const QString &title, const QString &desc,
                      QWidget *content, QWidget *parent)
{
    QFrame *page = new QFrame(parent);
    page->setObjectName(QStringLiteral("fdInstPage"));
    QVBoxLayout *vl = new QVBoxLayout(page);
    vl->setContentsMargins(20, 18, 20, 18);
    vl->setSpacing(12);

    QWidget *head = new QWidget(page);
    head->setObjectName(QStringLiteral("fdInstPageHead"));
    QHBoxLayout *hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(12);

    QLabel *badge = new QLabel(stepText, head);
    badge->setObjectName(QStringLiteral("fdStepBadge"));
    hl->addWidget(badge);

    QWidget *txt = new QWidget(head);
    QVBoxLayout *tl = new QVBoxLayout(txt);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(1);
    QLabel *t = new QLabel(title, txt);
    t->setObjectName(QStringLiteral("fdInstTitle"));
    QLabel *d = new QLabel(desc, txt);
    d->setObjectName(QStringLiteral("fdInstDesc"));
    tl->addWidget(t);
    tl->addWidget(d);
    hl->addWidget(txt, 1);

    QPushButton *back = new QPushButton(QStringLiteral("← 上一步"), head);
    back->setObjectName(QStringLiteral("fdBackBtn"));
    back->setCursor(Qt::PointingHandCursor);
    hl->addWidget(back);

    vl->addWidget(head);
    vl->addWidget(content, 1);
    return page;
}
} // namespace

QWidget *AppFileDialog::buildEditionPage()
{
    QWidget *content = new QWidget(this);
    QHBoxLayout *hl = new QHBoxLayout(content);
    hl->setContentsMargins(0, 8, 0, 8);
    hl->setSpacing(16);

    auto makeEditionCard = [this, hl](const QString &letter, const QString &name,
                                      const QString &sub, const QString &next, bool isJava) {
        QFrame *card = makeCard(QStringLiteral("fdEditionCard"));
        card->setProperty("java", isJava);
        QVBoxLayout *vl = new QVBoxLayout(card);
        vl->setContentsMargins(20, 24, 20, 20);
        vl->setSpacing(8);
        vl->setAlignment(Qt::AlignHCenter);

        QLabel *ico = new QLabel(letter, card);
        ico->setObjectName(QStringLiteral("fdEditionIco"));
        ico->setAlignment(Qt::AlignCenter);
        ico->setFixedSize(64, 64);
        ico->setAttribute(Qt::WA_StyledBackground, true);
        vl->addWidget(ico, 0, Qt::AlignHCenter);

        QLabel *nm = new QLabel(name, card);
        nm->setObjectName(QStringLiteral("fdEditionName"));
        nm->setAlignment(Qt::AlignCenter);
        vl->addWidget(nm);

        QLabel *ds = new QLabel(sub, card);
        ds->setObjectName(QStringLiteral("fdEditionDesc"));
        ds->setAlignment(Qt::AlignCenter);
        ds->setWordWrap(true);
        vl->addWidget(ds);

        QLabel *nx = new QLabel(next, card);
        nx->setObjectName(QStringLiteral("fdEditionNext"));
        nx->setAlignment(Qt::AlignCenter);
        vl->addWidget(nx, 0, Qt::AlignHCenter);

        new ClickableFilter(card, [this, isJava]() { chooseEdition(isJava); });
        hl->addWidget(card, 1);
    };

    makeEditionCard(QStringLiteral("J"), tr("Java 版"),
                    tr("Fabric · Forge · NeoForge\n先选择实例，再浏览实例内资源"),
                    tr("选择实例 →"), true);
    makeEditionCard(QStringLiteral("B"), tr("基岩版"),
                    tr("Bedrock 多实例数据隔离\n先选择实例，再浏览实例内资源"),
                    tr("选择实例 →"), false);

    m_editionPage = makePageShell(QStringLiteral("1 / 4"), tr("选择游戏版本"),
                                  tr("先确定版本体系，再浏览其中的实例与本地资源"),
                                  content, this);
    return m_editionPage;
}

QWidget *AppFileDialog::buildInstancePage()
{
    m_instanceList = new QListWidget(this);
    m_instanceList->setObjectName(QStringLiteral("fdInstanceList"));
    m_instanceList->setSpacing(6);
    m_instanceList->setSelectionMode(QAbstractItemView::NoSelection);
    m_instanceList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

m_instancePage = makePageShell(QStringLiteral("2 / 4"), tr("选择实例"),
                               tr("选择要浏览资源的游戏实例"), m_instanceList, this);
    return m_instancePage;
}

QWidget *AppFileDialog::buildTypePage()
{
    m_typeList = new QListWidget(this);
    m_typeList->setObjectName(QStringLiteral("fdTypeList"));
    m_typeList->setViewMode(QListView::IconMode);
    m_typeList->setMovement(QListView::Static);
    m_typeList->setResizeMode(QListView::Adjust);
    m_typeList->setIconSize(QSize(44, 44));
    m_typeList->setGridSize(QSize(200, 92));
    m_typeList->setUniformItemSizes(true);
    m_typeList->setWordWrap(true);
    m_typeList->setSelectionMode(QAbstractItemView::NoSelection);
    m_typeList->setSpacing(6);

    m_typePage = makePageShell(QStringLiteral("3 / 4"), tr("选择资源类型"),
                               QString(), m_typeList, this);

    connect(m_typeList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int idx = item ? item->data(Qt::UserRole).toInt() : -1;
        if (idx >= 0 && idx < m_resTypes.size())
            openResType(m_resTypes.at(idx));
    });

    return m_typePage;
}

/* ============================ 系统式导航 ============================ */

void AppFileDialog::loadQuickAccess()
{
    m_sidebar->clear();

    auto addSection = [this](const QString &label) {
        QListWidgetItem *it = new QListWidgetItem(label, m_sidebar);
        it->setFlags(Qt::NoItemFlags);
        it->setData(Qt::UserRole, QString());
        QFont f = it->font();
        f.setBold(true);
        f.setPixelSize(11);
        it->setFont(f);
        it->setSizeHint(QSize(0, 22));
    };
    auto addItem = [this](const QString &text, const QString &target, QStyle::StandardPixmap sp) {
        QListWidgetItem *it = new QListWidgetItem(
            style()->standardIcon(sp), text, m_sidebar);
        it->setData(Qt::UserRole, target);
        it->setSizeHint(QSize(0, 34));
    };

    addSection(tr("快速访问"));
    addItem(tr("主页"), QDir::homePath(), QStyle::SP_DirHomeIcon);
    addItem(tr("桌面"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation), QStyle::SP_DesktopIcon);
    addItem(tr("文档"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), QStyle::SP_FileDialogDetailedView);
    addItem(tr("下载"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), QStyle::SP_ArrowDown);
    addItem(tr("图片"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation), QStyle::SP_FileDialogContentsView);

    addSection(tr("位置"));
    addItem(tr("此电脑"), QString(), QStyle::SP_ComputerIcon);
    addItem(tr("实例目录"), SettingsManager::instance()->getDefaultInstancePath(), QStyle::SP_DirIcon);
}

void AppFileDialog::goToComputerRoot()
{
    m_currentDir.clear();
    m_selectedPaths.clear();
    rebuildBreadcrumbs();
    populate();
    pushHistory(QString());
    updateNavButtons();
}

bool AppFileDialog::goTo(const QString &path)
{
    if (path.isEmpty()) {
        goToComputerRoot();
        return true;
    }
    QDir dir(path);
    if (!dir.exists()) {
        dir = QDir(QDir::homePath());
        if (!dir.exists())
            return false;
    }
    const QString abs = dir.absolutePath();
    m_currentDir = abs;
    m_selectedPaths.clear();
    rebuildBreadcrumbs();
    populate();
    pushHistory(abs);
    updateNavButtons();
    return true;
}

void AppFileDialog::goUpDir()
{
    if (m_currentDir.isEmpty()) {
        return;
    }
    QDir parent(QFileInfo(m_currentDir).dir());
    if (parent.exists()) {
        // 驱动器根目录的上一级 → “此电脑”
        if (QFileInfo(m_currentDir).filePath() == parent.absolutePath()
            || parent.isRoot()) {
            goToComputerRoot();
        } else {
            goTo(parent.absolutePath());
        }
    }
}

void AppFileDialog::pushHistory(const QString &loc)
{
    m_history.resize(m_historyIdx + 1);
    m_history << loc;
    m_historyIdx = m_history.size() - 1;
}

void AppFileDialog::goBack()
{
    if (m_historyIdx <= 0)
        return;
    --m_historyIdx;
    const QString loc = m_history.at(m_historyIdx);
    if (loc.isEmpty())
        m_currentDir.clear();
    else
        m_currentDir = loc;
    m_selectedPaths.clear();
    rebuildBreadcrumbs();
    populate();
    updateNavButtons();
}

void AppFileDialog::goForward()
{
    if (m_historyIdx >= m_history.size() - 1)
        return;
    ++m_historyIdx;
    const QString loc = m_history.at(m_historyIdx);
    if (loc.isEmpty())
        m_currentDir.clear();
    else
        m_currentDir = loc;
    m_selectedPaths.clear();
    rebuildBreadcrumbs();
    populate();
    updateNavButtons();
}

void AppFileDialog::updateNavButtons()
{
    m_backBtn->setEnabled(m_historyIdx > 0);
    m_forwardBtn->setEnabled(m_historyIdx < m_history.size() - 1);
    m_upBtn->setEnabled(!m_currentDir.isEmpty());
}

void AppFileDialog::enterAddrEditing()
{
    if (m_browseMode != BrowseMode::System)
        return;
    m_addrEditing = true;
    m_addrEdit->setText(m_currentDir.isEmpty()
                            ? QStringLiteral("此电脑")
                            : QDir::toNativeSeparators(m_currentDir));
    m_addrEdit->show();
    m_crumbBar->hide();
    m_addrEdit->setFocus();
    m_addrEdit->selectAll();
}

void AppFileDialog::exitAddrEditing()
{
    if (!m_addrEditing)
        return;
    m_addrEditing = false;
    m_addrEdit->hide();
    m_crumbBar->show();
    m_addrEdit->clearFocus();
}

void AppFileDialog::addrNavigate()
{
    if (m_browseMode != BrowseMode::System)
        return;
    QString text = m_addrEdit->text().trimmed();
    if (text.isEmpty() || text == QStringLiteral("此电脑")) {
        exitAddrEditing();
        if (text.isEmpty())
            goToComputerRoot();
        return;
    }
    QString path = QDir::fromNativeSeparators(text);
    // 相对路径基于当前目录
    if (!QDir::isAbsolutePath(path))
        path = QDir(m_currentDir).filePath(path);
    const QString abs = QDir::cleanPath(path);
    if (QDir(abs).exists()) {
        exitAddrEditing();
        goTo(abs);
    } else {
        // 无法定位，保留编辑态并给出提示
        if (m_statusLabel) {
            m_statusLabel->setText(tr("路径不存在：%1").arg(text));
            m_statusLabel->show();
        }
    }
}

void AppFileDialog::showContextMenu(const QPoint &globalPos, const QString &path)
{
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("fdCtxMenu"));

    const bool hasTarget = !path.isEmpty();
    const bool isDir = hasTarget && QFileInfo(path).isDir();
    const QStringList sel = selectedPaths();

    if (hasTarget) {
        // 若右键项不在当前选中集，单独选中它
        if (!sel.contains(path)) {
            m_selectedPaths.clear();
            m_selectedPaths.insert(path);
            applySelectionToVisibleWidget();
        }

        const bool multi = m_selectedPaths.size() > 1;

        QAction *openAct = menu.addAction(isDir ? tr("打开文件夹") : tr("打开"));
        connect(openAct, &QAction::triggered, this, [this, path, isDir]() {
            if (isDir) {
                goTo(path);
            } else {
                m_selectedPaths.clear();
                m_selectedPaths.insert(path);
                m_fileNameEdit->setText(QFileInfo(path).fileName());
                acceptResult();
            }
        });

        if (!isDir && m_mode != ExistingDirectory && m_mode != SaveFile) {
            QAction *openWithAct = menu.addAction(tr("打开文件所在目录"));
            connect(openWithAct, &QAction::triggered, this, [this, path]() {
                goTo(QFileInfo(path).absolutePath());
            });
        }

        menu.addSeparator();

        QAction *copyAct = menu.addAction(multi ? tr("复制路径 (%1 项)").arg(m_selectedPaths.size()) : tr("复制路径"));
        connect(copyAct, &QAction::triggered, this, [this]() {
            const QStringList paths = selectedPaths();
            QString text;
            for (const QString &p : paths) {
                if (!text.isEmpty())
                    text += QLatin1Char('\n');
                text += QDir::toNativeSeparators(p);
            }
            QGuiApplication::clipboard()->setText(text);
        });

        QAction *renameAct = menu.addAction(tr("重命名"));
        renameAct->setEnabled(m_selectedPaths.size() == 1 && !isDir && hasTarget);
        connect(renameAct, &QAction::triggered, this, [this, path]() {
            const QString oldName = QFileInfo(path).fileName();
            bool ok = false;
            const QString newName = QInputDialog::getText(this, tr("重命名"), tr("新名称："),
                                                          QLineEdit::Normal, oldName, &ok);
            if (!ok || newName.trimmed().isEmpty() || newName == oldName)
                return;
            const QString newPath = QFileInfo(path).absolutePath()
                + QLatin1Char('/') + newName.trimmed();
            QFile f(path);
            if (f.rename(newPath)) {
                populate();
            } else {
                QMessageBox::warning(this, tr("重命名"), tr("重命名失败，请检查名称是否合法。"));
            }
        });

        // 实例式文件列表：手动设置资源分类
        if (m_browseMode == BrowseMode::Instance && m_instLevel == InstLevel::Files
            && m_catManager && hasTarget) {
            menu.addSeparator();
            QMenu *catSub = menu.addMenu(tr("设置分类"));
            addFileCategoryMenu(catSub, QFileInfo(path).fileName());
            menu.addSeparator();
        }

        menu.addSeparator();

        QAction *propAct = menu.addAction(tr("属性"));
        connect(propAct, &QAction::triggered, this, [this, path]() {
            m_selectedPaths.clear();
            m_selectedPaths.insert(path);
            applySelectionToVisibleWidget();
            onSelectionChanged();
        });

        QAction *delAct = menu.addAction(tr("删除"));
        delAct->setEnabled(hasTarget);
        connect(delAct, &QAction::triggered, this, [this, path, isDir, multi]() {
            const QStringList paths = selectedPaths();
            const QString first = paths.isEmpty() ? path : paths.first();
            const QMessageBox::StandardButton ret =
                QMessageBox::question(this, tr("删除"),
                                      multi ? tr("确定删除选中的 %1 项？").arg(paths.size())
                                            : tr("确定删除“%1”？").arg(QFileInfo(first).fileName()));
            if (ret != QMessageBox::Yes)
                return;
            for (const QString &p : paths) {
                if (isDir)
                    QDir(p).removeRecursively();
                else
                    QFile::remove(p);
            }
            m_selectedPaths.clear();
            populate();
        });
    } else {
        QAction *refreshAct = menu.addAction(tr("刷新"));
        connect(refreshAct, &QAction::triggered, this, [this]() { populate(); });

        QAction *selectAllAct = menu.addAction(tr("全选"));
        connect(selectAllAct, &QAction::triggered, this, [this]() {
            if (m_tree->isVisible())
                m_tree->selectAll();
            else
                m_grid->selectAll();
            onSelectionChanged();
        });

        menu.addSeparator();
        QAction *openRootAct = menu.addAction(tr("打开“此电脑”"));
        connect(openRootAct, &QAction::triggered, this, [this]() { goToComputerRoot(); });
    }

    menu.exec(globalPos);
}


void AppFileDialog::rebuildBreadcrumbs()
{
    // 清空原有按钮
    while (QLayoutItem *item = m_crumbLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    auto addCrumb = [this](const QString &label, const QString &target, bool current) {
        QPushButton *b = new QPushButton(label, m_crumbBar);
        b->setObjectName(current ? QStringLiteral("fdCrumb") : QStringLiteral("fdCrumbLink"));
        b->setCursor(Qt::PointingHandCursor);
        b->setFlat(true);
        b->setProperty("current", current);
        if (!current) {
            connect(b, &QPushButton::clicked, this, [this, target]() { goTo(target); });
        }
        m_crumbLayout->addWidget(b);
    };
    auto addSep = [this]() {
        QLabel *sep = new QLabel(QStringLiteral("▸"), m_crumbBar);
        sep->setObjectName(QStringLiteral("fdCrumbSep"));
        m_crumbLayout->addWidget(sep);
    };

    if (m_currentDir.isEmpty()) {
        addCrumb(tr("此电脑"), QString(), true);
        return;
    }

    addCrumb(tr("此电脑"), QString(), false);
    addSep();

    const QString clean = QDir::cleanPath(m_currentDir);
    const QStringList parts = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString prefix;
    for (int i = 0; i < parts.size(); ++i) {
        if (i == 0 && parts.at(i).endsWith(QLatin1Char(':'))) {
            prefix = parts.at(i) + QLatin1Char('/');
        } else {
            prefix = QDir(prefix).filePath(parts.at(i));
        }
        const bool isDrive = (i == 0 && parts.at(i).endsWith(QLatin1Char(':')));
        const QString label = isDrive ? driveDisplayName(parts.at(i)) : parts.at(i);
        addCrumb(label, prefix, i == parts.size() - 1);
        if (i < parts.size() - 1)
            addSep();
    }
}

bool AppFileDialog::entryVisible(const Entry &e) const
{
    // 搜索
    if (!m_searchText.isEmpty()
        && !e.name.contains(m_searchText, Qt::CaseInsensitive)) {
        return false;
    }
    // 实例式：资源目录作用域过滤
    if (m_browseMode == BrowseMode::Instance) {
        if (m_scopeDirsOnly && !e.isDir)
            return false;
        if (!e.isDir && !m_scopeFilters.isEmpty() && !matchesFilterList(e.name, m_scopeFilters))
            return false;
        // 手动分类筛选（m_instLevel == Files 时生效）
        if (m_instLevel == InstLevel::Files && !m_activeInstCategoryId.isEmpty()) {
            const QString catId = m_catManager ? m_catManager->categoryOf(e.name) : QString();
            if (m_activeInstCategoryId == QStringLiteral("__uncategorized__"))
                return catId.isEmpty();
            if (catId != m_activeInstCategoryId)
                return false;
        }
        return true;
    }
    // 类型筛选
    if (m_activeCat != QStringLiteral("all") && e.cat != m_activeCat) {
        return false;
    }
    // 目录模式
    if (showOnlyDirs() && !e.isDir && !e.isDrive) {
        return false;
    }
    // 文件过滤器
    if (!e.isDir && !e.isDrive) {
        const QStringList pats = activePatterns();
        if (!pats.isEmpty() && !matchesFilterList(e.name, pats))
            return false;
    }
    return true;
}

void AppFileDialog::populate()
{
    QList<Entry> entries;

    if (m_currentDir.isEmpty()) {
        for (const QFileInfo &d : QDir::drives()) {
            Entry e;
            e.name = driveDisplayName(d.absoluteFilePath());
            e.absPath = d.absoluteFilePath();
            e.isDir = true;
            e.isDrive = true;
            e.cat = QStringLiteral("folder");
            e.mod = d.lastModified();
            if (entryVisible(e))
                entries << e;
        }
    } else {
        QDir dir(m_currentDir);
        const QFileInfoList infos = dir.entryInfoList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
            QDir::Name | QDir::DirsFirst);
        for (const QFileInfo &info : infos) {
            Entry e;
            e.name = info.fileName();
            e.absPath = info.absoluteFilePath();
            e.isDir = info.isDir();
            e.ext = info.suffix();
            e.size = info.isDir() ? -1 : info.size();
            e.mod = info.lastModified();
            e.cat = fileCategory(e.ext, e.isDir);
            if (entryVisible(e))
                entries << e;
        }
    }

    sortEntries(entries);

    const bool instFiles = m_browseMode == BrowseMode::Instance
                           && m_instLevel == InstLevel::Files;

    // 填充树
    m_tree->clear();
    m_tree->setUpdatesEnabled(false);
    QFileIconProvider icp;
    for (const Entry &e : entries) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, e.name);
        item->setText(1, fileTypeText(e));
        item->setText(2, e.isDir ? QStringLiteral("—") : (e.size >= 0 ? QLocale::system().formattedDataSize(e.size) : QString()));
        item->setText(3, e.mod.isValid() ? e.mod.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString());
        item->setText(4, instFiles && m_catManager ? m_catManager->categoryNameOf(e.name) : QString());
        item->setIcon(0, e.isDir
                          ? style()->standardIcon(QStyle::SP_DirIcon)
                          : icp.icon(QFileInfo(e.absPath)));
        item->setData(0, Qt::UserRole, e.absPath);
        item->setData(0, Qt::UserRole + 1, e.isDir);
        item->setData(0, Qt::UserRole + 2, e.ext);
        item->setData(0, Qt::UserRole + 3, e.size);
        item->setData(0, Qt::UserRole + 5, e.isDrive);
        item->setToolTip(0, e.absPath);
        if (m_selectedPaths.contains(e.absPath))
            item->setSelected(true);
    }
    m_tree->setUpdatesEnabled(true);

    // 填充网格
    m_grid->clear();
    for (const Entry &e : entries) {
        auto *item = new QListWidgetItem(m_grid);
        item->setText(e.name);
        item->setIcon(makeTileIcon(categoryIconLetter(e), categoryColor(e.cat), 46));
        item->setToolTip(e.absPath);
        if (instFiles && m_catManager) {
            const QString cn = m_catManager->categoryNameOf(e.name);
            if (!cn.isEmpty())
                item->setToolTip(e.absPath + QStringLiteral("\n分类：") + cn);
        }
        item->setData(Qt::UserRole, e.absPath);
        item->setData(Qt::UserRole + 1, e.isDir);
        item->setData(Qt::UserRole + 2, e.ext);
        item->setData(Qt::UserRole + 3, e.size);
        item->setData(Qt::UserRole + 5, e.isDrive);
        if (m_selectedPaths.contains(e.absPath))
            item->setSelected(true);
    }

    m_countLabel->setText(tr("%1 项").arg(entries.size()));
    if (m_instCountLabel)
        m_instCountLabel->setText(tr("%1 项").arg(entries.size()));
    updatePreview();
    updateFooterState();
}

void AppFileDialog::sortEntries(QList<Entry> &entries)
{
    std::stable_sort(entries.begin(), entries.end(), [this](const Entry &a, const Entry &b) {
        if (a.isDrive != b.isDrive)
            return a.isDrive;
        if (a.isDir != b.isDir)
            return a.isDir;
        int r = 0;
        if (m_sortKey == QStringLiteral("name"))
            r = a.name.compare(b.name, Qt::CaseInsensitive);
        else if (m_sortKey == QStringLiteral("date"))
            r = a.mod.toString().compare(b.mod.toString());
        else if (m_sortKey == QStringLiteral("type"))
            r = QString::compare(fileTypeText(a), fileTypeText(b));
        else if (m_sortKey == QStringLiteral("size"))
            r = (a.size < b.size) ? -1 : (a.size > b.size ? 1 : 0);
        return m_sortAsc ? (r < 0) : (r > 0);
    });
}

bool AppFileDialog::showOnlyDirs() const
{
    return m_mode == ExistingDirectory || (m_options & ShowDirsOnly);
}

QStringList AppFileDialog::activePatterns() const
{
    if (m_filterPatterns.isEmpty())
        return QStringList();
    // 若只有“所有文件”，不做过滤
    if (m_filterPatterns.size() == 1 && m_filterNames.size() == 1
        && m_filterNames.first().contains(QStringLiteral("所有文件"))) {
        return QStringList();
    }
    return m_filterPatterns.first();
}

void AppFileDialog::parseFilter(const QString &filter)
{
    m_filterNames.clear();
    m_filterPatterns.clear();
    if (filter.trimmed().isEmpty()) {
        m_filterNames << tr("所有文件 (*)");
        m_filterPatterns << QStringList();
        return;
    }
    const QStringList parts = filter.split(QStringLiteral(";;"));
    for (const QString &part : parts) {
        const int openIdx = part.lastIndexOf(QLatin1Char('('));
        const int closeIdx = part.lastIndexOf(QLatin1Char(')'));
        if (openIdx < 0 || closeIdx <= openIdx) {
            m_filterNames << part.trimmed();
            m_filterPatterns << QStringList();
            continue;
        }
        const QString name = part.left(openIdx).trimmed();
        const QString patternsStr = part.mid(openIdx + 1, closeIdx - openIdx - 1);
        QStringList patterns;
        for (const QString &tok : patternsStr.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            QString pat = tok.trimmed();
            if (pat.endsWith(QLatin1Char(';')))
                pat.chop(1);
            if (pat.isEmpty())
                continue;
            patterns << pat;
        }
        m_filterNames << (name.isEmpty() ? part.trimmed() : name);
        m_filterPatterns << patterns;
    }
    if (m_filterNames.isEmpty()) {
        m_filterNames << tr("所有文件 (*)");
        m_filterPatterns << QStringList();
    }
}

void AppFileDialog::applyFilter(const QString &filter)
{
    parseFilter(filter);
}

QString AppFileDialog::currentFilterName() const
{
    for (int i = 0; i < m_filterNames.size(); ++i) {
        if (!m_filterNames.at(i).contains(QStringLiteral("所有文件"))
            && !m_filterNames.at(i).contains(QStringLiteral("All Files"))) {
            return m_filterNames.at(i);
        }
    }
    return m_filterNames.value(0);
}

bool AppFileDialog::matchesFilterList(const QString &name, const QStringList &filters)
{
    for (const QString &p : filters) {
        if (QDir::match(p, name))
            return true;
    }
    return false;
}

/* ============================ 实例式 ============================ */

void AppFileDialog::loadInstances()
{
    m_instances.clear();

    QStringList roots;
    const QList<InstanceFolderInfo> folders =
        SettingsManager::instance()->getInstanceFolders();
    if (folders.isEmpty()) {
        roots << QCoreApplication::applicationDirPath() + QStringLiteral("/.minecraft");
    } else {
        for (int i = 0; i < folders.size(); ++i)
            roots << folders.at(i).path;
    }

    QSet<QString> seen;
    for (const QString &root : roots) {
        QDir versionsDir(root + QStringLiteral("/versions"));
        if (!versionsDir.exists())
            continue;
        const QFileInfoList dirs = versionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &dirInfo : dirs) {
            const QString dirPath = dirInfo.absoluteFilePath();
            if (seen.contains(dirPath))
                continue;
            seen.insert(dirPath);

            const QString name = dirInfo.fileName();
            QString jsonPath = dirPath + QLatin1Char('/') + name + QStringLiteral(".json");
            if (!QFile::exists(jsonPath))
                jsonPath = dirPath + QStringLiteral("/version.json");
            QJsonObject obj;
            {
                QFile f(jsonPath);
                if (f.open(QIODevice::ReadOnly))
                    obj = QJsonDocument::fromJson(f.readAll()).object();
            }
            if (obj.isEmpty())
                continue;

            QString loader;
            const QStringList keys = obj.keys();
            for (const QString &k : keys) {
                const QString kl = k.toLower();
                if (kl.contains(QStringLiteral("fabric"))) { loader = QStringLiteral("Fabric"); break; }
                if (kl.contains(QStringLiteral("quilt")))  { loader = QStringLiteral("Quilt");  break; }
                if (kl.contains(QStringLiteral("neoforge"))){ loader = QStringLiteral("NeoForge"); break; }
                if (kl.contains(QStringLiteral("forge")))  { loader = QStringLiteral("Forge");  break; }
            }
            QString version = obj.value(QStringLiteral("inheritsFrom")).toString();
            if (version.isEmpty())
                version = obj.value(QStringLiteral("clientVersion")).toString();
            if (version.isEmpty())
                version = obj.value(QStringLiteral("id")).toString();
            if (version.isEmpty())
                version = name;

            InstItem item;
            item.name = name;
            item.path = dirPath;
            item.version = version;
            item.loader = loader;
            item.lastPlayed = QFileInfo(jsonPath).lastModified()
                                  .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
            m_instances << item;
        }
    }
}

void AppFileDialog::enterSystemMode()
{
    m_browseMode = BrowseMode::System;
    updateModeButtons();
    m_bodyStack->setCurrentWidget(m_browserPage);
    m_sidebar->setVisible(true);
    m_stepperBar->hide();
    m_resStepper->hide();
    if (m_instCatBar)
        m_instCatBar->hide();
    m_backBtn->setVisible(true);
    m_forwardBtn->setVisible(true);
    m_upBtn->setVisible(true);
    m_crumbBar->setVisible(true);
    m_addrBtn->setVisible(true);
    if (m_addrEditing)
        exitAddrEditing();
    m_addrEdit->setVisible(false);
    m_searchEdit->setVisible(true);
    m_sortCombo->setVisible(true);
    m_listViewBtn->setVisible(true);
    m_gridViewBtn->setVisible(true);
    m_previewBtn->setVisible(true);
    m_tree->setVisible(m_listViewBtn->isChecked());
    m_grid->setVisible(!m_listViewBtn->isChecked());
    m_previewPanel->setVisible(m_previewBtn->isChecked());
    m_chipBar->show();
    m_titleLabel->setText(tr("选择文件"));
    m_subtitleLabel->setText(tr("从本地磁盘选择要打开的文件"));
    populate();
    updateFooterState();
}

void AppFileDialog::enterInstanceMode()
{
    m_browseMode = BrowseMode::Instance;
    m_editionChosen = false;
    m_typeChosen = false;
    m_instPath.clear();
    m_instName.clear();
    m_instCategoryPath.clear();
    m_activeInstCategoryId.clear();
    m_selectedPaths.clear();
    m_searchText.clear();
    if (m_searchEdit)
        m_searchEdit->clear();
    m_activeCat = QStringLiteral("all");
    if (m_chipLayout) {
        for (int i = 0; i < m_chipLayout->count(); ++i) {
            if (auto *btn = qobject_cast<QPushButton *>(m_chipLayout->itemAt(i)->widget()))
                btn->setChecked(btn->property("cat").toString() == QStringLiteral("all"));
        }
    }
    updateModeButtons();
    m_stepperBar->show();
    m_resStepper->hide();
    m_chipBar->hide();
    if (m_instCatBar)
        m_instCatBar->hide();
    m_sidebar->hide();
    loadInstances();
    setInstLevel(InstLevel::Edition);
}

void AppFileDialog::enterNetworkMode()
{
    m_browseMode = BrowseMode::Network;
    m_selectedPaths.clear();
    m_searchText.clear();
    if (m_searchEdit)
        m_searchEdit->clear();
    updateModeButtons();
    m_bodyStack->setCurrentWidget(m_netPage);
    m_sidebar->setVisible(false);
    m_stepperBar->hide();
    m_resStepper->hide();
    m_chipBar->hide();
    if (m_instCatBar)
        m_instCatBar->hide();
    m_backBtn->setVisible(false);
    m_forwardBtn->setVisible(false);
    m_upBtn->setVisible(false);
    m_crumbBar->setVisible(false);
    m_addrBtn->setVisible(false);
    if (m_addrEditing)
        exitAddrEditing();
    m_addrEdit->setVisible(false);
    m_searchEdit->setVisible(false);
    m_sortCombo->setVisible(false);
    m_listViewBtn->setVisible(false);
    m_gridViewBtn->setVisible(false);
    m_previewBtn->setVisible(false);
    m_tree->hide();
    m_grid->hide();
    m_previewPanel->hide();
    m_titleLabel->setText(tr("选择文件"));
    m_subtitleLabel->setText(tr("输入网络链接，或切换到「系统式」/「实例式」浏览本地资源"));
    updateNetStatus();
    updateFooterState();
    if (m_netEdit)
        m_netEdit->setFocus();
}

void AppFileDialog::updateNetStatus()
{
    if (!m_netEdit || !m_netStatus)
        return;
    const QString text = m_netEdit->text().trimmed();
    if (text.isEmpty()) {
        m_netStatus->hide();
        updateFooterState();
        return;
    }
    const QUrl url(text);
    const bool ok = (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
                    && !url.host().isEmpty();
    if (ok) {
        m_netStatus->setText(tr("链接格式有效，可点击「打开」确认"));
        m_netStatus->setProperty("state", QStringLiteral("good"));
    } else {
        m_netStatus->setText(tr("链接无效或不完整（需以 http:// 或 https:// 开头）"));
        m_netStatus->setProperty("state", QStringLiteral("bad"));
    }
    m_netStatus->style()->unpolish(m_netStatus);
    m_netStatus->style()->polish(m_netStatus);
    m_netStatus->show();
    updateFooterState();
}

/* ============================ 资源模式逻辑 ============================ */

void AppFileDialog::setupResApis()
{
    if (m_cfApi || m_mrApi)
        return;
    m_cfApi = new CurseForgeAPI(this);
    const QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (!apiKey.isEmpty())
        m_cfApi->setApiKey(apiKey);
    m_mrApi = new ModrinthAPI(this);
    applyMcimToResApis();

    connect(m_cfApi, &CurseForgeAPI::searchCompleted, this, &AppFileDialog::onResSearchCompleted);
    connect(m_cfApi, &CurseForgeAPI::searchFailed, this, &AppFileDialog::onResSearchFailed);
    connect(m_cfApi, &CurseForgeAPI::modDetailReceived, this, &AppFileDialog::onResDetailReceived);
    connect(m_cfApi, &CurseForgeAPI::modDetailFailed, this, &AppFileDialog::onResDetailFailed);
    connect(m_mrApi, &ModrinthAPI::searchCompleted, this, &AppFileDialog::onResSearchCompleted);
    connect(m_mrApi, &ModrinthAPI::searchFailed, this, &AppFileDialog::onResSearchFailed);
    connect(m_mrApi, &ModrinthAPI::modDetailReceived, this, &AppFileDialog::onResDetailReceived);
    connect(m_mrApi, &ModrinthAPI::modDetailFailed, this, &AppFileDialog::onResDetailFailed);
}

void AppFileDialog::applyMcimToResApis()
{
    if (!m_cfApi || !m_mrApi)
        return;
    const QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
    const bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
    m_cfApi->setBaseUrl(useMcim ? QStringLiteral("https://mod.mcimirror.top/curseforge")
                                : QStringLiteral("https://api.curseforge.com"));
    m_mrApi->setBaseUrl(useMcim ? QStringLiteral("https://mod.mcimirror.top/modrinth")
                                : QStringLiteral("https://api.modrinth.com"));
}

void AppFileDialog::enterResourceMode()
{
    m_browseMode = BrowseMode::Resource;
    m_selectedPaths.clear();
    m_searchText.clear();
    if (m_searchEdit)
        m_searchEdit->clear();
    updateModeButtons();
    m_bodyStack->setCurrentWidget(m_resStack);
    m_sidebar->setVisible(false);
    m_stepperBar->hide();
    m_resStepper->show();
    m_chipBar->hide();
    if (m_instCatBar)
        m_instCatBar->hide();
    m_backBtn->setVisible(false);
    m_forwardBtn->setVisible(false);
    m_upBtn->setVisible(false);
    m_crumbBar->setVisible(false);
    m_addrBtn->setVisible(false);
    if (m_addrEditing)
        exitAddrEditing();
    m_addrEdit->setVisible(false);
    m_searchEdit->setVisible(false);
    m_sortCombo->setVisible(false);
    m_listViewBtn->setVisible(false);
    m_gridViewBtn->setVisible(false);
    m_previewBtn->setVisible(false);
    m_tree->hide();
    m_grid->hide();
    m_previewPanel->hide();
    m_titleLabel->setText(tr("选择资源"));
    m_subtitleLabel->setText(tr("从 CurseForge / Modrinth 获取 Java 版或基岩版资源"));
    setResLevel(ResLevel::Edition);
    updateFooterState();
}

void AppFileDialog::setResLevel(ResLevel lv)
{
    m_resLevel = lv;
    renderResStepper();
    renderResPages();
    updateFooterState();
}

void AppFileDialog::renderResStepper()
{
    if (m_browseMode != BrowseMode::Resource)
        return;
    const int cur = static_cast<int>(m_resLevel);
    const int count = m_resStepperLayout->count();
    int stepIdx = -1;
    for (int i = 0; i < count; ++i) {
        QWidget *w = m_resStepperLayout->itemAt(i)->widget();
        if (auto *btn = qobject_cast<QPushButton *>(w)) {
            ++stepIdx;
            const int idx = btn->property("step").toInt();
            const bool done = idx < cur;
            const bool current = idx == cur;
            btn->setProperty("state", current ? QStringLiteral("current")
                                              : (done ? QStringLiteral("done") : QStringLiteral("todo")));
            btn->setEnabled(done);
            QString text = btn->text();
            if (done && idx == 0 && m_resEditionChosen)
                text = tr("版本平台：%1 · %2")
                           .arg(m_resEditionIsJava ? tr("Java 版") : tr("基岩版"),
                                m_resPlatform == QStringLiteral("modrinth") ? tr("Modrinth") : tr("CurseForge"));
            else if (done && idx == 1 && !m_resTypeLabel.isEmpty())
                text = tr("类型：%1").arg(m_resTypeLabel);
            else if (done && idx == 2 && !m_resInfo.name.isEmpty())
                text = tr("资源：%1").arg(m_resInfo.name);
            btn->setText(text);
            btn->style()->unpolish(btn);
            btn->style()->polish(btn);
        }
    }
}

void AppFileDialog::renderResPages()
{
    if (m_browseMode != BrowseMode::Resource)
        return;
    m_resStepper->show();
    switch (m_resLevel) {
    case ResLevel::Edition:
        m_resStack->setCurrentWidget(m_resEditionPage);
        break;
    case ResLevel::Type:
        m_resStack->setCurrentWidget(m_resTypePage);
        break;
    case ResLevel::Search:
        m_resStack->setCurrentWidget(m_resSearchPage);
        if (m_resSearchEdit)
            m_resSearchEdit->setFocus();
        if (m_resResultList->count() == 0 && !m_resLoading)
            performResSearch(0);
        break;
    case ResLevel::Version:
        m_resStack->setCurrentWidget(m_resVersionPage);
        break;
    }
    updateFooterState();
}

void AppFileDialog::resChooseEditionPlatform(const QString &edition, const QString &platform)
{
    m_resEditionIsJava = (edition == QLatin1String("java"));
    m_resPlatform = platform;
    m_resEditionChosen = true;
    m_resTypeLabel.clear();
    m_resInfo = ModInfo();
    m_resVersion = ModVersionFile();
    populateResTypes();
    setResLevel(ResLevel::Type);
}

void AppFileDialog::populateResTypes()
{
    m_resTypeList->clear();
    struct TypeDef {
        QString label;
        QString cf;
        QString mr;
        QColor color;
    };
    QList<TypeDef> defs;
    if (m_resEditionIsJava) {
        defs = {
            { tr("模组"),   QStringLiteral("6"),     QStringLiteral("mod"),          QColor("#EC4899") },
            { tr("资源包"), QStringLiteral("12"),    QStringLiteral("resourcepack"), QColor("#10B981") },
            { tr("光影包"), QStringLiteral("6552"),  QStringLiteral("shader"),       QColor("#F59E0B") },
            { tr("数据包"), QStringLiteral("4546"),  QStringLiteral("datapack"),     QColor("#06B6D4") },
            { tr("整合包"), QStringLiteral("4471"),  QStringLiteral("modpack"),      QColor("#6366F1") },
            { tr("世界"),   QStringLiteral("17"),    QStringLiteral("mod"),          QColor("#8B5CF6") },
        };
    } else {
        defs = {
            { tr("附加包"), QStringLiteral("4984"),  QStringLiteral("mod"),          QColor("#10B981") },
            { tr("资源包"), QStringLiteral("6929"),  QStringLiteral("resourcepack"), QColor("#06B6D4") },
            { tr("地图"),   QStringLiteral("6913"),  QStringLiteral("mod"),          QColor("#F59E0B") },
            { tr("皮肤"),   QStringLiteral("6925"),  QStringLiteral("mod"),          QColor("#EC4899") },
            { tr("脚本"),   QStringLiteral("6940"),  QStringLiteral("mod"),          QColor("#6366F1") },
        };
    }

    for (const TypeDef &d : defs) {
        QListWidgetItem *item = new QListWidgetItem(m_resTypeList);
        item->setSizeHint(QSize(0, 62));
        QFrame *card = new QFrame;
        card->setObjectName(QStringLiteral("fdResCard"));
        QHBoxLayout *hl = new QHBoxLayout(card);
        hl->setContentsMargins(14, 10, 14, 10);
        hl->setSpacing(14);
        QLabel *ico = new QLabel(card);
        ico->setObjectName(QStringLiteral("fdResCardIco"));
        ico->setFixedSize(40, 40);
        ico->setAlignment(Qt::AlignCenter);
        ico->setPixmap(makeTileIcon(d.label.left(1), d.color, 40));
        hl->addWidget(ico);
        QLabel *nm = new QLabel(d.label, card);
        nm->setObjectName(QStringLiteral("fdResCardName"));
        hl->addWidget(nm, 1);
        QLabel *arrow = new QLabel(QStringLiteral("→"), card);
        arrow->setObjectName(QStringLiteral("fdInstArrow"));
        hl->addWidget(arrow);
        new ClickableFilter(card, [this, d]() {
            m_resTypeCf = d.cf;
            m_resTypeMr = d.mr;
            m_resTypeLabel = d.label;
            m_resQuery.clear();
            m_resPage = 0;
            m_resResults.clear();
            m_resInfo = ModInfo();
            m_resVersion = ModVersionFile();
            m_resResultList->clear();
            if (m_resSearchEdit)
                m_resSearchEdit->clear();
            setResLevel(ResLevel::Search);
        });
        m_resTypeList->setItemWidget(item, card);
    }
}

void AppFileDialog::performResSearch(int page)
{
    if (m_resLoading)
        return;
    m_resPage = page;
    m_resQuery = m_resSearchEdit ? m_resSearchEdit->text().trimmed() : QString();
    m_resLoading = true;
    m_resStatusLabel->setText(tr("正在搜索…"));
    m_resStatusLabel->setProperty("state", QStringLiteral("info"));
    m_resStatusLabel->style()->unpolish(m_resStatusLabel);
    m_resStatusLabel->style()->polish(m_resStatusLabel);
    m_resStatusLabel->show();

    if (m_resPlatform == QStringLiteral("modrinth")) {
        m_mrApi->setProjectType(m_resTypeMr);
        m_mrApi->searchMods(m_resQuery, QString(), QString(), page, 30, QStringLiteral("relevance"), QStringLiteral("desc"));
    } else {
        m_cfApi->setGameId(m_resEditionIsJava ? QStringLiteral("432") : QStringLiteral("78022"));
        m_cfApi->setClassId(m_resTypeCf);
        m_cfApi->searchMods(m_resQuery, QString(), QString(), page, 30, QStringLiteral("popularity"), QStringLiteral("desc"));
    }
}

void AppFileDialog::onResSearchCompleted(const ModSearchResult &result)
{
    m_resLoading = false;
    m_resStatusLabel->hide();
    m_resResults = result.mods;
    m_resResultList->clear();

    auto fmtCount = [](qint64 c) -> QString {
        if (c >= 1000000) return QString::number(c / 1000000.0, 'f', 1) + QStringLiteral("M");
        if (c >= 1000)    return QString::number(c / 1000.0, 'f', 1) + QStringLiteral("K");
        return QString::number(c);
    };

    for (int i = 0; i < m_resResults.size(); ++i) {
        const ModInfo &info = m_resResults[i];
        QListWidgetItem *item = new QListWidgetItem(m_resResultList);
        item->setData(Qt::UserRole, i);
        item->setSizeHint(QSize(0, 60));
        QFrame *card = new QFrame;
        card->setObjectName(QStringLiteral("fdResResultCard"));
        QHBoxLayout *hl = new QHBoxLayout(card);
        hl->setContentsMargins(12, 8, 12, 8);
        hl->setSpacing(12);
        QLabel *ico = new QLabel(card);
        ico->setObjectName(QStringLiteral("fdResCardIco"));
        ico->setFixedSize(40, 40);
        ico->setAlignment(Qt::AlignCenter);
        const QColor color = info.source == QStringLiteral("modrinth") ? QColor("#6366F1") : QColor("#F59E0B");
        ico->setPixmap(makeTileIcon(info.name.left(1).toUpper(), color, 40));
        hl->addWidget(ico);
        QWidget *txt = new QWidget(card);
        QVBoxLayout *tl = new QVBoxLayout(txt);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(2);
        QLabel *nm = new QLabel(info.name, txt);
        nm->setObjectName(QStringLiteral("fdResCardName"));
        nm->setWordWrap(true);
        QString meta = tr("%1 · 下载 %2")
                           .arg(info.author.isEmpty() ? tr("未知作者") : info.author,
                                fmtCount(info.downloadCount));
        if (!info.latestVersion.isEmpty())
            meta += tr(" · 最新 %1").arg(info.latestVersion);
        QLabel *ms = new QLabel(meta, txt);
        ms->setObjectName(QStringLiteral("fdResCardDesc"));
        tl->addWidget(nm);
        tl->addWidget(ms);
        hl->addWidget(txt, 1);
        QLabel *arrow = new QLabel(QStringLiteral("→"), card);
        arrow->setObjectName(QStringLiteral("fdInstArrow"));
        hl->addWidget(arrow);
        new ClickableFilter(card, [this, i]() {
            openResVersions(i);
        });
        m_resResultList->setItemWidget(item, card);
    }

    m_resPageLabel->setText(tr("第 %1 页").arg(m_resPage + 1));
    m_resPrevBtn->setEnabled(m_resPage > 0);
    m_resNextBtn->setEnabled(result.hasMore);

    if (m_resResults.isEmpty()) {
        m_resStatusLabel->setText(tr("没有找到相关资源，请更换关键词重试"));
        m_resStatusLabel->setProperty("state", QStringLiteral("info"));
        m_resStatusLabel->style()->unpolish(m_resStatusLabel);
        m_resStatusLabel->style()->polish(m_resStatusLabel);
        m_resStatusLabel->show();
    }
}

void AppFileDialog::onResSearchFailed(const QString &error)
{
    m_resLoading = false;
    m_resStatusLabel->setText(tr("搜索失败：%1").arg(error));
    m_resStatusLabel->setProperty("state", QStringLiteral("bad"));
    m_resStatusLabel->style()->unpolish(m_resStatusLabel);
    m_resStatusLabel->style()->polish(m_resStatusLabel);
    m_resStatusLabel->show();
}

void AppFileDialog::openResVersions(int idx)
{
    if (idx < 0 || idx >= m_resResults.size())
        return;
    m_resInfo = m_resResults[idx];
    m_resStatusLabel->setText(tr("正在加载「%1」的版本列表…").arg(m_resInfo.name));
    m_resStatusLabel->setProperty("state", QStringLiteral("info"));
    m_resStatusLabel->style()->unpolish(m_resStatusLabel);
    m_resStatusLabel->style()->polish(m_resStatusLabel);
    m_resStatusLabel->show();
    if (m_resPlatform == QStringLiteral("modrinth"))
        m_mrApi->fetchModDetail(m_resInfo.id);
    else
        m_cfApi->fetchModDetail(m_resInfo.id);
}

void AppFileDialog::onResDetailReceived(const ModInfo &detail)
{
    m_resInfo = detail;
    m_resStatusLabel->hide();
    populateResVersionList();
    setResLevel(ResLevel::Version);
}

void AppFileDialog::onResDetailFailed(const QString &error)
{
    m_resStatusLabel->setText(tr("加载版本列表失败：%1").arg(error));
    m_resStatusLabel->setProperty("state", QStringLiteral("bad"));
    m_resStatusLabel->style()->unpolish(m_resStatusLabel);
    m_resStatusLabel->style()->polish(m_resStatusLabel);
    m_resStatusLabel->show();
}

void AppFileDialog::populateResVersionList()
{
    m_resVersionList->clear();
    m_resVersion = ModVersionFile();

    m_resVersionHeader->setText(tr("资源：%1\n作者：%2 · 平台：%3")
                                    .arg(m_resInfo.name,
                                         m_resInfo.author.isEmpty() ? tr("未知") : m_resInfo.author,
                                         m_resPlatform == QStringLiteral("modrinth") ? tr("Modrinth") : tr("CurseForge")));

    const QList<ModVersionFile> &files = m_resInfo.versionFiles;
    if (files.isEmpty()) {
        QListWidgetItem *empty = new QListWidgetItem(tr("该资源没有可用的版本文件"), m_resVersionList);
        empty->setFlags(Qt::NoItemFlags);
        empty->setSizeHint(QSize(0, 48));
        return;
    }

    for (int i = 0; i < files.size(); ++i) {
        const ModVersionFile &vf = files[i];
        QListWidgetItem *item = new QListWidgetItem(m_resVersionList);
        item->setData(Qt::UserRole, i);
        item->setSizeHint(QSize(0, 60));
        QFrame *card = new QFrame;
        card->setObjectName(QStringLiteral("fdResVersionCard"));
        QHBoxLayout *hl = new QHBoxLayout(card);
        hl->setContentsMargins(14, 8, 14, 8);
        hl->setSpacing(14);

        QWidget *txt = new QWidget(card);
        QVBoxLayout *tl = new QVBoxLayout(txt);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(2);
        QString verText = vf.version.isEmpty() ? vf.fileName : vf.version;
        if (vf.releaseType == QStringLiteral("beta"))
            verText += QStringLiteral("  ·  BETA");
        else if (vf.releaseType == QStringLiteral("alpha"))
            verText += QStringLiteral("  ·  ALPHA");
        QLabel *ver = new QLabel(verText, txt);
        ver->setObjectName(QStringLiteral("fdResVerName"));
        QStringList gv = vf.gameVersions;
        if (gv.size() > 4)
            gv = gv.mid(0, 4) << QStringLiteral("…");
        QLabel *meta = new QLabel(
            tr("MC %1 · %2")
                .arg(gv.isEmpty() ? tr("多版本") : gv.join(QStringLiteral(", ")),
                     vf.loaders.isEmpty() ? tr("全加载器") : vf.loaders.join(QStringLiteral(", "))),
            txt);
        meta->setObjectName(QStringLiteral("fdInstMeta"));
        tl->addWidget(ver);
        tl->addWidget(meta);
        hl->addWidget(txt, 1);

        QString right;
        if (!vf.datePublished.isNull())
            right += vf.datePublished.toString(QStringLiteral("yyyy-MM-dd"));
        if (vf.fileSize > 0) {
            if (!right.isEmpty())
                right += QStringLiteral(" · ");
            right += QLocale::system().formattedDataSize(vf.fileSize);
        }
        QLabel *side = new QLabel(right, card);
        side->setObjectName(QStringLiteral("fdInstTime"));
        hl->addWidget(side);

        new ClickableFilter(card, [this, item]() {
            m_resVersionList->setCurrentItem(item);
        });

        m_resVersionList->setItemWidget(item, card);
    }
    updateFooterState();
}

void AppFileDialog::buildResSelected()
{
    m_resSelected = SelectedResource();
    m_resSelected.edition = m_resEditionIsJava ? QStringLiteral("java") : QStringLiteral("bedrock");
    m_resSelected.platform = m_resPlatform;
    m_resSelected.resourceType = m_resTypeLabel;
    m_resSelected.name = m_resInfo.name;
    m_resSelected.author = m_resInfo.author;
    m_resSelected.version = m_resVersion.version;
    m_resSelected.fileName = m_resVersion.fileName;
    m_resSelected.downloadUrl = m_resVersion.downloadUrl;
    m_resSelected.fileSize = m_resVersion.fileSize;
    m_resSelected.gameVersions = m_resVersion.gameVersions;
    m_resSelected.loaders = m_resVersion.loaders;
    m_resSelected.pageUrl = m_resInfo.pageUrl;
}

void AppFileDialog::chooseEdition(bool isJava)
{
    m_editionIsJava = isJava;
    m_editionChosen = true;
    m_typeChosen = false;
    rebuildInstanceList();
    setInstLevel(InstLevel::Instance);
}

void AppFileDialog::rebuildInstanceList()
{
    if (!m_instanceList)
        return;
    m_instanceList->clear();
    m_instName.clear();
    m_instCategoryPath.clear();

    auto addCard = [this](const InstItem &it) {
        QListWidgetItem *item = new QListWidgetItem(m_instanceList);
        item->setData(Qt::UserRole, it.path);
        item->setSizeHint(QSize(0, 68));

        QFrame *card = new QFrame;
        card->setObjectName(QStringLiteral("fdInstCard"));
        QHBoxLayout *hl = new QHBoxLayout(card);
        hl->setContentsMargins(14, 10, 14, 10);
        hl->setSpacing(12);

        QLabel *ico = new QLabel(it.name.left(1).toUpper(), card);
        ico->setObjectName(QStringLiteral("fdInstAvatar"));
        ico->setFixedSize(42, 42);
        ico->setAlignment(Qt::AlignCenter);
        hl->addWidget(ico);

        QWidget *txt = new QWidget(card);
        QVBoxLayout *tl = new QVBoxLayout(txt);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(2);
        QLabel *nm = new QLabel(it.name, txt);
        nm->setObjectName(QStringLiteral("fdInstName"));
        tl->addWidget(nm);

        QString metaText;
        if (m_editionIsJava) {
            metaText = (it.loader.isEmpty() ? QString() : it.loader + QStringLiteral(" · "))
                + tr("MC ") + it.version;
        } else {
            metaText = tr("基岩版")
                + (it.version.isEmpty() ? QString() : QStringLiteral(" · ") + it.version);
        }
        QLabel *meta = new QLabel(metaText, txt);
        meta->setObjectName(QStringLiteral("fdInstMeta"));
        tl->addWidget(meta);

        const QString last = it.lastPlayed.trimmed();
        QLabel *time = new QLabel(last.isEmpty() ? tr("尚未游玩") : tr("上次游玩：%1").arg(last), txt);
        time->setObjectName(QStringLiteral("fdInstTime"));
        tl->addWidget(time);

        // 实例手动分类标签
        InstItem sel = it;
        if (sel.catPath.isEmpty())
            sel.catPath = sel.path;
        const QString instCat = SettingsManager::instance()->getInstanceCategory(sel.catPath);
        if (!instCat.isEmpty()) {
            QLabel *chip = new QLabel(instCat, txt);
            chip->setObjectName(QStringLiteral("fdInstCatChip"));
            chip->setProperty("categoryChip", true);
            tl->addWidget(chip);
        }
        hl->addWidget(txt, 1);

        QLabel *arrow = new QLabel(QStringLiteral("→"), card);
        arrow->setObjectName(QStringLiteral("fdInstArrow"));
        hl->addWidget(arrow);

        new ClickableFilter(card, [this, sel]() {
            m_instPath = sel.path;
            m_instCategoryPath = sel.catPath;
            m_instName = sel.name;
            setInstLevel(InstLevel::Type);
        });
        new ContextMenuFilter(card, [this, sel]() {
            QMenu menu(this);
            menu.setObjectName(QStringLiteral("fdCtxMenu"));
            QMenu *catSub = menu.addMenu(tr("移动分类"));
            addInstanceCategoryMenu(catSub, sel.catPath);
            QAction *openAct = menu.addAction(tr("打开文件位置"));
            connect(openAct, &QAction::triggered, this, [this, sel]() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(sel.path));
            });
            QAction *copyAct = menu.addAction(tr("复制路径"));
            connect(copyAct, &QAction::triggered, this, [sel]() {
                QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(sel.path));
            });
            menu.exec(QCursor::pos());
        });
        m_instanceList->setItemWidget(item, card);
    };

    if (m_editionIsJava) {
        for (const InstItem &it : m_instances)
            addCard(it);
    } else {
        BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
        mgr->ensureInitialized();
        const QVector<BedrockInstance> insts = mgr->instances();
        if (!insts.isEmpty()) {
            for (const BedrockInstance &b : insts) {
                InstItem it;
                it.name = b.name;
                it.path = b.dataDir + QStringLiteral("/com.mojang");
                it.catPath = b.dataDir;
                it.version = b.version;
                it.lastPlayed = b.lastPlayed.isValid()
                    ? b.lastPlayed.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString();
                addCard(it);
            }
        } else {
            // 兜底：以系统基岩版数据目录作为唯一实例
            const QString com = bedrockComMojangDir();
            if (!com.isEmpty()) {
                InstItem it;
                it.name = tr("系统基岩版数据");
                it.path = com;
                it.catPath = QFileInfo(com).absolutePath();
                it.version = QString();
                addCard(it);
            }
        }
    }
}

void AppFileDialog::addInstanceCategoryMenu(QMenu *menu, const QString &catPath)
{
    if (!menu)
        return;
    SettingsManager *sm = SettingsManager::instance();
    const QString currentCat = sm->getInstanceCategory(catPath);
    const QStringList cats = sm->getInstanceCategories();

    QAction *noneAction = menu->addAction(tr("未分类"));
    noneAction->setCheckable(true);
    noneAction->setChecked(currentCat.isEmpty());
    for (const QString &cat : cats) {
        QAction *act = menu->addAction(cat);
        act->setCheckable(true);
        act->setChecked(currentCat == cat);
    }
    menu->addSeparator();
    QAction *newAction = menu->addAction(tr("新建分类…"));

    connect(menu, &QMenu::triggered, this, [this, menu, noneAction, newAction, catPath](QAction *action) {
        if (!action)
            return;
        SettingsManager *sm2 = SettingsManager::instance();
        if (action == noneAction) {
            sm2->setInstanceCategory(catPath, QString());
        } else if (action == newAction) {
            bool ok = false;
            const QString name = QInputDialog::getItem(this, tr("新建分类"),
                                    tr("分类名称："), sm2->getInstanceCategories(), 0, true, &ok).trimmed();
            if (ok && !name.isEmpty()) {
                if (!sm2->getInstanceCategories().contains(name))
                    sm2->addInstanceCategory(name);
                sm2->setInstanceCategory(catPath, name);
            }
        } else {
            for (QAction *a : menu->actions()) {
                if (a == action && a->isCheckable()) {
                    sm2->setInstanceCategory(catPath, action->text());
                    break;
                }
            }
        }
        rebuildInstanceList();
    });
}

void AppFileDialog::addFileCategoryMenu(QMenu *menu, const QString &fileName)
{
    if (!menu || !m_catManager)
        return;
    const QString currentCat = m_catManager->categoryOf(fileName);
    const QList<LocalResourceCategory> cats = m_catManager->categories();

    if (cats.isEmpty()) {
        QAction *emptyAction = menu->addAction(tr("暂无分类"));
        emptyAction->setEnabled(false);
    }
    for (const LocalResourceCategory &c : cats) {
        QAction *act = menu->addAction(c.name);
        act->setCheckable(true);
        act->setChecked(currentCat == c.id);
    }
    QAction *clearAction = menu->addAction(tr("清除分类"));
    clearAction->setEnabled(!currentCat.isEmpty());
    menu->addSeparator();
    QAction *newAction = menu->addAction(tr("新建分类…"));

    connect(menu, &QMenu::triggered, this, [this, menu, fileName, clearAction, newAction](QAction *action) {
        if (!action)
            return;
        if (action == clearAction) {
            m_catManager->assignCategory(fileName, QString());
        } else if (action == newAction) {
            bool ok = false;
            const QString name = QInputDialog::getItem(this, tr("新建分类"), tr("分类名称："),
                                LocalCategoryManager::suggestedNames(m_catManager->resourceType()),
                                0, true, &ok).trimmed();
            if (ok && !name.isEmpty()) {
                const QString id = m_catManager->addCategory(name);
                m_catManager->assignCategory(fileName, id);
            }
        } else {
            for (QAction *a : menu->actions()) {
                if (a == action && a->isCheckable()) {
                    const QString id = m_catManager->categoryId(action->text());
                    if (!id.isEmpty())
                        m_catManager->assignCategory(fileName, id);
                    break;
                }
            }
        }
        rebuildInstanceChips();
        populate();
    });
}

void AppFileDialog::refreshInstCategoryManager()
{
    if (!m_catManager || m_instCategoryPath.isEmpty())
        return;
    m_catManager->setInstancePath(m_instCategoryPath);
    m_catManager->setResourceType(m_resType.id);
}

void AppFileDialog::setInstLevel(InstLevel lv)
{
    m_instLevel = lv;
    renderStepper();
    renderInstPages();
    updateFooterState();
}

void AppFileDialog::renderStepper()
{
    const int cur = static_cast<int>(m_instLevel);
    const int count = m_stepperLayout->count();
    int stepIdx = -1;
    for (int i = 0; i < count; ++i) {
        QWidget *w = m_stepperLayout->itemAt(i)->widget();
        if (auto *btn = qobject_cast<QPushButton *>(w)) {
            ++stepIdx;
            const int idx = btn->property("step").toInt();
            const bool done = idx < cur;
            const bool current = idx == cur;
            btn->setProperty("state", current ? QStringLiteral("current")
                                               : (done ? QStringLiteral("done") : QStringLiteral("todo")));
            btn->setEnabled(done);
            QString text = btn->text();
            // 附加当前选择值
            if (done && idx == 0 && m_editionChosen)
                text = tr("版本：%1").arg(m_editionIsJava ? tr("Java 版") : tr("基岩版"));
            else if (done && idx == 1 && !m_instName.isEmpty())
                text = tr("实例：%1").arg(m_instName);
            else if (done && idx == 2 && m_typeChosen)
                text = tr("类型：%1").arg(m_resType.name);
            btn->setText(text);
            btn->style()->unpolish(btn);
            btn->style()->polish(btn);
        }
    }
}

void AppFileDialog::renderInstPages()
{
    const bool files = m_instLevel == InstLevel::Files;
    m_stepperBar->show();
    m_chipBar->setVisible(false);       // 实例式下系统类型筛选条不适用
    m_instCatBar->setVisible(files);    // 实例式文件列表：显示手动分类筛选条
    m_sidebar->hide();
    m_backBtn->setVisible(false);
    m_forwardBtn->setVisible(false);
    m_upBtn->setVisible(false);
    m_crumbBar->setVisible(false);
    m_addrBtn->setVisible(false);
    if (m_addrEditing)
        exitAddrEditing();
    m_addrEdit->setVisible(false);
    m_searchEdit->setVisible(files);
    m_sortCombo->setVisible(files);
    m_listViewBtn->setVisible(files);
    m_gridViewBtn->setVisible(files);
    m_previewBtn->setVisible(files);
    m_previewPanel->setVisible(files && m_previewBtn->isChecked());

    if (files) {
        m_bodyStack->setCurrentWidget(m_browserPage);
        m_tree->setVisible(m_listViewBtn->isChecked());
        m_grid->setVisible(!m_listViewBtn->isChecked());
        rebuildInstanceChips();
        populate();
    } else {
        m_bodyStack->setCurrentWidget(m_instStack);
        if (m_instLevel == InstLevel::Edition) {
            m_instStack->setCurrentWidget(m_editionPage);
        } else if (m_instLevel == InstLevel::Instance) {
            if (auto *t = m_instancePage->findChild<QLabel *>(QStringLiteral("fdInstTitle")))
                t->setText(tr("选择实例（%1）")
                               .arg(m_editionIsJava ? tr("Java 版") : tr("基岩版")));
            if (auto *d = m_instancePage->findChild<QLabel *>(QStringLiteral("fdInstDesc")))
                d->setText(tr("选择要浏览资源的游戏实例"));
            m_instStack->setCurrentWidget(m_instancePage);
        } else if (m_instLevel == InstLevel::Type) {
            refreshTypeList();
            m_instStack->setCurrentWidget(m_typePage);
            if (auto *t = m_typePage->findChild<QLabel *>(QStringLiteral("fdInstTitle")))
                t->setText(tr("选择资源类型（%1）")
                               .arg(m_editionIsJava ? tr("Java 版") : tr("基岩版")));
            if (auto *d = m_typePage->findChild<QLabel *>(QStringLiteral("fdInstDesc"))) {
                const QString scoped = !m_instName.isEmpty()
                    ? m_instName + QStringLiteral(" · ")
                    : QString();
                d->setText(scoped + tr("点击类型查看其中文件"));
            }
        }
    }
    m_titleLabel->setText(tr("实例资源选择"));
    if (files) {
        m_subtitleLabel->setText(
            (m_editionIsJava ? tr("Java 版") : tr("基岩版"))
                + (!m_instName.isEmpty() ? QStringLiteral(" ▸ ") + m_instName : QString())
                + (m_typeChosen ? QStringLiteral(" ▸ ") + m_resType.name : QString()));
    } else {
        m_subtitleLabel->setText(tr("按版本 / 实例 / 资源类型浏览本地资源"));
    }

    // 返回按钮（版本页无返回；类型页返回实例，实例页返回版本）
    auto setBack = [this](QWidget *page, int step) {
        if (!page)
            return;
        if (auto *btn = page->findChild<QPushButton *>(QStringLiteral("fdBackBtn"))) {
            const bool visible = (step == 2 || step == 3);
            btn->setVisible(visible);
            if (visible) {
                disconnect(btn, nullptr, nullptr, nullptr);
                connect(btn, &QPushButton::clicked, this, [this, step]() {
                    if (step == 3)
                        setInstLevel(InstLevel::Instance);
                    else
                        setInstLevel(InstLevel::Edition);
                });
            }
        }
    };
    setBack(m_editionPage, 1);
    setBack(m_instancePage, 2);
    setBack(m_typePage, 3);
}

void AppFileDialog::openResType(const ResourceType &rt)
{
    m_resType = rt;
    m_typeChosen = true;

    QString base;
    if (m_editionIsJava) {
        base = resolveResourceDir(m_instPath, rt.subDir);
    } else {
        base = m_instPath.isEmpty() ? QString() : m_instPath + QLatin1Char('/') + rt.subDir;
    }

    m_scopeFilters = rt.filters;
    m_scopeDirsOnly = rt.dirsOnly;
    m_currentDir = base;
    m_history.clear();
    m_historyIdx = -1;
    m_selectedPaths.clear();
    m_activeInstCategoryId.clear();
    refreshInstCategoryManager();
    setInstLevel(InstLevel::Files);
}

int AppFileDialog::countFilesIn(const QString &base, const ResourceType &rt) const
{
    QDir d(base);
    if (!d.exists())
        return 0;
    int n = 0;
    const QFileInfoList infos = d.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : infos) {
        if (rt.dirsOnly) {
            if (fi.isDir())
                ++n;
        } else if (fi.isFile() && matchesFilterList(fi.fileName(), rt.filters)) {
            ++n;
        }
    }
    return n;
}

void AppFileDialog::refreshTypeList()
{
    m_resTypes.clear();
    m_typeList->clear();

    struct Def { QString id, name, sub; QStringList filters; bool dirs; };
    QList<Def> defs;
    if (m_editionIsJava) {
        defs << Def{ QStringLiteral("mods"), tr("模组"), QStringLiteral("mods"),
                     { QStringLiteral("*.jar"), QStringLiteral("*.disabled") }, false }
             << Def{ QStringLiteral("resourcepacks"), tr("资源包"), QStringLiteral("resourcepacks"),
                     { QStringLiteral("*.zip") }, false }
             << Def{ QStringLiteral("shaderpacks"), tr("光影包"), QStringLiteral("shaderpacks"),
                     { QStringLiteral("*.zip") }, false }
             << Def{ QStringLiteral("schematics"), tr("投影"), QStringLiteral("schematics"),
                     { QStringLiteral("*.litematic"), QStringLiteral("*.nbt"),
                       QStringLiteral("*.schematic"), QStringLiteral("*.schem") }, false }
             << Def{ QStringLiteral("saves"), tr("存档"), QStringLiteral("saves"), {}, true }
             << Def{ QStringLiteral("datapacks"), tr("数据包"), QStringLiteral("datapacks"),
                     { QStringLiteral("*.zip") }, false };
    } else {
        defs << Def{ QStringLiteral("resource_packs"), tr("资源包"), QStringLiteral("resource_packs"),
                     { QStringLiteral("*.zip"), QStringLiteral("*.mcpack") }, false }
             << Def{ QStringLiteral("behavior_packs"), tr("行为包"), QStringLiteral("behavior_packs"),
                     { QStringLiteral("*.mcpack"), QStringLiteral("*.mcaddon"), QStringLiteral("*.zip") }, false }
             << Def{ QStringLiteral("minecraftWorlds"), tr("世界"), QStringLiteral("minecraftWorlds"), {}, true }
             << Def{ QStringLiteral("skins"), tr("皮肤"), QStringLiteral("skins"),
                     { QStringLiteral("*.png") }, false };
    }

    for (int i = 0; i < defs.size(); ++i) {
        const Def &d = defs.at(i);
        ResourceType rt;
        rt.id = d.id;
        rt.name = d.name;
        rt.subDir = d.sub;
        rt.filters = d.filters;
        rt.dirsOnly = d.dirs;
        m_resTypes << rt;

        QString base;
        if (m_editionIsJava)
            base = resolveResourceDir(m_instPath, rt.subDir);
        else
            base = m_instPath.isEmpty() ? QString() : m_instPath + QLatin1Char('/') + rt.subDir;
        const int count = countFilesIn(base, rt);

        QColor tc(QStringLiteral("#64748B"));
        const QString id = rt.id;
        if (id.contains(QStringLiteral("mods"))) tc = QColor(QStringLiteral("#EC4899"));
        else if (id.contains(QStringLiteral("pack")) || id.contains(QStringLiteral("skin"))) tc = QColor(QStringLiteral("#10B981"));
        else if (id.contains(QStringLiteral("shader"))) tc = QColor(QStringLiteral("#06B6D4"));
        else if (id.contains(QStringLiteral("schem"))) tc = QColor(QStringLiteral("#8B5CF6"));
        else if (id.contains(QStringLiteral("save")) || id.contains(QStringLiteral("World")) || id.contains(QStringLiteral("world"))) tc = QColor(QStringLiteral("#F59E0B"));

        auto *item = new QListWidgetItem(rt.name + QStringLiteral("\n") + tr("%1 项").arg(count), m_typeList);
        item->setIcon(makeTileIcon(rt.name.left(1), tc, 44));
        item->setData(Qt::UserRole, i);
        item->setToolTip(rt.name);
        item->setSizeHint(QSize(192, 92));
    }
}

QString AppFileDialog::resolveResourceDir(const QString &instancePath, const QString &subDir)
{
    const QString iso = instancePath + QLatin1Char('/') + subDir;
    if (QDir(iso).exists())
        return iso;
    QDir parent(instancePath);
    parent.cdUp();
    const QString shared = parent.absoluteFilePath(subDir);
    if (QDir(shared).exists())
        return shared;
    return iso;
}

QString AppFileDialog::bedrockComMojangDir()
{
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (local.isEmpty())
        return QString();
    const QString pkgRoot = local + QStringLiteral("/Packages");
    QDir pkg(pkgRoot);
    if (!pkg.exists())
        return QString();
    const QFileInfoList pkgs = pkg.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : pkgs) {
        const QString fn = fi.fileName();
        if (fn.startsWith(QStringLiteral("Microsoft.Minecraft"))) {
            const QString com = fi.absoluteFilePath()
                + QStringLiteral("/LocalState/games/com.mojang");
            if (QDir(com).exists())
                return com;
        }
    }
    return QString();
}

/* ============================ 通用 ============================ */

QString AppFileDialog::fileTypeText(const Entry &e)
{
    if (e.isDrive)
        return tr("驱动器");
    if (e.isDir)
        return tr("文件夹");
    const QString ext = e.ext.toLower();
    if (ext == QStringLiteral("jar"))
        return tr("模组 · Java 程序");
    if (ext == QStringLiteral("png") || ext == QStringLiteral("jpg")
        || ext == QStringLiteral("jpeg") || ext == QStringLiteral("gif")
        || ext == QStringLiteral("webp"))
        return tr("图片");
    if (ext == QStringLiteral("zip") || ext == QStringLiteral("rar")
        || ext == QStringLiteral("7z") || ext == QStringLiteral("mcpack")
        || ext == QStringLiteral("mcaddon"))
        return tr("压缩文件");
    if (ext == QStringLiteral("dat") || ext == QStringLiteral("mcworld")
        || ext == QStringLiteral("mcr") || ext == QStringLiteral("litematic")
        || ext == QStringLiteral("nbt") || ext == QStringLiteral("schematic")
        || ext == QStringLiteral("schem"))
        return tr("存档 / 投影");
    if (ext == QStringLiteral("json") || ext == QStringLiteral("toml")
        || ext == QStringLiteral("config"))
        return tr("配置文件");
    if (ext == QStringLiteral("log"))
        return tr("日志文件");
    if (ext == QStringLiteral("txt"))
        return tr("文本文件");
    return tr("文件");
}

QString AppFileDialog::fileCategory(const QString &ext, bool isDir)
{
    if (isDir)
        return QStringLiteral("folder");
    const QString e = ext.toLower();
    if (e == QStringLiteral("jar"))
        return QStringLiteral("mod");
    if (e == QStringLiteral("png") || e == QStringLiteral("jpg")
        || e == QStringLiteral("jpeg") || e == QStringLiteral("gif")
        || e == QStringLiteral("webp"))
        return QStringLiteral("image");
    if (e == QStringLiteral("zip") || e == QStringLiteral("rar")
        || e == QStringLiteral("7z") || e == QStringLiteral("mcpack")
        || e == QStringLiteral("mcaddon"))
        return QStringLiteral("archive");
    if (e == QStringLiteral("dat") || e == QStringLiteral("mcworld")
        || e == QStringLiteral("mcr") || e == QStringLiteral("litematic")
        || e == QStringLiteral("nbt") || e == QStringLiteral("schematic")
        || e == QStringLiteral("schem"))
        return QStringLiteral("save");
    return QStringLiteral("other");
}

QColor AppFileDialog::categoryColor(const QString &cat) const
{
    if (cat == QStringLiteral("mod"))
        return QColor(QStringLiteral("#EC4899"));
    if (cat == QStringLiteral("image"))
        return QColor(QStringLiteral("#10B981"));
    if (cat == QStringLiteral("save"))
        return QColor(QStringLiteral("#F59E0B"));
    if (cat == QStringLiteral("archive"))
        return QColor(QStringLiteral("#6366F1"));
    if (cat == QStringLiteral("folder"))
        return QColor(QStringLiteral("#F59E0B"));
    return QColor(QStringLiteral("#64748B"));
}

QString AppFileDialog::categoryIconLetter(const Entry &e) const
{
    return e.name.left(1).toUpper();
}

QPixmap AppFileDialog::makeTileIcon(const QString &letter, const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, size, size);
    g.setColorAt(0, color.lighter(115));
    g.setColorAt(1, color.darker(132));
    p.setBrush(g);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(1, 1, size - 2, size - 2), size * 0.24, size * 0.24);
    p.setPen(QColor(255, 255, 255, 235));
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(int(size * 0.5));
    p.setFont(f);
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, letter);
    return pm;
}

QStringList AppFileDialog::selectedPaths() const
{
    QStringList paths;
    if (m_tree->isVisible()) {
        for (QTreeWidgetItem *it : m_tree->selectedItems())
            paths << it->data(0, Qt::UserRole).toString();
    } else if (m_grid->isVisible()) {
        for (QListWidgetItem *it : m_grid->selectedItems())
            paths << it->data(Qt::UserRole).toString();
    }
    return paths;
}

void AppFileDialog::onSelectionChanged()
{
    const QStringList paths = selectedPaths();
    m_selectedPaths = QSet<QString>(paths.begin(), paths.end());

    updateSelectionBadge();
    updatePreview();
    updateFooterState();

    if (paths.size() == 1 && !m_selectedPaths.isEmpty()) {
        const QFileInfo fi(paths.first());
        if (!fi.isDir() && m_mode != SaveFile) {
            m_fileNameEdit->setText(fi.fileName());
        }
    }
}

void AppFileDialog::applySelectionToVisibleWidget()
{
    if (m_tree->isVisible()) {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = m_tree->topLevelItem(i);
            it->setSelected(m_selectedPaths.contains(it->data(0, Qt::UserRole).toString()));
        }
    } else if (m_grid->isVisible()) {
        for (int i = 0; i < m_grid->count(); ++i) {
            QListWidgetItem *it = m_grid->item(i);
            it->setSelected(m_selectedPaths.contains(it->data(Qt::UserRole).toString()));
        }
    }
}

void AppFileDialog::updateSelectionBadge()
{
    const int n = m_selectedPaths.size();
    if (n > 0) {
        m_selBadge->setText(tr("已选择 %1 项").arg(n));
        m_selBadge->show();
    } else {
        m_selBadge->hide();
    }
}

void AppFileDialog::updatePreview()
{
    const QStringList paths = selectedPaths();
    if (paths.size() != 1) {
        m_pvEmpty->setVisible(true);
        m_pvIcon->setVisible(false);
        m_pvName->setVisible(false);
        m_pvType->setVisible(false);
        m_pvRows->setVisible(false);
        m_pvEmpty->setText(paths.isEmpty()
                               ? tr("选择一个文件\n查看详细信息")
                               : tr("已选择 %1 项").arg(paths.size()));
        return;
    }
    const QFileInfo fi(paths.first());
    m_pvEmpty->setVisible(false);
    m_pvIcon->setVisible(true);
    m_pvName->setVisible(true);
    m_pvType->setVisible(true);
    m_pvRows->setVisible(true);

    const bool isDir = fi.isDir();
    const QString ext = fi.suffix();
    const QString cat = fileCategory(ext, isDir);
    const QColor color = categoryColor(cat);
    m_pvIcon->setPixmap(makeTileIcon(fi.fileName().left(1).toUpper(), color, 64));
    m_pvName->setText(fi.fileName());
    m_pvType->setText(QStringLiteral("●  ") + fileTypeText(Entry{ fi.fileName(), fi.absoluteFilePath(), isDir, false, ext, isDir ? -1 : fi.size(), fi.lastModified(), cat }));

    QString rows;
    rows += tr("大小：%1\n").arg(isDir ? tr("文件夹") : QLocale::system().formattedDataSize(fi.size()));
    rows += tr("类型：%1\n").arg(isDir ? tr("文件夹") : (!ext.isEmpty() ? QStringLiteral(".") + ext : tr("文件")));
    rows += tr("修改时间：%1\n").arg(fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    rows += tr("路径：%1").arg(fi.absoluteFilePath());
    m_pvRows->setText(rows);
}

void AppFileDialog::updateFooterState()
{
    const bool inst = m_browseMode == BrowseMode::Instance;
    const bool net = m_browseMode == BrowseMode::Network;
    const bool res = m_browseMode == BrowseMode::Resource;
    const bool files = inst && m_instLevel == InstLevel::Files;

    bool canOpen;
    if (res)
        canOpen = !m_resVersion.downloadUrl.isEmpty();
    else if (net)
        canOpen = !m_netEdit->text().trimmed().isEmpty();
    else
        canOpen = inst ? files : true;
    m_okBtn->setEnabled(canOpen);

    bool showField = !inst || files;
    if (net || res)
        showField = false;
    m_fileNameLabel->setVisible(showField);
    m_fileNameEdit->setVisible(showField);

    if (net || res) {
        m_statusLabel->hide();
    } else if (inst && !files) {
        m_statusLabel->setText(tr("请先在上方完成选择"));
        m_statusLabel->show();
    } else {
        m_statusLabel->hide();
    }
}

void AppFileDialog::updateModeButtons()
{
    m_systemBtn->setChecked(m_browseMode == BrowseMode::System);
    m_instanceBtn->setChecked(m_browseMode == BrowseMode::Instance);
    if (m_netBtn)
        m_netBtn->setChecked(m_browseMode == BrowseMode::Network);
    if (m_resBtn)
        m_resBtn->setChecked(m_browseMode == BrowseMode::Resource);
}

QString AppFileDialog::selectedFilePath() const
{
    const QStringList paths = selectedPaths();
    for (const QString &p : paths) {
        if (!QFileInfo(p).isDir())
            return p;
    }
    return QString();
}

QString AppFileDialog::pathEditText() const
{
    QString text = m_fileNameEdit->text().trimmed();
    if (text.isEmpty())
        return QString();
    if (QFileInfo(text).isAbsolute())
        return text;
    return QDir(m_currentDir).filePath(text);
}

QString AppFileDialog::currentLocationPath() const
{
    return m_currentDir;
}

void AppFileDialog::acceptResult()
{
    if (m_browseMode == BrowseMode::Network) {
        const QString url = m_netEdit->text().trimmed();
        if (url.isEmpty())
            return;
        if (m_mode == OpenFileNames) {
            m_resultList.clear();
            m_resultList << url;
        } else {
            m_result = url;
        }
        accept();
        return;
    }
    if (m_browseMode == BrowseMode::Resource) {
        if (m_resVersion.downloadUrl.isEmpty())
            return;
        buildResSelected();
        if (m_mode == OpenFileNames) {
            m_resultList.clear();
            m_resultList << m_resSelected.downloadUrl;
        } else {
            m_result = m_resSelected.downloadUrl;
        }
        accept();
        return;
    }
    switch (m_mode) {
    case ExistingDirectory: {
        // 单选文件夹时以所选目录为准（与系统目录选择器一致），
        // 否则回退到当前浏览目录（兼容双击进入后直接确认的用法）
        const QStringList sel = selectedPaths();
        if (sel.size() == 1 && QFileInfo(sel.first()).isDir())
            m_result = sel.first();
        else
            m_result = m_currentDir;
        accept();
        return;
    }
    case OpenFile:
    case SaveFile: {
        QString filePath = selectedFilePath();
        if (filePath.isEmpty())
            filePath = pathEditText();
        if (filePath.isEmpty())
            return;
        if (m_mode == OpenFile && !QFileInfo(filePath).isFile())
            return;
        m_result = filePath;
        accept();
        return;
    }
    case OpenFileNames: {
        QStringList paths;
        const QStringList sel = selectedPaths();
        for (const QString &p : sel) {
            if (!QFileInfo(p).isDir())
                paths << p;
        }
        if (paths.isEmpty()) {
            const QString single = pathEditText();
            if (QFileInfo(single).isFile())
                paths << single;
        }
        if (paths.isEmpty())
            return;
        m_resultList = paths;
        accept();
        return;
    }
    }
}

void AppFileDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString themeHover = tm->getThemeColorHover();
    const QString themeLight = tm->getThemeColorLight();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg     = isLight ? "rgba(255, 255, 255, 248)" : "rgba(44, 44, 48, 248)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 230)" : "rgba(90, 90, 96, 230)";
    const QString titleColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor  = isLight ? "#333333" : "#e8e8e8";
    const QString subColor   = isLight ? "#64748B" : "#9aa3b2";
    const QString muted      = isLight ? "#94A3B8" : "#7a8290";
    const QString fieldBg    = isLight ? "#f4f4f4" : "#3b3b40";
    const QString fieldHover = isLight ? "#ececec" : "#414146";
    const QString fieldBorder= isLight ? "#d4d4d4" : "#55555a";
    const QString hoverBg    = isLight ? "rgba(15,23,42,0.06)" : "rgba(255,255,255,0.06)";
    const QString selBg      = isLight ? "rgba(16,185,129,0.14)" : "rgba(16,185,129,0.20)";
    const QString listBg     = isLight ? "#fafafa" : "#38383d";
    const QString btnText    = isLight ? "#333333" : "#e8e8e8";
    const QString navBg      = isLight ? "rgba(255,255,255,0.55)" : "rgba(255,255,255,0.04)";

    const QString style = QString(
        // ---- 卡片 ----
        "QWidget#appDialogCard {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 18px;"
        "}"
        // ---- 标题栏 ----
        "QLabel#fdTitle { color: %3; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#fdSubtitle { color: %6; font-size: 12px; background: transparent; }"
        "QLabel#fdHeaderIcon { background: %4; border-radius: 9px; }"
        // ---- 模式按钮 ----
        "QPushButton#fdModeBtn {"
        "    background-color: %7; border: 1px solid %9; border-radius: 9px;"
        "    color: %5; padding: 7px 16px; font-size: 12.5px; font-weight: 600;"
        "}"
        "QPushButton#fdModeBtn:hover { background-color: %10; }"
        "QPushButton#fdModeBtn:checked {"
        "    background-color: %4; border-color: %4; color: #ffffff;"
        "}"
        "QToolButton#fdCloseBtn { background: transparent; border: none; border-radius: 8px; color: %5; }"
        "QToolButton#fdCloseBtn:hover { background-color: rgba(239,68,68,0.15); }"
        // ---- 工具栏 ----
        "QToolButton#fdNavBtn, QToolButton#fdViewBtn, QToolButton#fdAddrBtn {"
        "    background-color: transparent; border: 1px solid transparent; border-radius: 8px; padding: 3px;"
        "}"
        "QToolButton#fdNavBtn:hover, QToolButton#fdViewBtn:hover, QToolButton#fdAddrBtn:hover { background-color: %10; }"
        "QToolButton#fdViewBtn:checked { background-color: %4; border-color: %4; }"
        "QToolButton#fdNavBtn:disabled { opacity: 0.4; }"
        "QFrame#fdCrumbBar { background-color: %7; border: 1px solid %9; border-radius: 9px; }"
        "QLineEdit#fdAddrEdit {"
        "    background-color: %7; border: 1px solid %4; border-radius: 9px;"
        "    color: %5; padding: 5px 10px; font-size: 12px; font-family: 'Consolas','Cascadia Code';"
        "}"
        "QLineEdit#fdAddrEdit:focus { border-color: %4; background-color: %11; }"
        "QPushButton#fdCrumb, QPushButton#fdCrumbLink {"
        "    background: transparent; border: none; padding: 4px 8px; border-radius: 6px;"
        "    color: %6; font-size: 12px; text-align: left;"
        "}"
        "QPushButton#fdCrumbLink:hover { background-color: %10; color: %3; }"
        "QPushButton#fdCrumb { color: %3; font-weight: 600; }"
        "QLabel#fdCrumbSep { color: %8; font-size: 11px; }"
        "QLineEdit#fdSearch, QComboBox#fdSortCombo {"
        "    background-color: %7; border: 1px solid %9; border-radius: 8px;"
        "    color: %5; padding: 5px 10px; font-size: 12px;"
        "}"
        "QLineEdit#fdSearch:focus, QComboBox#fdSortCombo:focus {"
        "    border-color: %4; background-color: %11;"
        "}"
        "QComboBox#fdSortCombo::drop-down { border: none; width: 20px; }"
        // ---- 筛选 chips ----
        "QWidget#fdChipBar { background: transparent; }"
        "QLabel#fdBarHint { color: %8; font-size: 11px; }"
        "QPushButton#fdChip {"
        "    background-color: %7; border: 1px solid %9; border-radius: 999px;"
        "    color: %5; padding: 3px 12px; font-size: 12px; font-weight: 500;"
        "}"
        "QPushButton#fdChip:hover { border-color: %12; color: %3; }"
        "QPushButton#fdChip:checked {"
        "    background-color: %4; border-color: %4; color: #ffffff;"
        "}"
        // ---- 步骤条 ----
        "QFrame#fdStepperBar { background: transparent; }"
        "QLabel#fdStepArrow { color: %8; font-size: 12px; }"
        "QPushButton#fdStep {"
        "    background-color: %7; border: 1px solid %9; border-radius: 999px;"
        "    color: %8; padding: 5px 14px; font-size: 12px;"
        "}"
        "QPushButton#fdStep[state=\"done\"] {"
        "    background-color: %4; border-color: %4; color: #ffffff; font-weight: 600;"
        "}"
        "QPushButton#fdStep[state=\"current\"] {"
        "    background-color: %4; border-color: %4; color: #ffffff; font-weight: 700;"
        "}"
        "QPushButton#fdStep[state=\"todo\"] { color: %8; }"
        // ---- 侧边栏 ----
        "QListWidget#fdSidebar {"
        "    background-color: %13; border: 1px solid %9; border-radius: 10px;"
        "    color: %5; font-size: 12px; padding: 6px; outline: none;"
        "}"
        "QListWidget#fdSidebar::item { border-radius: 6px; padding: 4px 8px; }"
        "QListWidget#fdSidebar::item:hover { background-color: %10; }"
        "QListWidget#fdSidebar::item:selected { background-color: %15; color: %3; }"
        // ---- 列表 ----
        "QTreeWidget#fdListTree {"
        "    background-color: %14; border: 1px solid %9; border-radius: 10px;"
        "    color: %5; font-size: 12.5px; outline: none;"
        "}"
        "QTreeWidget#fdListTree::item { padding: 4px 2px; }"
        "QTreeWidget#fdListTree::item:hover { background-color: %10; }"
        "QTreeWidget#fdListTree::item:selected { background-color: %15; color: %3; }"
        "QHeaderView::section {"
        "    background-color: %13; border: none; border-bottom: 1px solid %9;"
        "    color: %8; font-size: 11px; padding: 5px 4px;"
        "}"
        "QListWidget#fdGridList {"
        "    background-color: %14; border: 1px solid %9; border-radius: 10px;"
        "    color: %5; font-size: 11px; outline: none;"
        "}"
        "QListWidget#fdGridList::item { border-radius: 8px; padding: 4px; }"
        "QListWidget#fdGridList::item:hover { background-color: %10; }"
        "QListWidget#fdGridList::item:selected { background-color: %15; color: %3; }"
        // ---- 预览 ----
        "QFrame#fdPreview { background-color: %13; border: 1px solid %9; border-radius: 10px; }"
        "QLabel#fdPvIcon { border-radius: 10px; background: transparent; }"
        "QLabel#fdPvName { color: %3; font-size: 13px; font-weight: 700; }"
        "QLabel#fdPvType { color: %6; font-size: 12px; }"
        "QLabel#fdPvRows { color: %6; font-size: 11.5px; }"
        "QLabel#fdPvHint { color: %8; font-size: 11px; }"
        "QLabel#fdPvEmpty { color: %8; font-size: 12px; }"
        // ---- 实例式页面 ----
        "QFrame#fdInstPage { background-color: %13; border: 1px solid %9; border-radius: 14px; }"
        "QLabel#fdStepBadge {"
        "    background-color: %4; color: #ffffff; border-radius: 12px;"
        "    font-size: 12px; font-weight: 700; padding: 6px 12px;"
        "}"
        "QLabel#fdInstTitle { color: %3; font-size: 18px; font-weight: 800; }"
        "QLabel#fdInstDesc { color: %6; font-size: 12px; }"
        "QPushButton#fdBackBtn {"
        "    background-color: %7; border: 1px solid %9; border-radius: 999px;"
        "    color: %5; font-size: 12px; font-weight: 600; padding: 6px 14px;"
        "}"
        "QPushButton#fdBackBtn:hover { background-color: %10; border-color: %12; }"
        // ---- 版本卡片 ----
        "QFrame#fdEditionCard {"
        "    background-color: %11; border: 1px solid %9; border-radius: 18px;"
        "}"
        "QFrame#fdEditionCard:hover { border-color: %4; background-color: %17; }"
        "QLabel#fdEditionIco {"
        "    background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %4, stop:1 %4);"
        "    color: #ffffff; border-radius: 18px; font-size: 24px; font-weight: 800;"
        "}"
        "QFrame#fdEditionCard[java=\"false\"] QLabel#fdEditionIco {"
        "    background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #22d3ee, stop:1 #0e7490);"
        "}"
        "QLabel#fdEditionName { color: %3; font-size: 18px; font-weight: 800; }"
        "QLabel#fdEditionDesc { color: %6; font-size: 12.5px; line-height: 150%; }"
        "QLabel#fdEditionNext {"
        "    background-color: %4; color: #ffffff; border-radius: 999px;"
        "    font-size: 12.5px; font-weight: 600; padding: 6px 16px;"
        "}"
        "QFrame#fdEditionCard[java=\"false\"] QLabel#fdEditionNext { background-color: #06b6d4; }"
        // ---- 实例卡片 ----
        "QListWidget#fdInstanceList { background: transparent; border: none; outline: none; }"
        "QFrame#fdInstCard {"
        "    background-color: %11; border: 1px solid %9; border-radius: 12px;"
        "}"
        "QFrame#fdInstCard:hover { border-color: %12; background-color: %17; }"
        "QLabel#fdInstAvatar { background-color: %4; color: #ffffff; border-radius: 12px; font-size: 18px; font-weight: 800; }"
        "QLabel#fdInstName { color: %3; font-size: 14px; font-weight: 700; }"
        "QLabel#fdInstMeta { color: %6; font-size: 11.5px; font-family: 'Consolas','Cascadia Code'; }"
        "QLabel#fdInstTime { color: %8; font-size: 11px; }"
        "QLabel#fdInstCatChip {"
        "    background-color: %16; color: %4; border: 1px solid rgba(16,185,129,0.3);"
        "    border-radius: 999px; font-size: 10.5px; padding: 1px 8px;"
        "}"
        "QLabel#fdInstArrow { color: %8; font-size: 18px; font-weight: 700; }"
        "QFrame#fdInstCard:hover QLabel#fdInstArrow { color: %4; }"
        // ---- 资源类型 ----
        "QListWidget#fdTypeList { background: transparent; border: none; outline: none; }"
        "QListWidget#fdTypeList::item {"
        "    background-color: %11; border: 1px solid %9; border-radius: 12px;"
        "    color: %3; font-size: 12.5px; padding: 8px;"
        "}"
        "QListWidget#fdTypeList::item:hover { border-color: %4; background-color: %17; }"
        // ---- 网络链接页面 ----
        "QWidget#fdNetPage { background-color: %13; border: 1px solid %9; border-radius: 14px; }"
        "QLabel#fdNetIcon { background-color: %4; color: #ffffff; border-radius: 18px; }"
        "QLabel#fdNetTitle { color: %3; font-size: 18px; font-weight: 800; }"
        "QLabel#fdNetDesc { color: %6; font-size: 12.5px; }"
        "QLineEdit#fdNetEdit {"
        "    background-color: %7; border: 1px solid %9; border-radius: 10px;"
        "    color: %5; padding: 10px 14px; font-size: 13px;"
        "    font-family: 'Consolas','Cascadia Code';"
        "}"
        "QLineEdit#fdNetEdit:focus { border-color: %4; background-color: %11; }"
        "QLabel#fdNetStatus { font-size: 12.5px; font-weight: 600; background: transparent; }"
        "QLabel#fdNetStatus[state=\"good\"] { color: %4; }"
        "QLabel#fdNetStatus[state=\"bad\"] { color: #ef4444; }"
        "QLabel#fdNetTips { color: %8; font-size: 11.5px; }"
        "QPushButton#fdNetChip {"
        "    background-color: %7; border: 1px solid %9; border-radius: 999px;"
        "    color: %5; padding: 4px 14px; font-size: 12px;"
        "}"
        "QPushButton#fdNetChip:hover { border-color: %12; color: %3; }"
        // ---- 资源模式 ----
        "QWidget#fdResPage { background-color: %13; border: 1px solid %9; border-radius: 14px; }"
        "QLabel#fdResPageTitle { color: %3; font-size: 16px; font-weight: 800; }"
        "QLabel#fdResPageDesc { color: %6; font-size: 12px; }"
        "QListWidget#fdEditionList, QListWidget#fdResTypeList {"
        "    background: transparent; border: none; outline: none;"
        "}"
        "QFrame#fdResCard {"
        "    background-color: %11; border: 1px solid %9; border-radius: 12px;"
        "}"
        "QFrame#fdResCard:hover { border-color: %4; background-color: %17; }"
        "QLabel#fdResCardIco { background: transparent; }"
        "QLabel#fdResCardName { color: %3; font-size: 13.5px; font-weight: 700; }"
        "QLabel#fdResCardDesc { color: %6; font-size: 11.5px; }"
        "QLabel#fdResStatus { font-size: 12.5px; font-weight: 600; background: transparent; }"
        "QLabel#fdResStatus[state=\"info\"] { color: %6; }"
        "QLabel#fdResStatus[state=\"bad\"] { color: #ef4444; }"
        "QLabel#fdResStatus[state=\"good\"] { color: %4; }"
        "QListWidget#fdResResultList, QListWidget#fdResVersionList {"
        "    background-color: %14; border: 1px solid %9; border-radius: 10px; outline: none;"
        "}"
        "QFrame#fdResResultCard {"
        "    background-color: %11; border: 1px solid %9; border-radius: 10px;"
        "}"
        "QFrame#fdResResultCard:hover { border-color: %4; background-color: %17; }"
        "QFrame#fdResVersionCard {"
        "    background-color: %11; border: 1px solid %9; border-radius: 10px;"
        "}"
        "QFrame#fdResVersionCard:hover { border-color: %4; background-color: %17; }"
        "QFrame#fdResVersionCard[selected=\"true\"] {"
        "    border-color: %4; background-color: %15;"
        "}"
        "QLabel#fdResVerName { color: %3; font-size: 13px; font-weight: 700; }"
        "QLabel#fdResVersionHeader {"
        "    color: %3; font-size: 13px; font-weight: 700;"
        "    background-color: %13; border: 1px solid %9; border-radius: 10px;"
        "    padding: 10px 14px;"
        "}"
        // ---- 底部 ----
        "QWidget#fdFooter { background: transparent; }"
        "QLabel#fdSelBadge {"
        "    background-color: %16; border: 1px solid rgba(16,185,129,0.35); border-radius: 999px;"
        "    color: %3; font-size: 12px; font-weight: 600; padding: 3px 12px;"
        "}"
        "QLabel#fdStatus { color: %8; font-size: 12px; }"
        "QLabel#fdFileNameLabel { color: %6; font-size: 12.5px; }"
        "QLineEdit#fdFileName {"
        "    background-color: %7; border: 1px solid %9; border-radius: 9px;"
        "    color: %5; padding: 6px 12px; font-size: 13px;"
        "}"
        "QLineEdit#fdFileName:focus { border-color: %4; background-color: %11; }"
        "QPushButton#fdBtn {"
        "    background-color: %7; border: 1px solid %9; border-radius: 10px;"
        "    color: %5; padding: 7px 22px; font-size: 13px;"
        "}"
        "QPushButton#fdBtn:hover { background-color: %10; border-color: %12; color: %3; }"
        "QPushButton#fdBtnPrimary {"
        "    background-color: %4; border: 1px solid %4; border-radius: 10px;"
        "    color: #ffffff; padding: 7px 26px; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton#fdBtnPrimary:hover { background-color: %18; border-color: %18; }"
        "QPushButton#fdBtnPrimary:disabled { background-color: %8; border-color: %8; color: rgba(255,255,255,0.8); }"
        // ---- 滚动条 ----
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
        "QScrollBar::handle:vertical { background: %8; border-radius: 4px; min-height: 30px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: %8; border-radius: 4px; min-width: 30px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"
        "QListWidget#fdSidebar QScrollBar::handle:vertical { background: rgba(148,163,184,0.35); }"
        "QListWidget#fdInstanceList, QListWidget#fdTypeList, QScrollArea { background: transparent; }"
        "QMenu#fdCtxMenu {"
        "    background-color: %1; border: 1px solid %2; border-radius: 10px;"
        "    padding: 5px;"
        "}"
        "QMenu#fdCtxMenu::item {"
        "    background: transparent; color: %3; padding: 7px 22px 7px 12px; border-radius: 6px;"
        "    font-size: 12.5px;"
        "}"
        "QMenu#fdCtxMenu::item:selected { background-color: %15; color: %3; }"
        "QMenu#fdCtxMenu::separator { height: 1px; background: %9; margin: 4px 8px; }"
        "QToolTip { background-color: %3; color: %1; border: none; padding: 4px 8px; border-radius: 6px; }"
    ).arg(cardBg, cardBorder, titleColor, themeColor, btnText, subColor, fieldBg,
           muted, fieldBorder, hoverBg, fieldHover, fieldHover, navBg, listBg, selBg,
           themeLight, fieldBg, themeHover);

    setStyleSheet(style);
}

/* ============================ 静态接口 ============================ */

QString AppFileDialog::getOpenFileName(QWidget *parent, const QString &caption, const QString &dir,
                                       const QString &filter, QString *selectedFilter, int options)
{
    AppFileDialog dlg(parent, OpenFile);
    dlg.setWindowTitle(caption);
    dlg.m_options = options;
    dlg.applyFilter(filter);
    if (!dir.isEmpty())
        dlg.goTo(dir);
    else
        dlg.goTo(QDir::homePath());
    if (dlg.exec() == Accepted) {
        if (selectedFilter)
            *selectedFilter = dlg.currentFilterName();
        return dlg.m_result;
    }
    return QString();
}

QString AppFileDialog::getSaveFileName(QWidget *parent, const QString &caption, const QString &dir,
                                       const QString &filter, QString *selectedFilter, int options)
{
    AppFileDialog dlg(parent, SaveFile);
    dlg.setWindowTitle(caption);
    dlg.m_options = options;
    dlg.applyFilter(filter);
    if (dir.isEmpty()) {
        dlg.goTo(QDir::homePath());
    } else {
        dlg.goTo(QFileInfo(dir).absolutePath());
        dlg.m_fileNameEdit->setText(QFileInfo(dir).fileName());
    }
    if (dlg.exec() == Accepted) {
        if (selectedFilter)
            *selectedFilter = dlg.currentFilterName();
        return dlg.m_result;
    }
    return QString();
}

QStringList AppFileDialog::getOpenFileNames(QWidget *parent, const QString &caption, const QString &dir,
                                            const QString &filter, QString *selectedFilter, int options)
{
    AppFileDialog dlg(parent, OpenFileNames);
    dlg.setWindowTitle(caption);
    dlg.m_options = options;
    dlg.applyFilter(filter);
    if (!dir.isEmpty())
        dlg.goTo(dir);
    else
        dlg.goTo(QDir::homePath());
    if (dlg.exec() == Accepted) {
        if (selectedFilter)
            *selectedFilter = dlg.currentFilterName();
        return dlg.m_resultList;
    }
    return QStringList();
}

QString AppFileDialog::getExistingDirectory(QWidget *parent, const QString &caption, const QString &dir,
                                            int options)
{
    AppFileDialog dlg(parent, ExistingDirectory);
    dlg.setWindowTitle(caption);
    dlg.m_options = options;
    if (!dir.isEmpty())
        dlg.goTo(dir);
    else
        dlg.goTo(QDir::homePath());
    if (dlg.exec() == Accepted)
        return dlg.m_result;
    return QString();
}

QString AppFileDialog::getResource(QWidget *parent, const QString &caption, SelectedResource *out)
{
    AppFileDialog dlg(parent, OpenFile);
    dlg.setWindowTitle(caption);
    dlg.goTo(QDir::homePath());
    if (dlg.exec() == Accepted) {
        if (out)
            *out = dlg.m_resSelected;
        return dlg.m_result;
    }
    if (out)
        *out = SelectedResource();
    return QString();
}
