/**
 * @file   SettingsProjectionEditorPage.cpp
 * @brief  投影编辑器设置页实现（内置）
 * @author BlockBox Team
 * @date   2026-10-16
 *
 * 投影方块编辑器已内置，默认行为（默认工具 / 显示空气 / 材质 / 真实形状 /
 * 材质来源实例）在此页配置，保存在启动器主设置中。
 */
#include "pages/SettingsPage.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "components/AppFileDialog.h"
#include "components/CustomCheckBox.h"
#include "components/OutlinedLabel.h"
#include "utils/Schematic/ProjectionEditorSettings.h"

namespace {

/** 工具名 <-> 下拉索引（与编辑器 Tool 枚举顺序一致：查看/放置/破坏/填充） */
int toolIndexFromName(const QString &name)
{
    if (name == QLatin1String("放置"))
        return 1;
    if (name == QLatin1String("破坏"))
        return 2;
    if (name == QLatin1String("填充"))
        return 3;
    return 0;
}

QString toolNameFromIndex(int index)
{
    switch (index) {
    case 1:  return QStringLiteral("放置");
    case 2:  return QStringLiteral("破坏");
    case 3:  return QStringLiteral("填充");
    default: return QStringLiteral("查看");
    }
}

bool strToBool(const QString &s, bool defaultValue)
{
    if (s.isEmpty())
        return defaultValue;
    return (s == QLatin1String("true") || s == QLatin1String("1"));
}

} // namespace

void SettingsPage::initProjectionEditorSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_projectionEditorSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // ── 标题栏（标题 + 恢复默认值按钮）──
    createSettingsHeader(layout, tr("投影编辑器"));

    QLabel *hintLabel = new QLabel(
        tr("投影方块编辑器已内置，无需安装插件即可使用。\n"
           "此处可调整编辑器的默认行为；修改后即时生效。"),
        m_projectionEditorSettings);
    hintLabel->setObjectName("advancedHintLabel");
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    // ── 编辑工具 ──
    QVBoxLayout *toolCard = createSettingsCard(layout, tr("编辑工具"));

    m_projToolCombo = new QComboBox();
    m_projToolCombo->setObjectName("projToolCombo");
    m_projToolCombo->addItem(tr("查看"), 0);
    m_projToolCombo->addItem(tr("放置"), 1);
    m_projToolCombo->addItem(tr("破坏"), 2);
    m_projToolCombo->addItem(tr("填充"), 3);
    disableWheelEffect(m_projToolCombo);

    QHBoxLayout *toolRow = appendSettingRow(toolCard,
        tr("默认工具"), tr("打开编辑器时默认选中的工具（随后可在编辑器工具栏切换）。"), QString());
    toolRow->addWidget(m_projToolCombo);

    connect(m_projToolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
        ProjectionEditorSettings::setValue(QStringLiteral("defaultTool"),
                                           toolNameFromIndex(index));
    });

    m_projShowAirCheck = new CustomCheckBox();
    QHBoxLayout *airRow = appendSettingRow(toolCard,
        tr("默认显示空气方块"), tr("勾选后打开编辑器即显示空气方块（不勾选则默认隐藏）。"), QString(), true);
    airRow->addWidget(m_projShowAirCheck);

    connect(m_projShowAirCheck, &CustomCheckBox::toggled, this, [this](bool on) {
        ProjectionEditorSettings::setValue(QStringLiteral("showAir"),
                                           on ? QStringLiteral("true") : QStringLiteral("false"));
    });

    // ── 渲染效果 ──
    QVBoxLayout *renderCard = createSettingsCard(layout, tr("渲染效果"));

    m_projTextureCheck = new CustomCheckBox();
    QHBoxLayout *textureRow = appendSettingRow(renderCard,
        tr("启用实例材质（方块贴图）"), tr("使用游戏实例的方块贴图渲染，观感更真实；关闭则回退到纯色显示。"), QString());
    textureRow->addWidget(m_projTextureCheck);

    connect(m_projTextureCheck, &CustomCheckBox::toggled, this, [this](bool on) {
        ProjectionEditorSettings::setValue(QStringLiteral("useTextures"),
                                           on ? QStringLiteral("true") : QStringLiteral("false"));
    });

    m_projShapesCheck = new CustomCheckBox();
    QHBoxLayout *shapeRow = appendSettingRow(renderCard,
        tr("真实形状渲染（活板门等）"),
        tr("按方块的真实模型渲染非完整方块（活板门、台阶、栅栏等）；关闭则统一显示为完整立方体。"), QString(), true);
    shapeRow->addWidget(m_projShapesCheck);

    connect(m_projShapesCheck, &CustomCheckBox::toggled, this, [this](bool on) {
        ProjectionEditorSettings::setValue(QStringLiteral("useShapes"),
                                           on ? QStringLiteral("true") : QStringLiteral("false"));
    });

    // ── 材质来源 ──
    QVBoxLayout *sourceCard = createSettingsCard(layout, tr("材质来源"));

    m_projSourceInstanceEdit = new QLineEdit();
    m_projSourceInstanceEdit->setObjectName("projSourceInstanceEdit");
    m_projSourceInstanceEdit->setPlaceholderText(
        tr("例如：<实例目录>/versions/<版本>，用于加载方块贴图"));

    QPushButton *browseBtn = new QPushButton(tr("浏览…"));
    browseBtn->setObjectName("restoreDefaultsBtn");
    browseBtn->setCursor(Qt::PointingHandCursor);

    QHBoxLayout *sourceRow = appendSettingRow(sourceCard,
        tr("默认材质来源实例目录"),
        tr("从投影管理页进入时自动使用该实例的贴图；从别处打开且未指定实例时，才使用这里填写的实例目录。"),
        QString(), true);
    sourceRow->addWidget(m_projSourceInstanceEdit, 1);
    sourceRow->addWidget(browseBtn);

    connect(m_projSourceInstanceEdit, &QLineEdit::editingFinished, this, [this]() {
        ProjectionEditorSettings::setValue(QStringLiteral("sourceInstance"),
                                           m_projSourceInstanceEdit->text().trimmed());
    });
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString start = m_projSourceInstanceEdit->text().trimmed();
        const QString dir = AppFileDialog::getExistingDirectory(this, tr("选择游戏实例目录"), start);
        if (!dir.isEmpty()) {
            m_projSourceInstanceEdit->setText(dir);
            ProjectionEditorSettings::setValue(QStringLiteral("sourceInstance"), dir);
        }
    });

    // ── 初始值 ──
    refreshProjectionEditorSettingsControls();

    layout->addStretch();
}

/**
 * @brief 把当前存储的投影编辑器设置同步到本页控件
 *
 * 供初始化与「恢复默认值」后刷新界面使用。
 */
void SettingsPage::refreshProjectionEditorSettingsControls()
{
    if (!m_projToolCombo || !m_projShowAirCheck)
        return;

    const QString tool = ProjectionEditorSettings::value(
        QStringLiteral("defaultTool"),
        ProjectionEditorSettings::defaultTool());
    m_projToolCombo->setCurrentIndex(toolIndexFromName(tool));

    const bool showAir = strToBool(
        ProjectionEditorSettings::value(QStringLiteral("showAir"),
                                        ProjectionEditorSettings::defaultShowAir()),
        false);
    m_projShowAirCheck->setChecked(showAir);

    const bool textures = strToBool(
        ProjectionEditorSettings::value(QStringLiteral("useTextures"),
                                        ProjectionEditorSettings::defaultUseTextures()),
        true);
    m_projTextureCheck->setChecked(textures);

    const bool shapes = strToBool(
        ProjectionEditorSettings::value(QStringLiteral("useShapes"),
                                        ProjectionEditorSettings::defaultUseShapes()),
        true);
    m_projShapesCheck->setChecked(shapes);

    if (m_projSourceInstanceEdit)
        m_projSourceInstanceEdit->setText(
            ProjectionEditorSettings::value(QStringLiteral("sourceInstance")));
}