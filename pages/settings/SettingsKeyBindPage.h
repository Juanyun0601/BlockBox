/**
 * @file   SettingsKeyBindPage.h
 * @brief  按键绑定设置页
 * @author BlockBox Team
 * @date   2026-06-26
 */
#ifndef SETTINGSKEYBINDPAGE_H
#define SETTINGSKEYBINDPAGE_H

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include <QEvent>
#include <QKeyEvent>

/**
 * @brief 单个按键绑定项的数据结构
 */
struct KeyBindItem
{
  QString id;           // 唯一标识
  QString displayName;  // 显示名称
  QString defaultKey;   // 默认快捷键
  QString currentKey;   // 当前快捷键
};

/**
 * @brief 按键绑定行控件
 */
class KeyBindRow : public QWidget
{
  Q_OBJECT

public:
  explicit KeyBindRow(const KeyBindItem &item, QWidget *parent = nullptr);

  QString id() const { return m_item.id; }
  QString currentKey() const { return m_item.currentKey; }
  void setCurrentKey(const QString &key);
  void resetToDefault();

signals:
  void keyChanged(const QString &id, const QString &newKey);
  void captureStarted();
  void captureFinished();

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
  void onBindClicked();

private:
  void updateDisplay();
  void startKeyCapture();
  void stopKeyCapture();

  KeyBindItem m_item;
  QLabel *m_nameLabel;
  QLabel *m_keyLabel;
  QPushButton *m_bindButton;
  QPushButton *m_resetButton;
  bool m_capturing;
};

/**
 * @brief 按键绑定设置页
 */
class SettingsKeyBindPage : public QWidget
{
  Q_OBJECT

public:
  explicit SettingsKeyBindPage(QWidget *parent = nullptr);
  ~SettingsKeyBindPage();

  static QVector<KeyBindItem> defaultBindings();
  void loadBindings();
  void saveBindings();
  void resetAllToDefault();

signals:
  void bindingsChanged();
  void captureStarted();
  void captureFinished();

private:
  void initUI();

  QVBoxLayout *m_mainLayout;
  QVector<KeyBindRow *> m_rows;
};

#endif // SETTINGSKEYBINDPAGE_H