#include "OnlineResourceDialog.h"

#include "platform.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/TouchScroll.h"
#include "utils/UiMetrics.h"
#include "utils/mod/CurseForgeAPI.h"
#include "utils/mod/ModrinthAPI.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtGlobal>

#include <functional>

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

} // namespace

OnlineResourceDialog::OnlineResourceDialog(QWidget *parent)
    : AppDialogBase(parent)
{
    m_mobileMode = Platform::isMobile();
    setProperty("mobileMode", m_mobileMode);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    // 移动端充分利用竖屏空间（更高），桌面端 660x560
    card->setMinimumSize(UiMetrics::fitToScreen(this, m_mobileMode ? QSize(600, 900)
                                                                   : QSize(660, 560)));

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(m_mobileMode ? 12 : 24,
                                   m_mobileMode ? 10 : 18,
                                   m_mobileMode ? 12 : 24,
                                   m_mobileMode ? 10 : 18);
    cardLayout->setSpacing(10);

    // 顶栏：标题 + 关闭按钮
    QWidget *header = new QWidget(this);
    header->setObjectName(QStringLiteral("fdHeader"));
    QHBoxLayout *row = new QHBoxLayout(header);
    row->setContentsMargins(2, 2, 2, 2);
    row->setSpacing(10);

    QLabel *icon = new QLabel(header);
    icon->setObjectName(QStringLiteral("fdHeaderIcon"));
    icon->setFixedSize(32, 32);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(style()->standardIcon(QStyle::SP_DriveNetIcon).pixmap(18, 18));
    icon->setAttribute(Qt::WA_StyledBackground, true);
    row->addWidget(icon);

    QWidget *txt = new QWidget(header);
    QVBoxLayout *vl = new QVBoxLayout(txt);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    m_titleLabel = new QLabel(tr("选择资源"), txt);
    m_titleLabel->setObjectName(QStringLiteral("fdTitle"));
    m_subtitleLabel = new QLabel(tr("从 CurseForge / Modrinth 获取 Java 版或基岩版资源"), txt);
    m_subtitleLabel->setObjectName(QStringLiteral("fdSubtitle"));
    if (m_mobileMode)
        m_subtitleLabel->hide();   // 竖屏省高度
    vl->addWidget(m_titleLabel);
    vl->addWidget(m_subtitleLabel);
    row->addWidget(txt, 1);

    m_closeBtn = new QToolButton(header);
    m_closeBtn->setObjectName(QStringLiteral("fdCloseBtn"));
    m_closeBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    m_closeBtn->setAutoRaise(true);
    m_closeBtn->setToolTip(tr("关闭 (Esc)"));
    row->addWidget(m_closeBtn);

    cardLayout->addWidget(header);

    buildResStepper();
    cardLayout->addWidget(m_resStepper);

    cardLayout->addWidget(buildResPages(), 1);

    cardLayout->addWidget(buildFooter());

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(card, 0, 0);

    setupResApis();
    setResLevel(ResLevel::Edition);

    connect(m_closeBtn, &QToolButton::clicked, this, &QDialog::reject);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_okBtn, &QPushButton::clicked, this, &OnlineResourceDialog::acceptResult);

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

    // 移动端触屏适配：各列表支持手指拖动滚动（桌面端无变化）
    TouchScroll::enableDragScroll(m_resEditionList);
    TouchScroll::enableDragScroll(m_resTypeList);
    TouchScroll::enableDragScroll(m_resResultList);
    TouchScroll::enableDragScroll(m_resVersionList);

    initStyle();
}

void OnlineResourceDialog::buildResStepper()
{
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
            if (i < static_cast<int>(m_resLevel))
                setResLevel(static_cast<ResLevel>(i));
        });
    }
    m_resStepperLayout->addStretch();
}

QWidget *OnlineResourceDialog::buildFooter()
{
    QWidget *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("fdFooter"));

    // 移动端按钮整行铺满（更大点击区）；桌面端单行
    QHBoxLayout *row = new QHBoxLayout(footer);
    row->setContentsMargins(2, 4, 2, 0);
    row->setSpacing(8);

    m_statusLabel = new QLabel(footer);
    m_statusLabel->setObjectName(QStringLiteral("fdStatus"));
    m_statusLabel->hide();
    row->addWidget(m_statusLabel);

    m_cancelBtn = new QPushButton(tr("取消"), footer);
    m_cancelBtn->setObjectName(QStringLiteral("fdBtn"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);

    m_okBtn = new QPushButton(tr("打开"), footer);
    m_okBtn->setObjectName(QStringLiteral("fdBtnPrimary"));
    m_okBtn->setCursor(Qt::PointingHandCursor);
    m_okBtn->setDefault(true);

    if (m_mobileMode) {
        QVBoxLayout *col = new QVBoxLayout(footer);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(6);
        col->addLayout(row);
        QHBoxLayout *btnRow = new QHBoxLayout;
        btnRow->setContentsMargins(2, 0, 2, 0);
        btnRow->setSpacing(8);
        btnRow->addWidget(m_cancelBtn, 1);
        btnRow->addWidget(m_okBtn, 1);
        col->addLayout(btnRow);
    } else {
        row->addWidget(m_cancelBtn);
        row->addWidget(m_okBtn);
    }

    return footer;
}

/* ============================ 资源模式页面 ============================ */

QWidget *OnlineResourceDialog::buildResPages()
{
    m_resStack = new QStackedWidget(this);
    m_resStack->setObjectName(QStringLiteral("fdResStack"));
    m_resStack->addWidget(buildResEditionPage());
    m_resStack->addWidget(buildResTypePage());
    m_resStack->addWidget(buildResSearchPage());
    m_resStack->addWidget(buildResVersionPage());
    return m_resStack;
}

QWidget *OnlineResourceDialog::buildResEditionPage()
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

QWidget *OnlineResourceDialog::buildResTypePage()
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

QWidget *OnlineResourceDialog::buildResSearchPage()
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

QWidget *OnlineResourceDialog::buildResVersionPage()
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

/* ============================ 资源模式逻辑 ============================ */

void OnlineResourceDialog::setupResApis()
{
    if (m_cfApi || m_mrApi)
        return;
    m_cfApi = new CurseForgeAPI(this);
    const QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (!apiKey.isEmpty())
        m_cfApi->setApiKey(apiKey);
    m_mrApi = new ModrinthAPI(this);
    applyMcimToResApis();

    connect(m_cfApi, &CurseForgeAPI::searchCompleted, this, &OnlineResourceDialog::onResSearchCompleted);
    connect(m_cfApi, &CurseForgeAPI::searchFailed, this, &OnlineResourceDialog::onResSearchFailed);
    connect(m_cfApi, &CurseForgeAPI::modDetailReceived, this, &OnlineResourceDialog::onResDetailReceived);
    connect(m_cfApi, &CurseForgeAPI::modDetailFailed, this, &OnlineResourceDialog::onResDetailFailed);
    connect(m_mrApi, &ModrinthAPI::searchCompleted, this, &OnlineResourceDialog::onResSearchCompleted);
    connect(m_mrApi, &ModrinthAPI::searchFailed, this, &OnlineResourceDialog::onResSearchFailed);
    connect(m_mrApi, &ModrinthAPI::modDetailReceived, this, &OnlineResourceDialog::onResDetailReceived);
    connect(m_mrApi, &ModrinthAPI::modDetailFailed, this, &OnlineResourceDialog::onResDetailFailed);
}

void OnlineResourceDialog::applyMcimToResApis()
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

void OnlineResourceDialog::setResLevel(ResLevel lv)
{
    m_resLevel = lv;
    renderResStepper();
    renderResPages();
    updateFooterState();
}

void OnlineResourceDialog::renderResStepper()
{
    const int cur = static_cast<int>(m_resLevel);
    const int count = m_resStepperLayout->count();
    for (int i = 0; i < count; ++i) {
        QWidget *w = m_resStepperLayout->itemAt(i)->widget();
        if (auto *btn = qobject_cast<QPushButton *>(w)) {
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

void OnlineResourceDialog::renderResPages()
{
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

void OnlineResourceDialog::resChooseEditionPlatform(const QString &edition, const QString &platform)
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

void OnlineResourceDialog::populateResTypes()
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
            // Modrinth 没有「世界存档」分类，这里留空；选了 Modrinth 时这一项不会出现
            { tr("世界"),   QStringLiteral("17"),    QString(),                     QColor("#8B5CF6") },
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
        // Modrinth 没有的分类（例如世界存档）在 Modrinth 下载源下直接不显示，
        // 避免点进去搜出一堆模组
        if (m_resPlatform == QStringLiteral("modrinth") && d.mr.isEmpty())
            continue;
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

void OnlineResourceDialog::performResSearch(int page)
{
    if (m_resLoading)
        return;
    if (m_resPlatform == QStringLiteral("modrinth") && m_resTypeMr.isEmpty()) {
        // Modrinth 没有这个分类，别把无关内容当成搜索结果
        m_resLoading = false;
        m_resStatusLabel->setText(
            tr("Modrinth 没有「%1」这个分类，请返回上一步把下载源换成 CurseForge").arg(m_resTypeLabel));
        m_resStatusLabel->setProperty("state", QStringLiteral("bad"));
        m_resStatusLabel->style()->unpolish(m_resStatusLabel);
        m_resStatusLabel->style()->polish(m_resStatusLabel);
        m_resStatusLabel->show();
        return;
    }
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

void OnlineResourceDialog::onResSearchCompleted(const ModSearchResult &result)
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

void OnlineResourceDialog::onResSearchFailed(const QString &error)
{
    m_resLoading = false;
    m_resStatusLabel->setText(tr("搜索失败：%1").arg(error));
    m_resStatusLabel->setProperty("state", QStringLiteral("bad"));
    m_resStatusLabel->style()->unpolish(m_resStatusLabel);
    m_resStatusLabel->style()->polish(m_resStatusLabel);
    m_resStatusLabel->show();
}

void OnlineResourceDialog::openResVersions(int idx)
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

void OnlineResourceDialog::onResDetailReceived(const ModInfo &detail)
{
    m_resInfo = detail;
    m_resStatusLabel->hide();
    populateResVersionList();
    setResLevel(ResLevel::Version);
}

void OnlineResourceDialog::onResDetailFailed(const QString &error)
{
    m_resStatusLabel->setText(tr("加载版本列表失败：%1").arg(error));
    m_resStatusLabel->setProperty("state", QStringLiteral("bad"));
    m_resStatusLabel->style()->unpolish(m_resStatusLabel);
    m_resStatusLabel->style()->polish(m_resStatusLabel);
    m_resStatusLabel->show();
}

void OnlineResourceDialog::populateResVersionList()
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

void OnlineResourceDialog::buildResSelected()
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

/* ============================ 通用 ============================ */

void OnlineResourceDialog::updateFooterState()
{
    m_okBtn->setEnabled(!m_resVersion.downloadUrl.isEmpty());
}

void OnlineResourceDialog::acceptResult()
{
    if (m_resVersion.downloadUrl.isEmpty())
        return;
    buildResSelected();
    m_result = m_resSelected.downloadUrl;
    accept();
}

QPixmap OnlineResourceDialog::makeTileIcon(const QString &letter, const QColor &color, int size)
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

void OnlineResourceDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString themeHover = tm->getThemeColorHover();
    const QString themeLight = tm->getThemeColorLight();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg     = isLight ? "rgba(255, 255, 255, 248)" : "rgba(44, 44, 48, 248)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 230)" : "rgba(90, 90, 96, 230)";
    const QString titleColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString btnText    = isLight ? "#333333" : "#e8e8e8";
    const QString subColor   = isLight ? "#64748B" : "#9aa3b2";
    const QString muted      = isLight ? "#94A3B8" : "#7a8290";
    const QString fieldBg    = isLight ? "#f4f4f4" : "#3b3b40";
    const QString fieldHover = isLight ? "#ececec" : "#414146";
    const QString fieldBorder= isLight ? "#d4d4d4" : "#55555a";
    const QString hoverBg    = isLight ? "rgba(15,23,42,0.06)" : "rgba(255,255,255,0.06)";
    const QString selBg      = isLight ? "rgba(16,185,129,0.14)" : "rgba(16,185,129,0.20)";
    const QString listBg     = isLight ? "#fafafa" : "#38383d";

    const QString style = QString(
        // ---- 卡片 ----
        "QWidget#appDialogCard {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 18px;"
        "}"
        // ---- 顶栏 ----
        "QLabel#fdTitle { color: %3; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#fdSubtitle { color: %5; font-size: 12px; background: transparent; }"
        "QLabel#fdHeaderIcon { background: %4; border-radius: 9px; }"
        "QToolButton#fdCloseBtn { background: transparent; border: none; border-radius: 8px; color: %6; }"
        "QToolButton#fdCloseBtn:hover { background-color: rgba(239,68,68,0.15); }"
        // ---- 步骤条 ----
        "QFrame#fdStepperBar { background: transparent; }"
        "QLabel#fdStepArrow { color: %7; font-size: 12px; }"
        "QPushButton#fdStep {"
        "    background-color: %8; border: 1px solid %9; border-radius: 999px;"
        "    color: %7; padding: 5px 14px; font-size: 12px;"
        "}"
        "QPushButton#fdStep[state=\"done\"] {"
        "    background-color: %4; border-color: %4; color: #ffffff; font-weight: 600;"
        "}"
        "QPushButton#fdStep[state=\"current\"] {"
        "    background-color: %4; border-color: %4; color: #ffffff; font-weight: 700;"
        "}"
        "QPushButton#fdStep[state=\"todo\"] { color: %7; }"
        // ---- 资源页面 ----
        "QWidget#fdResPage { background-color: %10; border: 1px solid %9; border-radius: 14px; }"
        "QLabel#fdResPageTitle { color: %3; font-size: 16px; font-weight: 800; }"
        "QLabel#fdResPageDesc { color: %5; font-size: 12px; }"
        "QListWidget#fdEditionList, QListWidget#fdResTypeList {"
        "    background: transparent; border: none; outline: none;"
        "}"
        "QFrame#fdResCard {"
        "    background-color: %11; border: 1px solid %9; border-radius: 12px;"
        "}"
        "QFrame#fdResCard:hover { border-color: %4; background-color: %12; }"
        "QLabel#fdResCardIco { background: transparent; }"
        "QLabel#fdResCardName { color: %3; font-size: 13.5px; font-weight: 700; }"
        "QLabel#fdResCardDesc { color: %5; font-size: 11.5px; }"
        "QLabel#fdResStatus { font-size: 12.5px; font-weight: 600; background: transparent; }"
        "QLabel#fdResStatus[state=\"info\"] { color: %5; }"
        "QLabel#fdResStatus[state=\"bad\"] { color: #ef4444; }"
        "QLabel#fdResStatus[state=\"good\"] { color: %4; }"
        "QLabel#fdBarHint { color: %7; font-size: 11px; }"
        "QListWidget#fdResResultList, QListWidget#fdResVersionList {"
        "    background-color: %13; border: 1px solid %9; border-radius: 10px; outline: none;"
        "}"
        "QFrame#fdResResultCard {"
        "    background-color: %11; border: 1px solid %9; border-radius: 10px;"
        "}"
        "QFrame#fdResResultCard:hover { border-color: %4; background-color: %12; }"
        "QFrame#fdResVersionCard {"
        "    background-color: %11; border: 1px solid %9; border-radius: 10px;"
        "}"
        "QFrame#fdResVersionCard:hover { border-color: %4; background-color: %12; }"
        "QFrame#fdResVersionCard[selected=\"true\"] {"
        "    border-color: %4; background-color: %14;"
        "}"
        "QLabel#fdResVerName { color: %3; font-size: 13px; font-weight: 700; }"
        "QLabel#fdResVersionHeader {"
        "    color: %3; font-size: 13px; font-weight: 700;"
        "    background-color: %10; border: 1px solid %9; border-radius: 10px;"
        "    padding: 10px 14px;"
        "}"
        "QLabel#fdInstArrow { color: %7; font-size: 18px; font-weight: 700; }"
        "QFrame#fdResCard:hover QLabel#fdInstArrow, QFrame#fdResResultCard:hover QLabel#fdInstArrow { color: %4; }"
        "QLabel#fdInstMeta { color: %5; font-size: 11.5px; font-family: 'Consolas','Cascadia Code'; }"
        "QLabel#fdInstTime { color: %7; font-size: 11px; }"
        // ---- 搜索框 ----
        "QLineEdit#fdNetEdit {"
        "    background-color: %8; border: 1px solid %9; border-radius: 10px;"
        "    color: %6; padding: 10px 14px; font-size: 13px;"
        "    font-family: 'Consolas','Cascadia Code';"
        "}"
        "QLineEdit#fdNetEdit:focus { border-color: %4; background-color: %12; }"
        // ---- 底部 ----
        "QWidget#fdFooter { background: transparent; }"
        "QLabel#fdStatus { color: %7; font-size: 12px; }"
        "QPushButton#fdBtn {"
        "    background-color: %8; border: 1px solid %9; border-radius: 10px;"
        "    color: %6; padding: 7px 22px; font-size: 13px;"
        "}"
        "QPushButton#fdBtn:hover { background-color: %12; border-color: %15; color: %3; }"
        "QPushButton#fdBtnPrimary {"
        "    background-color: %4; border: 1px solid %4; border-radius: 10px;"
        "    color: #ffffff; padding: 7px 26px; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton#fdBtnPrimary:hover { background-color: %16; border-color: %16; }"
        "QPushButton#fdBtnPrimary:disabled { background-color: %7; border-color: %7; color: rgba(255,255,255,0.8); }"
        // ---- 滚动条 ----
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
        "QScrollBar::handle:vertical { background: %7; border-radius: 4px; min-height: 30px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: %7; border-radius: 4px; min-width: 30px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"
        "QToolTip { background-color: %3; color: %1; border: none; padding: 4px 8px; border-radius: 6px; }"
        ).arg(cardBg, cardBorder, titleColor, themeColor, subColor, btnText, muted,
              fieldBg, fieldBorder, fieldHover, listBg, hoverBg, selBg, themeLight, fieldHover, themeHover);

    QString finalStyle = style;
    if (m_mobileMode) {
        // 触屏：加大按钮点击区
        finalStyle += QStringLiteral(
            "QPushButton#fdBtn, QPushButton#fdBtnPrimary {"
            "    padding: 13px 24px; font-size: 14px; border-radius: 12px;"
            "}"
            "QLineEdit#fdNetEdit { padding: 11px 14px; font-size: 14px; }"
            "QPushButton#fdStep { padding: 8px 16px; }");
    }
    setStyleSheet(finalStyle);
}

/* ============================ 静态接口 ============================ */

QString OnlineResourceDialog::getResource(QWidget *parent, const QString &caption, SelectedResource *out)
{
    OnlineResourceDialog dlg(parent);
    dlg.setWindowTitle(caption);
    dlg.m_titleLabel->setText(caption);
    if (dlg.exec() == Accepted) {
        if (out)
            *out = dlg.m_resSelected;
        return dlg.m_result;
    }
    if (out)
        *out = SelectedResource();
    return QString();
}
