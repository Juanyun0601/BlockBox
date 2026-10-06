/**
 * @file   SettingsAiAssistantPage.cpp
 * @brief  AI 助手设置页实现
 * @author BlockBox Team
 * @date   2026-08-25
 */
#include "pages/SettingsPage.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QUuid>
#include <QVBoxLayout>

#include "components/CustomCheckBox.h"
#include "components/OutlinedLabel.h"
#include "utils/SettingsManager.h"

void SettingsPage::initAiAssistantSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_aiAssistantSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // ── 标题栏（标题 + 恢复默认值按钮）──
    createSettingsHeader(layout, tr("AI 助手"));

    QLabel *hintLabel = new QLabel(tr("配置 AI 助手的模型、联网搜索和工作模式等参数"), m_aiAssistantSettings);
    hintLabel->setObjectName("advancedHintLabel");
    layout->addWidget(hintLabel);

    SettingsManager *settings = SettingsManager::instance();

    // ── 默认模型设置 ──
    QVBoxLayout *defaultModelCard = createSettingsCard(layout, tr("默认模型"));

    QComboBox *defaultModelCombo = new QComboBox();
    defaultModelCombo->addItems({tr("DeepSeek Chat"), tr("DeepSeek Reasoner"), tr("GPT-4o"), tr("GPT-4o-mini"), tr("Claude 3.5 Sonnet"), tr("自定义")});
    disableWheelEffect(defaultModelCombo);

    int savedModelIndex = settings->getProperty("ai/defaultModelIndex", 0).toInt();
    defaultModelCombo->setCurrentIndex(savedModelIndex);

    connect(defaultModelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
        settings->setProperty("ai/defaultModelIndex", index);
    });

    QHBoxLayout *defaultModelRow = appendSettingRow(defaultModelCard,
        tr("默认模型"), tr("选择 AI 助手默认使用的模型。"),
        tr("不同模型在能力、速度和价格上有差异，请根据需求选择。"));
    defaultModelRow->addWidget(defaultModelCombo);

    // ── 服务商配置 ──
    QVBoxLayout *providerCard = createSettingsCard(layout, tr("服务商配置"));

    // 包装容器，提供左右内边距
    QWidget *providerContent = new QWidget();
    providerContent->setObjectName("providerCardContent");
    QVBoxLayout *providerContentLayout = new QVBoxLayout(providerContent);
    providerContentLayout->setContentsMargins(24, 8, 24, 8);
    providerContentLayout->setSpacing(8);

    QLabel *providerHint = new QLabel(tr("配置各服务商的 API 地址和密钥，配置后可在模型选择窗口中使用对应模型。"), providerContent);
    providerHint->setObjectName("settingDesc");
    providerHint->setWordWrap(true);
    providerContentLayout->addWidget(providerHint);

    // 服务商配置列表
    struct ProviderInfo {
        QString key;
        QString name;
        QString defaultUrl;
        QString configKey;
    };

    QList<ProviderInfo> providers = {
        {"DeepSeek", "DeepSeek", "https://api.deepseek.com/v1", "ai/provider_deepseek"},
        {"OpenAI", "OpenAI", "https://api.openai.com/v1", "ai/provider_openai"},
        {"Anthropic", "Anthropic", "https://api.anthropic.com", "ai/provider_anthropic"},
        {"Gemini", "Google Gemini", "https://generativelanguage.googleapis.com/v1", "ai/provider_gemini"},
        {"OpenRouter", "OpenRouter", "https://openrouter.ai/api/v1", "ai/provider_openrouter"},
        {"GLM", "智谱 GLM", "https://open.bigmodel.cn/api/paas/v4", "ai/provider_glm"},
        {"Qwen", "通义千问", "https://dashscope.aliyuncs.com/compatible-mode/v1", "ai/provider_qwen"},
        {"ERNIE", "文心一言", "https://aip.baidubce.com", "ai/provider_ernie"},
    };

    QListWidget *providerList = new QListWidget();
    providerList->setObjectName("providerList");
    providerList->setMinimumHeight(100);
    providerList->setMaximumHeight(200);

    // 自定义服务商（排在最前面）
    QSettings customSettings;
    QString customJsonStr = customSettings.value("ai/customModels", "").toString();
    int customCount = 0;
    if (!customJsonStr.isEmpty()) {
        QJsonParseError pe;
        QJsonDocument d = QJsonDocument::fromJson(customJsonStr.toUtf8(), &pe);
        if (pe.error == QJsonParseError::NoError && d.isArray())
            customCount = d.array().size();
    }
    QString customDisplay = tr("自定义服务商");
    if (customCount > 0)
        customDisplay += QString("  (%1 %2)").arg(customCount).arg(tr("个模型"));

    QListWidgetItem *customItem = new QListWidgetItem(customDisplay);
    customItem->setData(Qt::UserRole, QStringLiteral("__custom__"));
    customItem->setData(Qt::UserRole + 1, QVariant::fromValue(QString()));
    customItem->setData(Qt::UserRole + 2, QVariant::fromValue(QString()));
    customItem->setData(Qt::UserRole + 3, QVariant::fromValue(QString()));
    providerList->addItem(customItem);

    // 内置服务商
    for (const ProviderInfo &provider : providers) {
        QString savedKey = settings->getProperty(provider.configKey + "/key", "").toString();
        QString displayText = provider.name;
        if (!savedKey.isEmpty()) {
            displayText += "  " + tr("(已配置)");
        }
        QListWidgetItem *item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, QVariant::fromValue(provider.key));
        item->setData(Qt::UserRole + 1, QVariant::fromValue(provider.name));
        item->setData(Qt::UserRole + 2, QVariant::fromValue(provider.defaultUrl));
        item->setData(Qt::UserRole + 3, QVariant::fromValue(provider.configKey));
        providerList->addItem(item);
    }

    QLabel *providerListLabel = new QLabel(tr("服务商列表"), providerContent);
    providerListLabel->setObjectName("settingTitle");
    providerContentLayout->addWidget(providerListLabel);

    QLabel *providerListDesc = new QLabel(tr("双击服务商名称进入详细配置页面。"), providerContent);
    providerListDesc->setObjectName("settingDesc");
    providerListDesc->setWordWrap(true);
    providerContentLayout->addWidget(providerListDesc);

    providerContentLayout->addWidget(providerList);

    providerCard->addWidget(providerContent);

    // 双击服务商列表弹出配置对话框
    connect(providerList, &QListWidget::itemDoubleClicked, this, [this, settings, providerList, providers](QListWidgetItem *item) {
        QString providerKey = item->data(Qt::UserRole).toString();

        // 自定义服务商 → 打开自定义模型管理对话框
        if (providerKey == QStringLiteral("__custom__")) {
            QDialog dlg(this);
            dlg.setWindowTitle(tr("自定义服务商"));
            dlg.setMinimumSize(520, 420);

            QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
            dlgLayout->setContentsMargins(16, 16, 16, 16);
            dlgLayout->setSpacing(12);

            QListWidget *modelList = new QListWidget();
            modelList->setObjectName("customModelsList");

            // 加载函数
            auto loadModels = [&modelList]() {
                modelList->clear();
                QSettings s;
                QString json = s.value("ai/customModels", "").toString();
                if (json.isEmpty()) return;
                QJsonParseError pe;
                QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
                if (pe.error != QJsonParseError::NoError || !doc.isArray()) return;
                for (const QJsonValue &v : doc.array()) {
                    QJsonObject o = v.toObject();
                    QString name = o["displayName"].toString();
                    QString id = o["id"].toString();
                    QString url = o["apiUrl"].toString();
                    QListWidgetItem *li = new QListWidgetItem(QString("%1 (%2)").arg(name, id));
                    li->setData(Qt::UserRole, o);
                    li->setToolTip(url);
                    modelList->addItem(li);
                }
            };
            loadModels();

            dlgLayout->addWidget(modelList);

            QHBoxLayout *btnLayout = new QHBoxLayout();
            btnLayout->setSpacing(8);

            QPushButton *addBtn = new QPushButton(tr("添加"));
            btnLayout->addWidget(addBtn);

            QPushButton *editBtn = new QPushButton(tr("编辑"));
            btnLayout->addWidget(editBtn);

            QPushButton *delBtn = new QPushButton(tr("删除"));
            btnLayout->addWidget(delBtn);

            btnLayout->addStretch();

            QPushButton *importBtn = new QPushButton(tr("导入 JSON"));
            btnLayout->addWidget(importBtn);

            QPushButton *exportBtn = new QPushButton(tr("导出 JSON"));
            btnLayout->addWidget(exportBtn);

            dlgLayout->addLayout(btnLayout);

            // 添加
            connect(addBtn, &QPushButton::clicked, &dlg, [&, &modelList, &loadModels]() {
                QDialog addDlg(&dlg);
                addDlg.setWindowTitle(tr("添加自定义模型"));
                addDlg.setMinimumWidth(380);
                QFormLayout *fl = new QFormLayout(&addDlg);

                QLineEdit *nameEdit = new QLineEdit();
                nameEdit->setPlaceholderText(tr("显示名称"));
                fl->addRow(tr("名称:"), nameEdit);

                QLineEdit *idEdit = new QLineEdit();
                idEdit->setPlaceholderText(tr("模型 ID"));
                fl->addRow(tr("模型ID:"), idEdit);

                QLineEdit *urlEdit = new QLineEdit();
                urlEdit->setPlaceholderText(tr("API 地址"));
                fl->addRow(tr("API地址:"), urlEdit);

                QLineEdit *keyEdit = new QLineEdit();
                keyEdit->setEchoMode(QLineEdit::Password);
                keyEdit->setPlaceholderText(tr("输入 API Key"));
                fl->addRow(tr("API Key:"), keyEdit);

                QCheckBox *thinkingCheck = new QCheckBox(tr("推理模型"));
                fl->addRow(QString(), thinkingCheck);

                QPushButton *saveBtn = new QPushButton(tr("保存"));
                connect(saveBtn, &QPushButton::clicked, &addDlg, &QDialog::accept);
                fl->addRow(saveBtn);

                if (addDlg.exec() == QDialog::Accepted) {
                    QString name = nameEdit->text().trimmed();
                    QString id = idEdit->text().trimmed();
                    QString url = urlEdit->text().trimmed();
                    QString key = keyEdit->text().trimmed();
                    if (name.isEmpty() || id.isEmpty() || url.isEmpty()) return;

                    QSettings s;
                    QString json = s.value("ai/customModels", "").toString();
                    QJsonArray arr;
                    if (!json.isEmpty()) {
                        QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
                        if (d.isArray()) arr = d.array();
                    }
                    QJsonObject obj;
                    obj["id"] = id;
                    obj["displayName"] = name;
                    obj["apiUrl"] = url;
                    obj["apiKey"] = key;
                    obj["supportsThinking"] = thinkingCheck->isChecked();
                    arr.append(obj);
                    s.setValue("ai/customModels", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
                    loadModels();
                    // 更新外部列表项计数
                    QSettings cs;
                    QString cj = cs.value("ai/customModels", "").toString();
                    QJsonParseError cpe;
                    QJsonDocument cd = QJsonDocument::fromJson(cj.toUtf8(), &cpe);
                    int cnt = (cpe.error == QJsonParseError::NoError && cd.isArray()) ? cd.array().size() : 0;
                    QString txt = tr("自定义服务商");
                    if (cnt > 0) txt += QString("  (%1 %2)").arg(cnt).arg(tr("个模型"));
                    item->setText(txt);
                }
            });

            // 编辑
            connect(editBtn, &QPushButton::clicked, &dlg, [&, &modelList, &loadModels]() {
                QListWidgetItem *sel = modelList->currentItem();
                if (!sel) return;
                QJsonObject obj = sel->data(Qt::UserRole).toJsonObject();

                QDialog editDlg(&dlg);
                editDlg.setWindowTitle(tr("编辑自定义模型"));
                editDlg.setMinimumWidth(380);
                QFormLayout *fl = new QFormLayout(&editDlg);

                QLineEdit *nameEdit = new QLineEdit(obj["displayName"].toString());
                fl->addRow(tr("名称:"), nameEdit);

                QLineEdit *idEdit = new QLineEdit(obj["id"].toString());
                fl->addRow(tr("模型ID:"), idEdit);

                QLineEdit *urlEdit = new QLineEdit(obj["apiUrl"].toString());
                fl->addRow(tr("API地址:"), urlEdit);

                QLineEdit *keyEdit = new QLineEdit(obj["apiKey"].toString());
                keyEdit->setEchoMode(QLineEdit::Password);
                fl->addRow(tr("API Key:"), keyEdit);

                QCheckBox *thinkingCheck = new QCheckBox(tr("推理模型"));
                thinkingCheck->setChecked(obj["supportsThinking"].toBool());
                fl->addRow(QString(), thinkingCheck);

                QPushButton *saveBtn = new QPushButton(tr("保存"));
                connect(saveBtn, &QPushButton::clicked, &editDlg, &QDialog::accept);
                fl->addRow(saveBtn);

                if (editDlg.exec() == QDialog::Accepted) {
                    QString name = nameEdit->text().trimmed();
                    QString id = idEdit->text().trimmed();
                    QString url = urlEdit->text().trimmed();
                    QString key = keyEdit->text().trimmed();
                    if (name.isEmpty() || id.isEmpty() || url.isEmpty()) return;

                    QSettings s;
                    QString json = s.value("ai/customModels", "").toString();
                    QJsonArray arr;
                    if (!json.isEmpty()) {
                        QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
                        if (d.isArray()) arr = d.array();
                    }
                    for (int i = 0; i < arr.size(); ++i) {
                        QJsonObject e = arr[i].toObject();
                        if (e["id"].toString() == obj["id"].toString() && e["apiUrl"].toString() == obj["apiUrl"].toString()) {
                            QJsonObject u;
                            u["id"] = id;
                            u["displayName"] = name;
                            u["apiUrl"] = url;
                            u["apiKey"] = key;
                            u["supportsThinking"] = thinkingCheck->isChecked();
                            arr[i] = u;
                            break;
                        }
                    }
                    s.setValue("ai/customModels", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
                    loadModels();
                }
            });

            // 删除
            connect(delBtn, &QPushButton::clicked, &dlg, [&, &modelList, &loadModels, item]() {
                QListWidgetItem *sel = modelList->currentItem();
                if (!sel) return;
                QJsonObject obj = sel->data(Qt::UserRole).toJsonObject();

                QSettings s;
                QString json = s.value("ai/customModels", "").toString();
                QJsonArray arr;
                if (!json.isEmpty()) {
                    QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
                    if (d.isArray()) arr = d.array();
                }
                QJsonArray newArr;
                for (const QJsonValue &v : arr) {
                    QJsonObject e = v.toObject();
                    if (e["id"].toString() != obj["id"].toString() || e["apiUrl"].toString() != obj["apiUrl"].toString())
                        newArr.append(e);
                }
                s.setValue("ai/customModels", QString::fromUtf8(QJsonDocument(newArr).toJson(QJsonDocument::Compact)));
                loadModels();
                // 更新外部列表项计数
                QString cj = s.value("ai/customModels", "").toString();
                QJsonParseError cpe;
                QJsonDocument cd = QJsonDocument::fromJson(cj.toUtf8(), &cpe);
                int cnt = (cpe.error == QJsonParseError::NoError && cd.isArray()) ? cd.array().size() : 0;
                QString txt = tr("自定义服务商");
                if (cnt > 0) txt += QString("  (%1 %2)").arg(cnt).arg(tr("个模型"));
                item->setText(txt);
            });

            // 导入
            connect(importBtn, &QPushButton::clicked, &dlg, [&, &modelList, &loadModels, item]() {
                QString filePath = QFileDialog::getOpenFileName(&dlg, tr("导入模型配置"), QString(),
                    tr("JSON 文件 (*.json);;所有文件 (*)"));
                if (filePath.isEmpty()) return;

                QFile file(filePath);
                if (!file.open(QIODevice::ReadOnly)) return;
                QByteArray data = file.readAll();
                file.close();

                QJsonParseError pe;
                QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
                if (pe.error != QJsonParseError::NoError || !doc.isArray()) return;

                QSettings s;
                QString json = s.value("ai/customModels", "").toString();
                QJsonArray arr;
                if (!json.isEmpty()) {
                    QJsonDocument ed = QJsonDocument::fromJson(json.toUtf8());
                    if (ed.isArray()) arr = ed.array();
                }
                for (const QJsonValue &v : doc.array()) {
                    QJsonObject o = v.toObject();
                    QString id = o["id"].toString();
                    QString url = o["apiUrl"].toString();
                    if (id.isEmpty() || url.isEmpty()) continue;
                    bool exists = false;
                    for (const QJsonValue &e : arr) {
                        QJsonObject eo = e.toObject();
                        if (eo["id"].toString() == id && eo["apiUrl"].toString() == url) { exists = true; break; }
                    }
                    if (!exists) { o["isCustom"] = true; arr.append(o); }
                }
                s.setValue("ai/customModels", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
                loadModels();
                QString cj = s.value("ai/customModels", "").toString();
                QJsonParseError cpe;
                QJsonDocument cd = QJsonDocument::fromJson(cj.toUtf8(), &cpe);
                int cnt = (cpe.error == QJsonParseError::NoError && cd.isArray()) ? cd.array().size() : 0;
                QString txt = tr("自定义服务商");
                if (cnt > 0) txt += QString("  (%1 %2)").arg(cnt).arg(tr("个模型"));
                item->setText(txt);
            });

            // 导出
            connect(exportBtn, &QPushButton::clicked, &dlg, [&]() {
                QSettings s;
                QString json = s.value("ai/customModels", "").toString();
                if (json.isEmpty()) return;
                QString filePath = QFileDialog::getSaveFileName(&dlg, tr("导出模型配置"), QString(),
                    tr("JSON 文件 (*.json)"));
                if (filePath.isEmpty()) return;
                QFile file(filePath);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
                file.write(json.toUtf8());
                file.close();
            });

            dlg.exec();
            return;
        }

        // 内置服务商 → 配置对话框（包含模型选择 + 自定义模型 + Key 输入）
        QString providerName = item->data(Qt::UserRole + 1).toString();
        QString defaultUrl = item->data(Qt::UserRole + 2).toString();
        QString configKey = item->data(Qt::UserRole + 3).toString();

        QDialog dialog(this);
        dialog.setWindowTitle(tr("配置 %1").arg(providerName));
        dialog.setMinimumSize(520, 460);

        QVBoxLayout *dlgMainLayout = new QVBoxLayout(&dialog);
        dlgMainLayout->setContentsMargins(16, 16, 16, 16);
        dlgMainLayout->setSpacing(12);

        // 该服务商的模型列表
        QListWidget *modelList = new QListWidget();
        modelList->setObjectName("providerModelList");
        modelList->setMinimumHeight(150);

        QFile modelsFile(QStringLiteral(":/resources/ai_models.json"));
        if (modelsFile.open(QIODevice::ReadOnly)) {
            QJsonParseError pe;
            QJsonDocument doc = QJsonDocument::fromJson(modelsFile.readAll(), &pe);
            modelsFile.close();
            if (pe.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonArray arr = doc.object()["models"].toArray();
                for (const QJsonValue &v : arr) {
                    QJsonObject m = v.toObject();
                    QString apiUrl = m["apiUrl"].toString();
                    // 按服务商名称匹配 URL 或 displayName 前缀
                    bool match = false;
                    if (providerKey == "OpenAI") match = apiUrl.contains("openai.com");
                    else if (providerKey == "DeepSeek") match = apiUrl.contains("deepseek.com");
                    else if (providerKey == "Anthropic") match = apiUrl.contains("anthropic.com");
                    else if (providerKey == "Gemini") match = apiUrl.contains("googleapis.com");
                    else if (providerKey == "OpenRouter") match = apiUrl.contains("openrouter.ai");
                    else if (providerKey == "GLM") match = apiUrl.contains("bigmodel.cn");
                    else if (providerKey == "Qwen") match = apiUrl.contains("dashscope.aliyuncs");
                    else if (providerKey == "ERNIE") match = apiUrl.contains("baidubce.com");
                    if (!match) continue;

                    QString id = m["id"].toString();
                    QString displayName = m["displayName"].toString();
                    QListWidgetItem *li = new QListWidgetItem(displayName);
                    li->setData(Qt::UserRole, id);
                    li->setData(Qt::UserRole + 1, displayName);
                    li->setData(Qt::UserRole + 2, apiUrl);
                    li->setToolTip(id);
                    modelList->addItem(li);
                }
            }
        }

        QLabel *modelLabel = new QLabel(tr("选择模型:"));
        dlgMainLayout->addWidget(modelLabel);
        dlgMainLayout->addWidget(modelList);

        // 自定义模型输入行
        QHBoxLayout *customRow = new QHBoxLayout();
        customRow->setSpacing(6);
        QLineEdit *customModelEdit = new QLineEdit();
        customModelEdit->setPlaceholderText(tr("输入自定义模型 ID..."));
        customRow->addWidget(customModelEdit, 1);

        QPushButton *useCustomBtn = new QPushButton(tr("使用该模型"));
        useCustomBtn->setCursor(Qt::PointingHandCursor);
        useCustomBtn->setFixedWidth(90);
        customRow->addWidget(useCustomBtn);

        dlgMainLayout->addLayout(customRow);

        // API Key 输入
        QFormLayout *keyForm = new QFormLayout();
        keyForm->setSpacing(8);
        keyForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

        QLineEdit *keyEdit = new QLineEdit();
        keyEdit->setPlaceholderText(tr("API Key（可选）"));
        keyEdit->setEchoMode(QLineEdit::Password);
        QString savedKey = settings->getProperty(configKey + "/key", "").toString();
        keyEdit->setText(savedKey);
        keyForm->addRow(tr("API Key:"), keyEdit);
        dlgMainLayout->addLayout(keyForm);

        // 当前选中的模型信息
        QString selectedModelId;
        QString selectedModelUrl;
        QString selectedModelName;

        auto updateSelectedModel = [&](const QString &modelId, const QString &modelUrl, const QString &modelName) {
            selectedModelId = modelId;
            selectedModelUrl = modelUrl;
            selectedModelName = modelName;
        };

        // 点击列表项选中模型
        connect(modelList, &QListWidget::itemClicked, &dialog, [&](QListWidgetItem *li) {
            updateSelectedModel(
                li->data(Qt::UserRole).toString(),
                li->data(Qt::UserRole + 2).toString(),
                li->data(Qt::UserRole + 1).toString());
        });

        // 双击列表项也选中
        connect(modelList, &QListWidget::itemDoubleClicked, &dialog, [&](QListWidgetItem *li) {
            updateSelectedModel(
                li->data(Qt::UserRole).toString(),
                li->data(Qt::UserRole + 2).toString(),
                li->data(Qt::UserRole + 1).toString());
            dialog.accept();
        });

        // 自定义模型按钮
        connect(useCustomBtn, &QPushButton::clicked, &dialog, [&]() {
            QString customId = customModelEdit->text().trimmed();
            if (customId.isEmpty()) return;
            updateSelectedModel(customId, defaultUrl, providerName + " " + customId);
            dialog.accept();
        });

        // 按钮行
        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(8);
        btnLayout->addStretch();

        QPushButton *cancelBtn = new QPushButton(tr("取消"));
        cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);

        QPushButton *saveBtn = new QPushButton(tr("保存并使用"));
        saveBtn->setCursor(Qt::PointingHandCursor);
        connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
        btnLayout->addWidget(saveBtn);

        dlgMainLayout->addLayout(btnLayout);

        if (dialog.exec() == QDialog::Accepted) {
            settings->setProperty(configKey + "/url", selectedModelUrl.isEmpty() ? defaultUrl : selectedModelUrl);
            settings->setProperty(configKey + "/key", keyEdit->text().trimmed());
            settings->setProperty(configKey + "/selectedModel", selectedModelId);
            settings->setProperty(configKey + "/selectedModelName", selectedModelName);

            QString newKey = keyEdit->text().trimmed();
            QString displayText = providerName;
            if (!newKey.isEmpty()) {
                displayText += "  " + tr("(已配置)");
            }
            item->setText(displayText);
        }
    });

    // ── 联网搜索设置 ──
    QVBoxLayout *webSearchCard = createSettingsCard(layout, tr("联网搜索"));

    QComboBox *webSearchCombo = new QComboBox();
    webSearchCombo->addItems({tr("自动（AI 按需联网）"), tr("优先（AI 优先联网确认）"), tr("关闭")});
    disableWheelEffect(webSearchCombo);

    int savedWebSearchMode = settings->getProperty("ai/webSearchMode", 0).toInt();
    webSearchCombo->setCurrentIndex(savedWebSearchMode);

    connect(webSearchCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
        settings->setProperty("ai/webSearchMode", index);
    });

    QHBoxLayout *webSearchRow = appendSettingRow(webSearchCard,
        tr("联网模式"), tr("控制 AI 助手是否联网搜索信息。"),
        tr("联网搜索可获取最新信息，但会增加响应时间。"));
    webSearchRow->addWidget(webSearchCombo);

    // ── 深度思考设置 ──
    QVBoxLayout *deepThinkingCard = createSettingsCard(layout, tr("深度思考"));

    CustomCheckBox *deepThinkingCheck = new CustomCheckBox();
    bool savedDeepThinking = settings->getProperty("ai/deepThinking", false).toBool();
    deepThinkingCheck->setChecked(savedDeepThinking);
    connect(deepThinkingCheck, &CustomCheckBox::toggled, [=](bool checked) {
        settings->setProperty("ai/deepThinking", checked);
    });

    QHBoxLayout *deepThinkingRow = appendSettingRow(deepThinkingCard,
        tr("默认启用深度思考"), tr("开启后 AI 会先进行推理分析再回答，适合复杂问题。"),
        tr("深度思考会消耗更多 token，但能提供更准确、深入的回答。"), true);
    deepThinkingRow->addWidget(deepThinkingCheck);

    // ── 工作模式设置 ──
    QVBoxLayout *workModeCard = createSettingsCard(layout, tr("工作模式"));

    CustomCheckBox *agentToolsCheck = new CustomCheckBox();
    bool savedAgentTools = settings->getProperty("ai/agentTools", true).toBool();
    agentToolsCheck->setChecked(savedAgentTools);
    connect(agentToolsCheck, &CustomCheckBox::toggled, [=](bool checked) {
        settings->setProperty("ai/agentTools", checked);
    });

    QHBoxLayout *agentToolsRow = appendSettingRow(workModeCard,
        tr("启用 Agent 工具"), tr("允许 AI 助手在工作模式下执行本地工具（如查看实例、分析日志等）。"),
        tr("Agent 工具可让 AI 直接操作启动器功能，提高工作效率。"));
    agentToolsRow->addWidget(agentToolsCheck);

    CustomCheckBox *autoTitleCheck = new CustomCheckBox();
    bool savedAutoTitle = settings->getProperty("ai/autoTitle", true).toBool();
    autoTitleCheck->setChecked(savedAutoTitle);
    connect(autoTitleCheck, &CustomCheckBox::toggled, [=](bool checked) {
        settings->setProperty("ai/autoTitle", checked);
    });

    QHBoxLayout *autoTitleRow = appendSettingRow(workModeCard,
        tr("自动生成对话标题"), tr("对话结束后自动生成简洁的标题。"),
        tr("启用后可自动为对话创建有意义的标题，便于管理。"), true);
    autoTitleRow->addWidget(autoTitleCheck);

    // ── 上下文设置 ──
    QVBoxLayout *contextCard = createSettingsCard(layout, tr("上下文管理"));

    QComboBox *contextSizeCombo = new QComboBox();
    contextSizeCombo->addItems({tr("8K tokens"), tr("16K tokens"), tr("32K tokens"), tr("64K tokens"), tr("128K tokens")});
    disableWheelEffect(contextSizeCombo);

    int savedContextSize = settings->getProperty("ai/contextSize", 2).toInt();
    contextSizeCombo->setCurrentIndex(savedContextSize);

    connect(contextSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
        settings->setProperty("ai/contextSize", index);
    });

    QHBoxLayout *contextSizeRow = appendSettingRow(contextCard,
        tr("上下文大小"), tr("控制发送给 AI 的历史消息数量。"),
        tr("更大的上下文可让 AI 记住更多对话内容，但会消耗更多 token。"));
    contextSizeRow->addWidget(contextSizeCombo);

    CustomCheckBox *autoCompressCheck = new CustomCheckBox();
    bool savedAutoCompress = settings->getProperty("ai/autoCompress", true).toBool();
    autoCompressCheck->setChecked(savedAutoCompress);
    connect(autoCompressCheck, &CustomCheckBox::toggled, [=](bool checked) {
        settings->setProperty("ai/autoCompress", checked);
    });

    QHBoxLayout *autoCompressRow = appendSettingRow(contextCard,
        tr("自动压缩上下文"), tr("当对话历史过长时自动压缩，避免超出模型限制。"),
        tr("压缩会保留最近的消息并生成早期对话摘要，可显著延长对话长度。"), true);
    autoCompressRow->addWidget(autoCompressCheck);

    // ── 系统提示词设置 ──
    QVBoxLayout *systemPromptCard = createSettingsCard(layout, tr("系统提示词"));

    // 包装容器，提供左右内边距
    QWidget *spContent = new QWidget();
    spContent->setObjectName("providerCardContent");
    QVBoxLayout *spContentLayout = new QVBoxLayout(spContent);
    spContentLayout->setContentsMargins(24, 8, 24, 8);
    spContentLayout->setSpacing(8);

    // 从资源文件加载默认提示词（辅助 lambda）
    auto loadDefaultPrompt = [](const QString &key) -> QString {
        QFile file(QStringLiteral(":/resources/ai_system_prompts.json"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError pe;
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
            file.close();
            if (pe.error == QJsonParseError::NoError && doc.isObject())
                return doc.object().value(key).toString();
        }
        return QString();
    };

    // 聊天模式提示词
    QLabel *chatPromptSummary = new QLabel();
    chatPromptSummary->setObjectName("settingDesc");
    chatPromptSummary->setWordWrap(true);
    auto updateChatSummary = [chatPromptSummary, settings, loadDefaultPrompt]() {
        QString custom = settings->getProperty("ai/customSystemPrompt/chat", QString()).toString();
        QString text;
        if (custom.isEmpty()) {
            QString def = loadDefaultPrompt(QStringLiteral("chat"));
            text = def.left(50) + QStringLiteral("...") + tr(" (默认)");
        } else {
            text = custom.left(50) + QStringLiteral("...");
        }
        chatPromptSummary->setText(text);
    };
    updateChatSummary();

    QPushButton *chatPromptEditBtn = new QPushButton(tr("编辑"));
    chatPromptEditBtn->setCursor(Qt::PointingHandCursor);
    chatPromptEditBtn->setFixedWidth(80);
    connect(chatPromptEditBtn, &QPushButton::clicked, this, [this, settings, loadDefaultPrompt, updateChatSummary]() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("编辑聊天模式系统提示词"));
        dlg.setMinimumSize(600, 450);

        QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
        dlgLayout->setContentsMargins(16, 16, 16, 16);
        dlgLayout->setSpacing(12);

        QLabel *descLabel = new QLabel(tr("系统提示词定义了 AI 助手的角色、能力和行为规范。修改后将在下次对话时生效。"), &dlg);
        descLabel->setObjectName("settingDesc");
        descLabel->setWordWrap(true);
        dlgLayout->addWidget(descLabel);

        QTextEdit *textEdit = new QTextEdit(&dlg);
        textEdit->setObjectName("systemPromptEditor");
        QFont monoFont(QStringLiteral("Consolas"), 10);
        monoFont.setStyleHint(QFont::Monospace);
        textEdit->setFont(monoFont);
        textEdit->setAcceptRichText(false);
        QString custom = settings->getProperty("ai/customSystemPrompt/chat", QString()).toString();
        if (custom.isEmpty())
            textEdit->setPlainText(loadDefaultPrompt(QStringLiteral("chat")));
        else
            textEdit->setPlainText(custom);
        dlgLayout->addWidget(textEdit);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(8);

        QPushButton *resetBtn = new QPushButton(tr("恢复默认"), &dlg);
        resetBtn->setCursor(Qt::PointingHandCursor);
        connect(resetBtn, &QPushButton::clicked, &dlg, [settings, textEdit, loadDefaultPrompt]() {
            settings->setProperty("ai/customSystemPrompt/chat", QString());
            textEdit->setPlainText(loadDefaultPrompt(QStringLiteral("chat")));
        });
        btnLayout->addWidget(resetBtn);

        btnLayout->addStretch();

        QPushButton *cancelBtn = new QPushButton(tr("取消"), &dlg);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);

        QPushButton *saveBtn = new QPushButton(tr("保存"), &dlg);
        saveBtn->setCursor(Qt::PointingHandCursor);
        connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        btnLayout->addWidget(saveBtn);

        dlgLayout->addLayout(btnLayout);

        if (dlg.exec() == QDialog::Accepted) {
            QString defaultPrompt = loadDefaultPrompt(QStringLiteral("chat"));
            QString edited = textEdit->toPlainText().trimmed();
            if (edited == defaultPrompt)
                settings->setProperty("ai/customSystemPrompt/chat", QString());
            else
                settings->setProperty("ai/customSystemPrompt/chat", edited);
            updateChatSummary();
        }
    });

    QHBoxLayout *chatPromptRow = appendSettingRow(spContentLayout,
        tr("聊天模式提示词"), tr("定义 AI 助手在聊天模式下的角色和行为。"),
        tr("自定义提示词可让 AI 以特定风格或身份回复，修改后在新对话中生效。"));
    chatPromptRow->addWidget(chatPromptSummary, 1);
    chatPromptRow->addWidget(chatPromptEditBtn);

    // 工作模式提示词
    QLabel *workPromptSummary = new QLabel();
    workPromptSummary->setObjectName("settingDesc");
    workPromptSummary->setWordWrap(true);
    auto updateWorkSummary = [workPromptSummary, settings, loadDefaultPrompt]() {
        QString custom = settings->getProperty("ai/customSystemPrompt/work", QString()).toString();
        QString text;
        if (custom.isEmpty()) {
            QString def = loadDefaultPrompt(QStringLiteral("work"));
            text = def.left(50) + QStringLiteral("...") + tr(" (默认)");
        } else {
            text = custom.left(50) + QStringLiteral("...");
        }
        workPromptSummary->setText(text);
    };
    updateWorkSummary();

    QPushButton *workPromptEditBtn = new QPushButton(tr("编辑"));
    workPromptEditBtn->setCursor(Qt::PointingHandCursor);
    workPromptEditBtn->setFixedWidth(80);
    connect(workPromptEditBtn, &QPushButton::clicked, this, [this, settings, loadDefaultPrompt, updateWorkSummary]() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("编辑工作模式系统提示词"));
        dlg.setMinimumSize(600, 450);

        QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
        dlgLayout->setContentsMargins(16, 16, 16, 16);
        dlgLayout->setSpacing(12);

        QLabel *descLabel = new QLabel(tr("工作模式提示词定义了 AI 助手的工具调用能力和专业行为规范，包含 Agent 工具说明。"), &dlg);
        descLabel->setObjectName("settingDesc");
        descLabel->setWordWrap(true);
        dlgLayout->addWidget(descLabel);

        QTextEdit *textEdit = new QTextEdit(&dlg);
        textEdit->setObjectName("systemPromptEditor");
        QFont monoFont2(QStringLiteral("Consolas"), 10);
        monoFont2.setStyleHint(QFont::Monospace);
        textEdit->setFont(monoFont2);
        textEdit->setAcceptRichText(false);
        QString custom = settings->getProperty("ai/customSystemPrompt/work", QString()).toString();
        if (custom.isEmpty())
            textEdit->setPlainText(loadDefaultPrompt(QStringLiteral("work")));
        else
            textEdit->setPlainText(custom);
        dlgLayout->addWidget(textEdit);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(8);

        QPushButton *resetBtn = new QPushButton(tr("恢复默认"), &dlg);
        resetBtn->setCursor(Qt::PointingHandCursor);
        connect(resetBtn, &QPushButton::clicked, &dlg, [settings, textEdit, loadDefaultPrompt]() {
            settings->setProperty("ai/customSystemPrompt/work", QString());
            textEdit->setPlainText(loadDefaultPrompt(QStringLiteral("work")));
        });
        btnLayout->addWidget(resetBtn);

        btnLayout->addStretch();

        QPushButton *cancelBtn = new QPushButton(tr("取消"), &dlg);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);

        QPushButton *saveBtn = new QPushButton(tr("保存"), &dlg);
        saveBtn->setCursor(Qt::PointingHandCursor);
        connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        btnLayout->addWidget(saveBtn);

        dlgLayout->addLayout(btnLayout);

        if (dlg.exec() == QDialog::Accepted) {
            QString defaultPrompt = loadDefaultPrompt(QStringLiteral("work"));
            QString edited = textEdit->toPlainText().trimmed();
            if (edited == defaultPrompt)
                settings->setProperty("ai/customSystemPrompt/work", QString());
            else
                settings->setProperty("ai/customSystemPrompt/work", edited);
            updateWorkSummary();
        }
    });

    QHBoxLayout *workPromptRow = appendSettingRow(spContentLayout,
        tr("工作模式提示词"), tr("定义 AI 助手在工作模式下的工具调用能力和专业行为。"),
        tr("工作模式提示词包含 Agent 工具说明，修改前请确保了解各工具的用途。"), true);
    workPromptRow->addWidget(workPromptSummary, 1);
    workPromptRow->addWidget(workPromptEditBtn);

    systemPromptCard->addWidget(spContent);

    // ── 系统提示词库管理 ──
    QVBoxLayout *promptLibCard = createSettingsCard(layout, tr("系统提示词库"));

    // 包装容器，提供左右内边距
    QWidget *plContent = new QWidget();
    plContent->setObjectName("providerCardContent");
    QVBoxLayout *plContentLayout = new QVBoxLayout(plContent);
    plContentLayout->setContentsMargins(24, 8, 24, 8);
    plContentLayout->setSpacing(8);

    QLabel *promptLibHint = new QLabel(tr("管理系统提示词库，在 AI 聊天页新建对话时可从下拉框中选择使用（支持多选合并）。"), plContent);
    promptLibHint->setObjectName("settingDesc");
    promptLibHint->setWordWrap(true);
    plContentLayout->addWidget(promptLibHint);

    QListWidget *promptLibList = new QListWidget();
    promptLibList->setObjectName("providerList");
    promptLibList->setMinimumHeight(80);
    promptLibList->setMaximumHeight(180);

    // 加载提示词库
    auto loadPromptLibrary = [promptLibList, settings]() {
        promptLibList->clear();
        QString json = settings->getProperty("ai/systemPromptLibrary", QString()).toString();
        if (json.isEmpty()) return;
        QJsonParseError pe;
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isArray()) return;
        for (const QJsonValue &v : doc.array()) {
            QJsonObject o = v.toObject();
            QString name = o["name"].toString();
            QString content = o["content"].toString();
            QString summary = content.left(40).replace('\n', ' ');
            if (content.length() > 40) summary += QStringLiteral("...");
            QListWidgetItem *item = new QListWidgetItem(QStringLiteral("%1 — %2").arg(name, summary));
            item->setData(Qt::UserRole, o["id"].toString());
            item->setData(Qt::UserRole + 1, name);
            item->setData(Qt::UserRole + 2, content);
            promptLibList->addItem(item);
        }
    };
    loadPromptLibrary();

    plContentLayout->addWidget(promptLibList);

    // 添加/编辑/删除按钮行
    QHBoxLayout *promptLibBtnRow = new QHBoxLayout();
    promptLibBtnRow->setSpacing(8);

    QPushButton *addPromptBtn = new QPushButton(tr("添加"));
    addPromptBtn->setCursor(Qt::PointingHandCursor);
    promptLibBtnRow->addWidget(addPromptBtn);

    QPushButton *editPromptBtn = new QPushButton(tr("编辑"));
    editPromptBtn->setCursor(Qt::PointingHandCursor);
    promptLibBtnRow->addWidget(editPromptBtn);

    QPushButton *delPromptBtn = new QPushButton(tr("删除"));
    delPromptBtn->setCursor(Qt::PointingHandCursor);
    promptLibBtnRow->addWidget(delPromptBtn);

    promptLibBtnRow->addStretch();
    plContentLayout->addLayout(promptLibBtnRow);

    promptLibCard->addWidget(plContent);

    // 添加提示词
    connect(addPromptBtn, &QPushButton::clicked, this, [this, settings, promptLibList, loadPromptLibrary]() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("添加系统提示词"));
        dlg.setMinimumSize(520, 400);
        QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
        dlgLayout->setContentsMargins(16, 16, 16, 16);
        dlgLayout->setSpacing(12);

        QLineEdit *nameEdit = new QLineEdit();
        nameEdit->setPlaceholderText(tr("例如：翻译专家、红石工程师"));
        QFormLayout *nameForm = new QFormLayout();
        nameForm->addRow(tr("名称:"), nameEdit);
        dlgLayout->addLayout(nameForm);

        QTextEdit *contentEdit = new QTextEdit();
        contentEdit->setPlaceholderText(tr("输入系统提示词内容..."));
        QFont monoFont(QStringLiteral("Consolas"), 10);
        monoFont.setStyleHint(QFont::Monospace);
        contentEdit->setFont(monoFont);
        contentEdit->setAcceptRichText(false);
        dlgLayout->addWidget(contentEdit, 1);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        QPushButton *cancelBtn = new QPushButton(tr("取消"), &dlg);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);
        QPushButton *saveBtn = new QPushButton(tr("保存"), &dlg);
        saveBtn->setCursor(Qt::PointingHandCursor);
        connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        btnLayout->addWidget(saveBtn);
        dlgLayout->addLayout(btnLayout);

        if (dlg.exec() == QDialog::Accepted) {
            QString name = nameEdit->text().trimmed();
            QString content = contentEdit->toPlainText().trimmed();
            if (name.isEmpty() || content.isEmpty()) return;

            QString json = settings->getProperty("ai/systemPromptLibrary", QString()).toString();
            QJsonArray arr;
            if (!json.isEmpty()) {
                QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
                if (d.isArray()) arr = d.array();
            }
            QJsonObject obj;
            obj["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            obj["name"] = name;
            obj["content"] = content;
            arr.append(obj);
            settings->setProperty("ai/systemPromptLibrary",
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            loadPromptLibrary();
        }
    });

    // 编辑提示词
    connect(editPromptBtn, &QPushButton::clicked, this, [this, settings, promptLibList, loadPromptLibrary]() {
        QListWidgetItem *sel = promptLibList->currentItem();
        if (!sel) return;
        QString id = sel->data(Qt::UserRole).toString();
        QString oldName = sel->data(Qt::UserRole + 1).toString();
        QString oldContent = sel->data(Qt::UserRole + 2).toString();

        QDialog dlg(this);
        dlg.setWindowTitle(tr("编辑系统提示词"));
        dlg.setMinimumSize(520, 400);
        QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
        dlgLayout->setContentsMargins(16, 16, 16, 16);
        dlgLayout->setSpacing(12);

        QLineEdit *nameEdit = new QLineEdit(oldName);
        QFormLayout *nameForm = new QFormLayout();
        nameForm->addRow(tr("名称:"), nameEdit);
        dlgLayout->addLayout(nameForm);

        QTextEdit *contentEdit = new QTextEdit();
        contentEdit->setPlainText(oldContent);
        QFont monoFont2(QStringLiteral("Consolas"), 10);
        monoFont2.setStyleHint(QFont::Monospace);
        contentEdit->setFont(monoFont2);
        contentEdit->setAcceptRichText(false);
        dlgLayout->addWidget(contentEdit, 1);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        QPushButton *cancelBtn = new QPushButton(tr("取消"), &dlg);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);
        QPushButton *saveBtn = new QPushButton(tr("保存"), &dlg);
        saveBtn->setCursor(Qt::PointingHandCursor);
        connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        btnLayout->addWidget(saveBtn);
        dlgLayout->addLayout(btnLayout);

        if (dlg.exec() == QDialog::Accepted) {
            QString name = nameEdit->text().trimmed();
            QString content = contentEdit->toPlainText().trimmed();
            if (name.isEmpty() || content.isEmpty()) return;

            QString json = settings->getProperty("ai/systemPromptLibrary", QString()).toString();
            QJsonArray arr;
            if (!json.isEmpty()) {
                QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
                if (d.isArray()) arr = d.array();
            }
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject e = arr[i].toObject();
                if (e["id"].toString() == id) {
                    e["name"] = name;
                    e["content"] = content;
                    arr[i] = e;
                    break;
                }
            }
            settings->setProperty("ai/systemPromptLibrary",
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            loadPromptLibrary();
        }
    });

    // 删除提示词
    connect(delPromptBtn, &QPushButton::clicked, this, [this, settings, promptLibList, loadPromptLibrary]() {
        QListWidgetItem *sel = promptLibList->currentItem();
        if (!sel) return;
        QString id = sel->data(Qt::UserRole).toString();

        QString json = settings->getProperty("ai/systemPromptLibrary", QString()).toString();
        QJsonArray arr;
        if (!json.isEmpty()) {
            QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
            if (d.isArray()) arr = d.array();
        }
        QJsonArray newArr;
        for (const QJsonValue &v : arr) {
            QJsonObject e = v.toObject();
            if (e["id"].toString() != id)
                newArr.append(e);
        }
        settings->setProperty("ai/systemPromptLibrary",
            QString::fromUtf8(QJsonDocument(newArr).toJson(QJsonDocument::Compact)));
        loadPromptLibrary();
    });

    // ── 占位伸缩，保持内容顶部对齐 ──
    layout->addStretch();
}
