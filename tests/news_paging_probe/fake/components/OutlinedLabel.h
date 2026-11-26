/**
 * @file   OutlinedLabel.h（探针替身）
 * @brief  新闻分屏加载探针只需要同签名的普通 QLabel，描边效果与本探针无关
 */

#pragma once

#include <QLabel>

class OutlinedLabel : public QLabel
{
public:
  explicit OutlinedLabel(const QString &text = QString(), QWidget *parent = nullptr)
      : QLabel(text, parent)
  {
  }
};
