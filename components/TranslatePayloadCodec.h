/**
 * @file   TranslatePayloadCodec.h
 * @brief  列表页整页翻译的「请求 / 响应」格式编解码
 *
 * 约定格式：每行 `[#序号#] 名称 ||| 简介`。模型偶尔会加 ``` 包裹、
 * 省掉 `|||`、或写成「名称：… 简介：…」，解析时都要尽量兜住。
 *
 * 单独成文件的目的：不依赖 Qt Widgets 与网络，方便探针直接链接真实实现做回归。
 */

#pragma once

#include <QMap>
#include <QPair>
#include <QString>

namespace TranslatePayloadCodec {

/**
 * @brief 解析模型返回的批量翻译文本
 * @param text 模型原始输出
 * @return key = `[#n#]` 里的序号，value = (译名, 译简介)；解析不到任何条目时返回空
 */
QMap<int, QPair<QString, QString>> parse(const QString &text);

} // namespace TranslatePayloadCodec
