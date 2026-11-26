/**
 * @file   NewsDetailDialog.h（探针替身）
 * @brief  详情弹窗在探针里只记录点击，不真弹窗
 */

#pragma once

class QWidget;
struct NewsEntry;

void showNewsDetailDialog(QWidget *parent, const NewsEntry &entry);
