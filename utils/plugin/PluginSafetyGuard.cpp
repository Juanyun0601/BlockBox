/**
 * @file   PluginSafetyGuard.cpp
 * @brief  插件执行安全风控实现（信任确认 + 权限 + 危险操作扫描）
 * @author BlockBox Team
 * @date   2026-08-07
 */
#include "PluginSafetyGuard.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>
#include <QWidget>

#include "components/AppMessageBox.h"
#include "utils/ThemeManager.h"

namespace PluginSafetyGuard {

namespace {

/** 权限定义：token / 中文描述 / 最低风险 */
struct PermissionDef {
    const char *token;
    const char *label;
    RiskLevel minRisk;
};

const PermissionDef kPermissionDefs[] = {
    { "file:read",  "读取方块盒子文件",            RiskLow },
    { "file:write", "修改方块盒子文件/代码",        RiskMedium },
    { "registry",   "读写 Windows 注册表",         RiskMedium },
    { "network",    "访问网络",                    RiskLow },
    { "admin",      "请求管理员权限（UAC 提权）",   RiskHigh },
    { "system",     "执行系统级操作（服务/关机/格式化等）", RiskHigh },
    { "ui:style",   "修改启动器界面样式（仅外观）", RiskLow },
};

/** 危险模式表：pattern / 说明 / 是否高危 */
struct PatternEntry {
    const char *pattern;
    const char *detail;
    bool critical;
};

const PatternEntry kDangerPatterns[] = {
    // —— 删除类（高危）——
    {"remove-item",            "删除文件/目录（Remove-Item）",       true},
    {"remove-childitem",       "递归删除文件/目录",                 true},
    {"rm -rf",                 "强制递归删除（rm -rf）",            true},
    {"rmdir",                  "删除目录",                           true},
    {"rd /s",                  "递归删除目录（cmd）",                true},
    {"del /s",                 "递归删除文件（cmd）",                true},
    {"erase /s",               "递归删除文件（erase）",              true},
    {"clear-recyclebin",       "清空回收站",                         false},
    // —— 格式化 / 磁盘（高危）——
    {"format-volume",          "格式化磁盘卷（Format-Volume）",      true},
    {"format ",                "格式化磁盘（format）",               true},
    {"diskpart",               "磁盘分区操作（diskpart）",           true},
    // —— 关机 / 重启 / 服务 ——
    {"shutdown",               "关机/重启/注销（shutdown）",         false},
    {"restart-computer",       "重启计算机",                         true},
    {"stop-computer",          "关闭计算机",                         true},
    {"stop-service",           "停止系统服务",                       true},
    {"set-service",            "修改系统服务",                       true},
    {"new-service",            "创建系统服务",                       true},
    {"sc delete",              "删除系统服务（sc delete）",          true},
    {"bcdedit",                "修改引导配置（bcdedit）",            true},
    // —— 注册表 ——
    {"reg delete",             "删除注册表项（reg delete）",         true},
    {"reg add",                "修改注册表项（reg add）",            true},
    {"set-itemproperty",       "修改注册表值/属性",                  true},
    {"remove-itemproperty",    "删除注册表值",                       true},
    {"set-executionpolicy",    "修改 PowerShell 执行策略",           true},
    // —— 文件权限 / hosts ——
    {"takeown",                "夺取文件所有权（takeown）",          true},
    {"icacls",                 "修改文件权限（icacls）",             true},
    {"attrib ",                "修改文件属性（attrib）",             false},
    {"\\hosts",                "修改 hosts 文件",                    true},
    // —— 进程 ——
    {"stop-process",           "终止进程（Stop-Process）",           false},
    {"taskkill",               "终止进程（taskkill）",               false},
    {"wmic process",           "进程管理（wmic）",                   false},
};

/** 危险模式 → 所需权限映射（越权检测用） */
struct PatternPermEntry {
    const char *pattern;
    const char *perm;
};

const PatternPermEntry kPatternPerms[] = {
    // 文件修改类 → file:write
    {"remove-item",         "file:write"},
    {"remove-childitem",    "file:write"},
    {"rm -rf",              "file:write"},
    {"rmdir",               "file:write"},
    {"rd /s",               "file:write"},
    {"del /s",              "file:write"},
    {"erase /s",            "file:write"},
    {"takeown",             "file:write"},
    {"icacls",              "file:write"},
    {"attrib ",             "file:write"},
    {"\\hosts",             "file:write"},
    // 注册表类 → registry
    {"reg delete",          "registry"},
    {"reg add",             "registry"},
    {"set-itemproperty",    "registry"},
    {"remove-itemproperty", "registry"},
    // 系统级 → system
    {"shutdown",            "system"},
    {"restart-computer",    "system"},
    {"stop-computer",       "system"},
    {"stop-service",        "system"},
    {"set-service",         "system"},
    {"new-service",         "system"},
    {"sc delete",           "system"},
    {"bcdedit",             "system"},
    {"diskpart",            "system"},
    {"format-volume",       "system"},
    {"format ",             "system"},
    {"clear-recyclebin",    "system"},
    {"set-executionpolicy", "system"},
    {"stop-process",        "system"},
    {"taskkill",            "system"},
    {"wmic process",        "system"},
};

/** 是否为注释行（PowerShell # / cmd REM、::） */
bool isCommentLine(const QString &line)
{
    const QString t = line.trimmed().toLower();
    return t.startsWith(QLatin1String("#"))
        || t.startsWith(QLatin1String("rem "))
        || t.startsWith(QLatin1String("::"));
}

/** 读取当前主题下的文字/背景色，适配浅色/深色主题 */
struct ThemeColors {
    QString textMain;   // 主文字
    QString textSub;    // 次要文字
    QString cardBg;     // 卡片背景
    QString cardBorder; // 卡片边框
    QString fieldBg;    // 输入区背景
    QString title;      // 标题
};

ThemeColors themeColors()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm && tm->currentTheme() == ThemeManager::LightTheme);
    ThemeColors c;
    if (isLight) {
        c.textMain    = QStringLiteral("#202124");
        c.textSub     = QStringLiteral("#6b6b6b");
        c.cardBg      = QStringLiteral("#ffffff");
        c.cardBorder  = QStringLiteral("#e6e6e6");
        c.fieldBg     = QStringLiteral("#f7f7f7");
        c.title       = QStringLiteral("#1a1a1a");
    } else {
        c.textMain    = QStringLiteral("#ececef");
        c.textSub     = QStringLiteral("#a5a5ad");
        c.cardBg      = QStringLiteral("#2e2e32");
        c.cardBorder  = QStringLiteral("#45454a");
        c.fieldBg     = QStringLiteral("#3b3b40");
        c.title       = QStringLiteral("#f2f2f2");
    }
    return c;
}

/** 构建信任确认弹窗正文 */
QString buildMessage(const QString &pluginName, const QString &pluginAuthor,
                     const QString &pluginVersion, const QStringList &permissions,
                     const QString &actionLabel, RiskLevel level, const QString &riskNote,
                     const QList<RiskFinding> &findings,
                     const QStringList &violations,
                     const QString &trustAction)
{
    QStringList parts;

    // 插件元数据
    QString meta = QObject::tr("插件：%1").arg(pluginName);
    if (!pluginAuthor.isEmpty())
        meta += QStringLiteral("　") + QObject::tr("作者：%1").arg(pluginAuthor);
    if (!pluginVersion.isEmpty())
        meta += QStringLiteral("　") + QObject::tr("版本：v%1").arg(pluginVersion);
    parts << meta
          << QObject::tr("操作：%1").arg(actionLabel.isEmpty() ? QObject::tr("执行") : actionLabel);

    // 请求的能力权限
    if (!permissions.isEmpty()) {
        QStringList items;
        for (const QString &p : permissionLabels(permissions))
            items << QStringLiteral("  · %1").arg(p);
        parts << QObject::tr("该插件请求以下能力：") + QStringLiteral("\n")
              + items.join(QLatin1Char('\n'));
    }

    // 风险
    parts << QObject::tr("风险等级：%1").arg(levelDisplayName(level));
    if (!riskNote.isEmpty())
        parts << QObject::tr("风险说明：%1").arg(riskNote);

    // 脚本扫描结果
    if (!findings.isEmpty()) {
        QStringList items;
        for (const RiskFinding &f : findings)
            items << QStringLiteral("  · %1").arg(f.detail);
        parts << QObject::tr("已检测到脚本中的敏感操作：") + QStringLiteral("\n")
              + items.join(QLatin1Char('\n'));
    }

    // 越权警告
    if (!violations.isEmpty()) {
        QStringList items;
        for (const QString &p : violations)
            items << QStringLiteral("  · %1（%2）").arg(permissionLabel(p), p);
        parts << QObject::tr("⚠ 该插件未声明以下权限，却执行了相关敏感操作：")
              + QStringLiteral("\n") + items.join(QLatin1Char('\n'));
    }

    parts << QStringLiteral("\n")
          + QObject::tr("你信任该插件吗？信任后将%1。").arg(trustAction);
    return parts.join(QLatin1Char('\n'));
}

/** 高风险专用确认框：必须勾选「我已了解风险」才能执行 */
bool confirmHighRisk(QWidget *parent, const QString &title, const QString &message,
                     const QString &trustAction)
{
    const ThemeColors c = themeColors();
    const QString themeHex = ThemeManager::instance()->currentThemeColor();

    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setModal(true);
    dlg.setMinimumWidth(500);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(24, 22, 24, 18);
    layout->setSpacing(12);

    QLabel *titleLabel = new QLabel(title, &dlg);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #e5484d;"));
    layout->addWidget(titleLabel);

    QLabel *msgLabel = new QLabel(message, &dlg);
    msgLabel->setWordWrap(true);
    msgLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    msgLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(c.textMain));
    layout->addWidget(msgLabel);

    QCheckBox *confirmBox = new QCheckBox(
        QObject::tr("我已了解风险，信任并允许该插件%1").arg(trustAction), &dlg);
    confirmBox->setStyleSheet(QStringLiteral("color: #e5484d; font-size: 13px; font-weight: 600;"));
    layout->addWidget(confirmBox);

    QDialogButtonBox *buttons = new QDialogButtonBox(&dlg);
    QPushButton *okBtn = buttons->addButton(
        QObject::tr("信任并%1").arg(trustAction), QDialogButtonBox::AcceptRole);
    QPushButton *cancelBtn = buttons->addButton(QObject::tr("拒绝"), QDialogButtonBox::RejectRole);
    okBtn->setEnabled(false);   // 未勾选前不可执行
    okBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setCursor(Qt::PointingHandCursor);

    QObject::connect(confirmBox, &QCheckBox::toggled, okBtn, &QPushButton::setEnabled);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    okBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: #ffffff; border: none; border-radius: 8px;"
        " padding: 6px 18px; font-size: 13px; font-weight: 600; }"
        "QPushButton:disabled { background-color: #c9c9c9; color: #f5f5f5; }").arg(themeHex));
    cancelBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px;"
        " padding: 6px 18px; font-size: 13px; }").arg(c.cardBg, c.textMain, c.cardBorder));

    dlg.setStyleSheet(QStringLiteral(
        "QDialog { background-color: %1; border: 1px solid %2; border-radius: 12px; }")
        .arg(c.cardBg, c.cardBorder));

    return dlg.exec() == QDialog::Accepted;
}

} // namespace

RiskLevel levelFromString(const QString &s, RiskLevel fallback)
{
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("low"))
        return RiskLow;
    if (v == QLatin1String("high"))
        return RiskHigh;
    if (v == QLatin1String("medium"))
        return RiskMedium;
    return fallback;
}

QString levelDisplayName(RiskLevel level)
{
    switch (level) {
    case RiskLow:    return QObject::tr("低风险");
    case RiskMedium: return QObject::tr("中风险");
    case RiskHigh:   return QObject::tr("高风险");
    }
    return QObject::tr("中风险");
}

QList<RiskFinding> scanScript(const QString &scriptContent)
{
    QList<RiskFinding> findings;
    if (scriptContent.isEmpty())
        return findings;

    const QStringList lines = scriptContent.split(QLatin1Char('\n'));
    const int patternCount = static_cast<int>(sizeof(kDangerPatterns) / sizeof(kDangerPatterns[0]));

    for (const QString &rawLine : lines) {
        if (isCommentLine(rawLine))
            continue;
        const QString line = rawLine.toLower();
        if (line.isEmpty())
            continue;
        for (int i = 0; i < patternCount; ++i) {
            const PatternEntry &pe = kDangerPatterns[i];
            if (!line.contains(QLatin1String(pe.pattern)))
                continue;
            // 去重：同一模式只报一次
            bool exists = false;
            for (const RiskFinding &f : findings) {
                if (f.pattern == QLatin1String(pe.pattern)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                RiskFinding f;
                f.pattern = QLatin1String(pe.pattern);
                f.detail = QObject::tr(pe.detail);
                f.critical = pe.critical;
                findings.append(f);
            }
        }
    }
    return findings;
}

QString permissionLabel(const QString &token)
{
    const QString t = token.trimmed().toLower();
    const int n = static_cast<int>(sizeof(kPermissionDefs) / sizeof(kPermissionDefs[0]));
    for (int i = 0; i < n; ++i) {
        if (t == QLatin1String(kPermissionDefs[i].token))
            return QObject::tr(kPermissionDefs[i].label);
    }
    return token;   // 未知权限原样显示
}

QStringList permissionLabels(const QStringList &permissions)
{
    QStringList labels;
    for (const QString &p : permissions)
        labels << permissionLabel(p);
    return labels;
}

QStringList checkPermissionViolations(const QStringList &permissions,
                                      const QList<RiskFinding> &findings)
{
    QSet<QString> declared;
    for (const QString &p : permissions)
        declared.insert(p.trimmed().toLower());

    QSet<QString> violated;
    const int n = static_cast<int>(sizeof(kPatternPerms) / sizeof(kPatternPerms[0]));
    for (const RiskFinding &f : findings) {
        for (int i = 0; i < n; ++i) {
            const PatternPermEntry &pe = kPatternPerms[i];
            if (f.pattern == QLatin1String(pe.pattern)
                && !declared.contains(QLatin1String(pe.perm))) {
                violated.insert(QLatin1String(pe.perm));
            }
        }
    }

    QStringList result;
    for (const QString &p : violated)
        result.append(p);
    return result;
}

RiskLevel effectiveLevel(RiskLevel declared,
                         const QList<RiskFinding> &findings,
                         const QStringList &permissions)
{
    RiskLevel level = declared;

    // 1. 脚本扫描结果提升
    for (const RiskFinding &f : findings) {
        if (f.critical) {
            if (level < RiskHigh)
                level = RiskHigh;
        } else if (level < RiskMedium) {
            level = RiskMedium;
        }
    }

    // 2. 敏感权限提升
    const int n = static_cast<int>(sizeof(kPermissionDefs) / sizeof(kPermissionDefs[0]));
    for (const QString &p : permissions) {
        const QString t = p.trimmed().toLower();
        for (int i = 0; i < n; ++i) {
            if (t == QLatin1String(kPermissionDefs[i].token)) {
                if (level < kPermissionDefs[i].minRisk)
                    level = kPermissionDefs[i].minRisk;
                break;
            }
        }
    }

    // 3. 越权提升
    if (!checkPermissionViolations(permissions, findings).isEmpty())
        level = RiskHigh;

    return level;
}

/**
 * @brief 通用信任确认（内部）
 *
 * @param trustAction 信任后的动作描述，如「执行此操作」/「添加此插件」
 */
bool confirmTrusted(QWidget *parent,
                    const QString &pluginName,
                    const QString &pluginAuthor,
                    const QString &pluginVersion,
                    const QStringList &permissions,
                    const QString &actionLabel,
                    RiskLevel declared,
                    const QString &riskNote,
                    const QString &scriptContent,
                    const QString &trustAction)
{
    // 1. 扫描脚本内容
    const QList<RiskFinding> findings = scanScript(scriptContent);

    // 2. 越权检测
    const QStringList violations = checkPermissionViolations(permissions, findings);

    // 3. 综合风险等级
    const RiskLevel level = effectiveLevel(declared, findings, permissions);

    const QString message = buildMessage(pluginName, pluginAuthor, pluginVersion,
                                         permissions, actionLabel, level, riskNote,
                                         findings, violations, trustAction);

    // 4. 按风险等级弹窗
    if (level == RiskHigh) {
        return confirmHighRisk(parent,
                               QObject::tr("⚠ 你信任该插件吗？"),
                               message, trustAction);
    }

    const AppMessageBox::StandardButton ret = AppMessageBox::question(
        parent,
        QObject::tr("你信任该插件吗？"),
        message,
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);
    return ret == AppMessageBox::Yes;
}

bool confirmBeforeExecute(QWidget *parent,
                          const QString &pluginName,
                          const QString &pluginAuthor,
                          const QString &pluginVersion,
                          const QStringList &permissions,
                          const QString &actionLabel,
                          RiskLevel declared,
                          const QString &riskNote,
                          const QString &scriptContent)
{
    return confirmTrusted(parent, pluginName, pluginAuthor, pluginVersion,
                          permissions, actionLabel, declared, riskNote, scriptContent,
                          QObject::tr("执行此操作"));
}

bool confirmBeforeInstall(QWidget *parent,
                          const QString &pluginName,
                          const QString &pluginAuthor,
                          const QString &pluginVersion,
                          const QStringList &permissions,
                          RiskLevel declared,
                          const QString &riskNote,
                          const QString &scriptContent)
{
    return confirmTrusted(parent, pluginName, pluginAuthor, pluginVersion,
                          permissions,
                          QObject::tr("添加此插件到方块盒子"),
                          declared, riskNote, scriptContent,
                          QObject::tr("添加此插件"));
}

bool confirmBatchInstall(QWidget *parent,
                         const QList<BatchPluginEntry> &entries,
                         QList<int> &trustedIndices)
{
    trustedIndices.clear();
    if (entries.isEmpty())
        return false;

    // 单个插件时退回到逐个确认
    if (entries.size() == 1) {
        const BatchPluginEntry &e = entries.first();
        if (confirmBeforeInstall(parent, e.name, e.author, e.version,
                                 e.permissions, e.declared, e.riskNote,
                                 e.scriptContent)) {
            trustedIndices.append(0);
            return true;
        }
        return false;
    }

    // 预计算每个插件的综合风险等级
    struct EntryInfo {
        RiskLevel level;
        QList<RiskFinding> findings;
        QStringList violations;
    };
    QVector<EntryInfo> infos(entries.size());
    RiskLevel maxLevel = RiskLow;
    int highCount = 0;
    for (int i = 0; i < entries.size(); ++i) {
        const BatchPluginEntry &e = entries.at(i);
        infos[i].findings = scanScript(e.scriptContent);
        infos[i].violations = checkPermissionViolations(e.permissions, infos[i].findings);
        infos[i].level = effectiveLevel(e.declared, infos[i].findings, e.permissions);
        if (infos[i].level > maxLevel)
            maxLevel = infos[i].level;
        if (infos[i].level == RiskHigh)
            ++highCount;
    }

    const ThemeColors c = themeColors();
    const QString themeHex = ThemeManager::instance()->currentThemeColor();

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("批量导入插件"));
    dlg.setModal(true);
    dlg.setMinimumWidth(560);
    dlg.setMaximumWidth(640);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(24, 22, 24, 18);
    mainLayout->setSpacing(14);

    // 标题
    QLabel *titleLabel = new QLabel(
        QObject::tr("批量导入 %1 个插件").arg(entries.size()), &dlg);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: %1;").arg(c.title));
    mainLayout->addWidget(titleLabel);

    // 说明
    QString descText;
    if (highCount > 0) {
        descText = QObject::tr("其中 %1 个为高风险插件，需单独确认信任。").arg(highCount);
    } else {
        descText = QObject::tr("以下插件将被导入，点击「全部信任」一键导入。");
    }
    QLabel *descLabel = new QLabel(descText, &dlg);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(c.textSub));
    mainLayout->addWidget(descLabel);

    // 插件列表（滚动区域）
    QScrollArea *scrollArea = new QScrollArea(&dlg);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    scrollArea->setMaximumHeight(320);

    QWidget *listWidget = new QWidget();
    QVBoxLayout *listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(8);

    struct PluginCheckbox {
        QCheckBox *checkBox;
        int index;
    };
    QList<PluginCheckbox> checkboxes;

    for (int i = 0; i < entries.size(); ++i) {
        const BatchPluginEntry &e = entries.at(i);
        const RiskLevel level = infos[i].level;

        QHBoxLayout *row = new QHBoxLayout();
        row->setSpacing(10);

        QCheckBox *cb = new QCheckBox(&dlg);
        cb->setChecked(level != RiskHigh);  // 高风险默认不勾选
        cb->setMinimumWidth(20);
        row->addWidget(cb);

        checkboxes.append({cb, i});

        // 插件信息
        QVBoxLayout *infoCol = new QVBoxLayout();
        infoCol->setSpacing(2);

        // 第一行：名称 + 风险标签
        QHBoxLayout *topRow = new QHBoxLayout();
        topRow->setSpacing(8);

        QLabel *nameLabel = new QLabel(e.name, &dlg);
        nameLabel->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600; color: %1;").arg(c.title));
        topRow->addWidget(nameLabel);

        // 风险等级标签
        QString riskColor;
        if (level == RiskHigh)
            riskColor = QStringLiteral("#e5484d");
        else if (level == RiskMedium)
            riskColor = QStringLiteral("#f5a623");
        else
            riskColor = QStringLiteral("#4caf50");

        QLabel *riskBadge = new QLabel(levelDisplayName(level), &dlg);
        riskBadge->setStyleSheet(QStringLiteral(
            "font-size: 11px; font-weight: 600; color: %1; background: %1%2; "
            "border-radius: 4px; padding: 1px 6px;")
            .arg(riskColor, QStringLiteral("18")));
        topRow->addWidget(riskBadge);
        topRow->addStretch();

        infoCol->addLayout(topRow);

        // 第二行：作者/版本 + 权限
        QStringList metaParts;
        if (!e.author.isEmpty())
            metaParts << QObject::tr("作者：%1").arg(e.author);
        if (!e.version.isEmpty())
            metaParts << QObject::tr("v%1").arg(e.version);
        if (!e.permissions.isEmpty())
            metaParts << QObject::tr("权限：%1").arg(e.permissions.join(QStringLiteral(", ")));

        if (!metaParts.isEmpty()) {
            QLabel *metaLabel = new QLabel(metaParts.join(QStringLiteral("　")), &dlg);
            metaLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: %1;").arg(c.textSub));
            infoCol->addWidget(metaLabel);
        }

        // 第三行：扫描到的危险操作
        if (!infos[i].findings.isEmpty()) {
            QStringList findingStrs;
            for (const RiskFinding &f : infos[i].findings)
                findingStrs << f.detail;
            QLabel *findingsLabel = new QLabel(
                QObject::tr("⚠ 检测到：%1").arg(findingStrs.join(QStringLiteral("、"))), &dlg);
            findingsLabel->setWordWrap(true);
            findingsLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #e5484d;"));
            infoCol->addWidget(findingsLabel);
        }

        // 越权警告
        if (!infos[i].violations.isEmpty()) {
            QStringList violStrs;
            for (const QString &p : infos[i].violations)
                violStrs << permissionLabel(p);
            QLabel *violLabel = new QLabel(
                QObject::tr("⚠ 未声明权限：%1").arg(violStrs.join(QStringLiteral("、"))), &dlg);
            violLabel->setWordWrap(true);
            violLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #e5484d; font-weight: 600;"));
            infoCol->addWidget(violLabel);
        }

        row->addLayout(infoCol);
        listLayout->addLayout(row);
    }

    listLayout->addStretch();
    scrollArea->setWidget(listWidget);
    mainLayout->addWidget(scrollArea);

    // 按钮区
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    QPushButton *cancelBtn = new QPushButton(QObject::tr("全部拒绝"), &dlg);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px;"
        " padding: 8px 20px; font-size: 13px; }"
        "QPushButton:hover { background-color: %3; }").arg(c.cardBg, c.textMain, c.cardBorder));
    btnLayout->addWidget(cancelBtn);

    QPushButton *trustAllBtn = new QPushButton(QObject::tr("全部信任"), &dlg);
    trustAllBtn->setCursor(Qt::PointingHandCursor);
    trustAllBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: #ffffff; border: none; border-radius: 8px;"
        " padding: 8px 20px; font-size: 13px; font-weight: 600; }"
        "QPushButton:hover { opacity: 0.9; }").arg(themeHex));
    btnLayout->addWidget(trustAllBtn);

    mainLayout->addLayout(btnLayout);

    dlg.setStyleSheet(QStringLiteral(
        "QDialog { background-color: %1; border: 1px solid %2; border-radius: 12px; }")
        .arg(c.cardBg, c.cardBorder));

    // 连接按钮
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    bool accepted = false;
    QObject::connect(trustAllBtn, &QPushButton::clicked, [&]() {
        // 收集所有非高风险的勾选项 + 高风险的已勾选项
        for (const PluginCheckbox &pc : checkboxes) {
            if (pc.checkBox->isChecked())
                trustedIndices.append(pc.index);
        }
        accepted = true;
        dlg.accept();
    });

    dlg.exec();

    if (!accepted) {
        // 用户关闭窗口等同于拒绝
        trustedIndices.clear();
        return false;
    }

    return !trustedIndices.isEmpty();
}

} // namespace PluginSafetyGuard
