/**
 * @file   SettingsKeyBindPage.cpp
 * @brief  按键绑定设置页实现
 * @author BlockBox Team
 * @date   2026-06-26
 */
#include "SettingsKeyBindPage.h"

#include "components/OutlinedLabel.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMap>
#include <QScrollArea>

#include "utils/SettingsManager.h"

// ========== KeyBindRow ==========

KeyBindRow::KeyBindRow(const KeyBindItem &item, QWidget *parent)
  : QWidget(parent)
  , m_item(item)
  , m_capturing(false)
{
  setObjectName("settingRow");
  setAttribute(Qt::WA_StyledBackground, true);

  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(24, 8, 24, 8);
  layout->setSpacing(12);

  m_nameLabel = new QLabel(item.displayName, this);
  m_nameLabel->setObjectName("keyBindNameLabel");

  m_keyLabel = new QLabel(item.currentKey, this);
  m_keyLabel->setObjectName("keyBindKeyLabel");
  m_keyLabel->setMinimumWidth(100);
  m_keyLabel->setAlignment(Qt::AlignCenter);

  m_bindButton = new QPushButton(tr("绑定"), this);
  m_bindButton->setObjectName("keyBindButton");
  m_bindButton->setFixedWidth(60);
  connect(m_bindButton, &QPushButton::clicked, this, &KeyBindRow::onBindClicked);

  m_resetButton = new QPushButton(tr("默认"), this);
  m_resetButton->setObjectName("keyBindResetButton");
  m_resetButton->setFixedWidth(50);
  connect(m_resetButton, &QPushButton::clicked, this, &KeyBindRow::resetToDefault);

  layout->addWidget(m_nameLabel);
  layout->addStretch();
  layout->addWidget(m_keyLabel);
  layout->addWidget(m_bindButton);
  layout->addWidget(m_resetButton);

  setFocusPolicy(Qt::StrongFocus);
  installEventFilter(this);
}

void KeyBindRow::setCurrentKey(const QString &key)
{
  m_item.currentKey = key;
  updateDisplay();
}

void KeyBindRow::resetToDefault()
{
  m_item.currentKey = m_item.defaultKey;
  updateDisplay();
  emit keyChanged(m_item.id, m_item.currentKey);
}

void KeyBindRow::onBindClicked()
{
  if (m_capturing)
  {
    stopKeyCapture();
  }
  else
  {
    startKeyCapture();
  }
}

void KeyBindRow::startKeyCapture()
{
  m_capturing = true;
  m_bindButton->setText(tr("..."));
  m_keyLabel->setText(tr("按下按键..."));
  m_keyLabel->setStyleSheet("QLabel { color: #4CAF50; font-weight: bold; }");
  setFocus();
  grabKeyboard();
  emit captureStarted();
}

void KeyBindRow::stopKeyCapture()
{
  m_capturing = false;
  releaseKeyboard();
  m_bindButton->setText(tr("绑定"));
  m_keyLabel->setStyleSheet("");
  updateDisplay();
  emit captureFinished();
}

void KeyBindRow::updateDisplay()
{
  m_keyLabel->setText(m_item.currentKey);
}

bool KeyBindRow::eventFilter(QObject *obj, QEvent *event)
{
  Q_UNUSED(obj);
  if (m_capturing && event->type() == QEvent::KeyPress)
  {
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
    int key = keyEvent->key();

    // Ignore standalone modifier keys
    if (key == Qt::Key_Control || key == Qt::Key_Shift ||
        key == Qt::Key_Alt || key == Qt::Key_Meta)
    {
      return true;
    }

    QKeySequence seq(keyEvent->keyCombination());
    QString keyStr = seq.toString();

    if (!keyStr.isEmpty())
    {
      m_item.currentKey = keyStr;
      stopKeyCapture();
      updateDisplay();
      emit keyChanged(m_item.id, m_item.currentKey);
    }
    return true;
  }
  return QWidget::eventFilter(obj, event);
}

// ========== SettingsKeyBindPage ==========

SettingsKeyBindPage::SettingsKeyBindPage(QWidget *parent)
  : QWidget(parent)
{
  initUI();
  loadBindings();
}

SettingsKeyBindPage::~SettingsKeyBindPage()
{
}

QVector<KeyBindItem> SettingsKeyBindPage::defaultBindings()
{
  return {
    {"nav_home",       tr("切换到首页"),     "Ctrl+1", "Ctrl+1"},
    {"nav_resources",  tr("切换到资源页"),   "Ctrl+2", "Ctrl+2"},
    {"nav_settings",   tr("切换到设置页"),   "Ctrl+3", "Ctrl+3"},
    {"nav_ai",         tr("切换到AI助手"),   "Ctrl+4", "Ctrl+4"},
    {"search",         tr("搜索"),           "Ctrl+F", "Ctrl+F"},
    {"tasks",          tr("任务列表"),       "Ctrl+T", "Ctrl+T"},
    {"instance_select",tr("实例选择"),       "Ctrl+E", "Ctrl+E"},
    {"back",           tr("返回上一级"),     "Esc",    "Esc"},
    {"home",           tr("返回主页"),       "Home",   "Home"},
    {"toggle_maximize",tr("切换最大化"),     "F11",    "F11"},
    {"quit",           tr("退出程序"),       "Ctrl+Q", "Ctrl+Q"},
    {"launch",         tr("启动游戏"),       "Ctrl+L", "Ctrl+L"},
    {"mod_download",   tr("模组下载"),       "Ctrl+D", "Ctrl+D"},
    {"install_instance",tr("安装新实例"),    "Ctrl+I", "Ctrl+I"},
    {"instance_manage",tr("实例管理"),       "Ctrl+M", "Ctrl+M"},
    {"refresh",        tr("刷新"),           "F5",     "F5"},
    {"edition_switch", tr("切换Java/基岩版"), "Ctrl+Shift+E", "Ctrl+Shift+E"},
  };
}

void SettingsKeyBindPage::initUI()
{
  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setContentsMargins(20, 0, 20, 20);
  m_mainLayout->setSpacing(0);

  OutlinedLabel *titleLabel = new OutlinedLabel(tr("按键绑定"), this);
  titleLabel->setObjectName("sectionTitle");
  m_mainLayout->addWidget(titleLabel);

  QLabel *descLabel = new QLabel(tr("点击「绑定」按钮后按下新按键即可重新绑定快捷键"), this);
  descLabel->setObjectName("keyBindDesc");
  m_mainLayout->addWidget(descLabel);

  // Scroll area for the bind list
  QScrollArea *scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  QWidget *scrollContent = new QWidget(scrollArea);
  QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);
  scrollLayout->setContentsMargins(0, 0, 0, 0);
  scrollLayout->setSpacing(2);

  QFrame *bindCard = new QFrame(scrollContent);
  bindCard->setObjectName("settingsCard");
  bindCard->setFrameShape(QFrame::NoFrame);
  QVBoxLayout *bindLayout = new QVBoxLayout(bindCard);
  bindLayout->setContentsMargins(0, 0, 0, 0);
  bindLayout->setSpacing(0);

  auto defaults = defaultBindings();
  for (int i = 0; i < defaults.size(); ++i)
  {
    const auto &item = defaults[i];
    KeyBindRow *row = new KeyBindRow(item, bindCard);
    if (i == defaults.size() - 1) {
      row->setProperty("lastRow", true);
    }
    connect(row, &KeyBindRow::keyChanged, this, [this](const QString &, const QString &) {
      saveBindings();
      emit bindingsChanged();
    });
    connect(row, &KeyBindRow::captureStarted, this, &SettingsKeyBindPage::captureStarted);
    connect(row, &KeyBindRow::captureFinished, this, &SettingsKeyBindPage::captureFinished);
    m_rows.append(row);
    bindLayout->addWidget(row);
  }

  scrollLayout->addWidget(bindCard);
  scrollLayout->addStretch();
  scrollArea->setWidget(scrollContent);

  m_mainLayout->addWidget(scrollArea, 1);

  // Reset all button
  QPushButton *resetAllBtn = new QPushButton(tr("恢复默认值"), this);
  resetAllBtn->setObjectName("restoreDefaultsBtn");
  connect(resetAllBtn, &QPushButton::clicked, this, [this]() {
    resetAllToDefault();
    saveBindings();
    emit bindingsChanged();
  });
  m_mainLayout->addWidget(resetAllBtn, 0, Qt::AlignRight);
}

void SettingsKeyBindPage::loadBindings()
{
  QVector<KeyBindItem> defaults = defaultBindings();

  QString jsonStr = SettingsManager::instance()->getProperty("keyBindings").toString();
  if (jsonStr.isEmpty())
  {
    // Use defaults
    for (int i = 0; i < m_rows.size() && i < defaults.size(); ++i)
    {
      m_rows[i]->setCurrentKey(defaults[i].defaultKey);
    }
    return;
  }

  QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
  if (!doc.isArray())
  {
    return;
  }

  QJsonArray arr = doc.array();
  QMap<QString, QString> savedMap;
  for (const auto &val : arr)
  {
    QJsonObject obj = val.toObject();
    savedMap[obj["id"].toString()] = obj["key"].toString();
  }

  for (auto *row : m_rows)
  {
    QString key = savedMap.value(row->id(), QString());
    if (!key.isEmpty())
    {
      row->setCurrentKey(key);
    }
    else
    {
      // Find default
      for (const auto &d : defaults)
      {
        if (d.id == row->id())
        {
          row->setCurrentKey(d.defaultKey);
          break;
        }
      }
    }
  }
}

void SettingsKeyBindPage::saveBindings()
{
  QJsonArray arr;
  for (auto *row : m_rows)
  {
    QJsonObject obj;
    obj["id"] = row->id();
    obj["key"] = row->currentKey();
    arr.append(obj);
  }

  QJsonDocument doc(arr);
  SettingsManager::instance()->setProperty("keyBindings", QString(doc.toJson(QJsonDocument::Compact)));
}

void SettingsKeyBindPage::resetAllToDefault()
{
  QVector<KeyBindItem> defaults = defaultBindings();
  for (int i = 0; i < m_rows.size() && i < defaults.size(); ++i)
  {
    m_rows[i]->resetToDefault();
  }
}