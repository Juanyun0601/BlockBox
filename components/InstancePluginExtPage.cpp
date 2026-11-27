/**
 * @file   InstancePluginExtPage.cpp
 * @brief  实例助手「扩展」页实现
 * @author BlockBox Team
 */
#include "InstancePluginExtPage.h"

#include "utils/plugin/PluginManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

/** 与插件详情页一致的参数拼装：true → 纯开关，false → -Name:$false，其余 → -Name value */
QStringList buildArgs(const QMap<QString, QString> &args,
                      const std::function<QString(const QString &)> &resolve)
{
    QStringList extra;
    for (auto it = args.cbegin(); it != args.cend(); ++it) {
        const QString name = QStringLiteral("-") + it.key();
        const QString value = resolve(it.value());
        if (value == QLatin1String("true"))
            extra << name;
        else if (value == QLatin1String("false"))
            extra << (name + QStringLiteral(":$false"));
        else
            extra << name << value;
    }
    return extra;
}

} // namespace

InstancePluginExtPage::InstancePluginExtPage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget(scroll);
    m_host = new QVBoxLayout(content);
    m_host->setContentsMargins(12, 12, 12, 12);
    m_host->setSpacing(12);
    scroll->setWidget(content);
    outer->addWidget(scroll);

    rebuild();
}

bool InstancePluginExtPage::hasPanels()
{
    return !PluginManager::instance()->instancePanels().isEmpty();
}

void InstancePluginExtPage::rebuild()
{
    if (!m_host)
        return;

    // 清空旧内容
    while (QLayoutItem *item = m_host->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    const QList<PluginInfo> panels = PluginManager::instance()->instancePanels();
    if (panels.isEmpty()) {
        auto *empty = new QLabel(tr("还没有插件提供实例助手面板。\n"
                                   "插件在 plugin.json 的 ui.placements 里加上 "
                                   "\"instanceAssistant\" 就会出现在这里。"),
                                 this);
        empty->setWordWrap(true);
        empty->setObjectName(QStringLiteral("pluginSettingLabel"));
        m_host->addWidget(empty);
        m_host->addStretch();
        return;
    }

    for (const PluginInfo &info : panels)
        buildPluginCard(m_host, info);

    m_host->addStretch();
}

void InstancePluginExtPage::buildPluginCard(QVBoxLayout *host, const PluginInfo &info)
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("pluginUiSection"));
    card->setFrameShape(QFrame::NoFrame);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);

    auto *title = new QLabel(info.name.isEmpty() ? info.id : info.name, card);
    QFont f = title->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 1.5);
    title->setFont(f);
    v->addWidget(title);

    if (!info.description.isEmpty()) {
        auto *desc = new QLabel(info.description, card);
        desc->setWordWrap(true);
        desc->setObjectName(QStringLiteral("pluginSettingLabel"));
        v->addWidget(desc);
    }

    // ---- 输入字段 ----
    QMap<QString, QWidget *> fieldWidgets;
    if (!info.ui.fields.isEmpty()) {
        auto *form = new QFormLayout();
        form->setLabelAlignment(Qt::AlignLeft);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(6);
        for (const PluginUiField &field : info.ui.fields) {
            QWidget *w = nullptr;
            if (field.type == QLatin1String("bool")) {
                auto *cb = new QCheckBox(card);
                cb->setChecked(field.defaultValue == QLatin1String("true")
                               || field.defaultValue == QLatin1String("1"));
                w = cb;
            } else if (field.type == QLatin1String("select")) {
                auto *combo = new QComboBox(card);
                for (const QString &opt : field.options) {
                    QString value = opt, display = opt;
                    const int sep = opt.indexOf(QLatin1Char('|'));
                    if (sep >= 0) {
                        value = opt.left(sep);
                        display = opt.mid(sep + 1);
                    }
                    combo->addItem(display, value);
                }
                if (!field.defaultValue.isEmpty())
                    combo->setCurrentText(field.defaultValue);
                w = combo;
            } else {
                auto *edit = new QLineEdit(field.defaultValue, card);
                if (!field.placeholder.isEmpty())
                    edit->setPlaceholderText(field.placeholder);
                w = edit;
            }
            w->setObjectName(QStringLiteral("pluginSettingInput"));
            fieldWidgets.insert(field.key, w);
            form->addRow(field.label.isEmpty() ? field.key : field.label, w);
        }
        v->addLayout(form);
    }

    // ---- 结果区 ----
    auto *output = new QPlainTextEdit(card);
    output->setReadOnly(true);
    output->setObjectName(QStringLiteral("pluginUiResult"));
    output->setMinimumHeight(90);
    output->setMaximumHeight(200);
    output->setVisible(false);
    v->addWidget(output);

    // ---- 动作按钮 ----
    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    for (const PluginUiAction &action : info.ui.actions) {
        auto *btn = new QPushButton(action.label.isEmpty() ? action.id : action.label, card);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setObjectName(QStringLiteral("pluginPageActionBtnPrimary"));
        btn->setFixedHeight(32);
        connect(btn, &QPushButton::clicked, this, [this, info, action, fieldWidgets, output]() {
            runAction(info, action, fieldWidgets, output);
        });
        row->addWidget(btn);
    }
    row->addStretch();
    v->addLayout(row);

    host->addWidget(card);
}

QString InstancePluginExtPage::fieldValue(QWidget *widget) const
{
    if (auto *edit = qobject_cast<QLineEdit *>(widget))
        return edit->text().trimmed();
    if (auto *cb = qobject_cast<QCheckBox *>(widget))
        return cb->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
    if (auto *combo = qobject_cast<QComboBox *>(widget))
        return combo->currentData().toString();
    return QString();
}

void InstancePluginExtPage::runAction(const PluginInfo &info, const PluginUiAction &action,
                                      const QMap<QString, QWidget *> &fieldWidgets,
                                      QPlainTextEdit *output)
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        output->setVisible(true);
        output->appendPlainText(tr("上一个动作还在执行，请稍候。"));
        return;
    }

    // 必填校验
    for (const PluginUiField &field : info.ui.fields) {
        if (field.required && fieldValue(fieldWidgets.value(field.key)).isEmpty()) {
            output->setVisible(true);
            output->appendPlainText(tr("请先填写「%1」。")
                                        .arg(field.label.isEmpty() ? field.key : field.label));
            return;
        }
    }

    // 解包插件到临时目录
    const QString tmpDir = QDir::tempPath() + QStringLiteral("/BlockBox/") + info.id;
    QString error;
    if (!PluginManager::instance()->extractPlugin(info.id, tmpDir, &error)) {
        output->setVisible(true);
        output->appendPlainText(tr("解包插件失败：%1").arg(error));
        return;
    }
    const QString scriptPath = QDir::cleanPath(tmpDir + QStringLiteral("/") + action.script);
    if (!QFileInfo::exists(scriptPath)) {
        output->setVisible(true);
        output->appendPlainText(tr("包内未找到脚本：%1").arg(action.script));
        return;
    }

    output->setVisible(true);
    output->clear();
    output->appendPlainText(tr("正在执行 %1 …").arg(action.label.isEmpty() ? action.id : action.label));

    const QStringList extra = buildArgs(action.args, [this, fieldWidgets](const QString &ref) {
        if (fieldWidgets.contains(ref))
            return fieldValue(fieldWidgets.value(ref));
        return ref; // 字面值（如 "true" / 固定参数）
    });

    if (m_proc)
        m_proc->deleteLater();
    m_proc = new QProcess(this);

    // 与 AI 工具路径保持一致：把插件私有变量用 BB_ 前缀交给脚本，
    // 这样界面按钮里的脚本也能拿到端口/令牌，不用手填。
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QMap<QString, QString> vars = PluginManager::instance()->resolvedPluginVars(info);
    env.insert(QStringLiteral("BB_PLUGIN_ID"), info.id);
    env.insert(QStringLiteral("BB_PLUGIN_DIR"), tmpDir);
    env.insert(QStringLiteral("BB_PLUGIN_DATA"), vars.value(QStringLiteral("data")));
    env.insert(QStringLiteral("BB_APP_DIR"), vars.value(QStringLiteral("app")));
    env.insert(QStringLiteral("BB_PLUGIN_TOKEN"), vars.value(QStringLiteral("token")));
    env.insert(QStringLiteral("BB_PLUGIN_PORT"), vars.value(QStringLiteral("port")));
    m_proc->setProcessEnvironment(env);
    m_proc->setWorkingDirectory(tmpDir);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this, output]() {
        const QString out = QString::fromLocal8Bit(m_proc->readAllStandardOutput());
        if (!out.isEmpty())
            output->appendPlainText(out.trimmed());
    });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this, output]() {
        const QString err = QString::fromLocal8Bit(m_proc->readAllStandardError());
        if (!err.isEmpty())
            output->appendPlainText(err.trimmed());
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, output](int code, QProcess::ExitStatus) {
                output->appendPlainText(code == 0 ? tr("—— 完成 ——")
                                                 : tr("—— 结束（退出码 %1）——").arg(code));
                emit statusMessage(tr("插件动作执行结束"));
            });

    QStringList psArgs;
    psArgs << QStringLiteral("-NoProfile") << QStringLiteral("-ExecutionPolicy")
           << QStringLiteral("Bypass") << QStringLiteral("-WindowStyle")
           << QStringLiteral("Hidden") << QStringLiteral("-File")
           << QDir::toNativeSeparators(scriptPath);
    psArgs << extra;

    m_proc->start(QStringLiteral("powershell"), psArgs);
}
