/**
 * @file   ModDisplay.h
 * @brief  模组显示名多语言选择器(统一各页面的名称优先级逻辑)
 * @author BlockBox Team
 * @date   2026-08-29
 *
 * 模组名有中文名(chineseName,来自 MCModAPI)与英文名(englishName/name,
 * JAR 元数据或在线 API 原始名)两套。本选择器按当前界面语言决定主/副标题:
 *   - 中文界面:主标题 = 中文名,副标题 = 英文名;
 *   - 英文/西语界面:主标题 = 英文名,副标题 = 中文名。
 * 全部内联、无状态,语言切换后调用方刷新显示即按新语言取值。
 */

#ifndef MODDISPLAY_H
#define MODDISPLAY_H

#include "ModData.h"
#include "../LanguageManager.h"

namespace ModDisplay {

/** 当前界面语言是否以中文为主(简体/繁体) */
inline bool preferChinese()
{
    switch (LanguageManager::instance()->currentLanguage())
    {
    case LanguageManager::Chinese:
    case LanguageManager::ChineseTraditional:
        return true;
    case LanguageManager::English:
    case LanguageManager::Spanish:
        return false;
    }
    return true;
}

/** 英文名(englishName 优先,缺省用原始 name) */
inline QString englishNameOf(const ModInfo &info)
{
    return info.englishName.isEmpty() ? info.name : info.englishName;
}

/** 模组主要显示名(跟随界面语言);两套名均空时回退 fallback/文件名 */
inline QString primaryName(const ModInfo &info, const QString &fallback = QString())
{
    const QString cn = info.chineseName;
    const QString en = englishNameOf(info);
    if (preferChinese())
    {
        if (!cn.isEmpty())
            return cn;
        if (!en.isEmpty())
            return en;
    }
    else
    {
        if (!en.isEmpty())
            return en;
        if (!cn.isEmpty())
            return cn;
    }
    if (!fallback.isEmpty())
        return fallback;
    return info.name.isEmpty() ? info.fileName : info.name;
}

/** 模组副标题:与主名互补的另一语言名(相同或缺失返回空) */
inline QString secondaryName(const ModInfo &info)
{
    const QString cn = info.chineseName;
    const QString en = englishNameOf(info);
    if (preferChinese())
        return (!en.isEmpty() && en != cn) ? en : QString();
    return (!cn.isEmpty() && cn != en) ? cn : QString();
}

/** 显示名便捷封装(等价 primaryName,不含文件名回退外的自定义) */
inline QString displayName(const ModInfo &info)
{
    return primaryName(info);
}

/** 文件名去 .jar / .disabled 后缀 */
inline QString fileStem(const ModInfo &info)
{
    QString stem = info.fileName;
    if (stem.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
        stem.chop(9);
    if (stem.endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive))
        stem.chop(4);
    return stem;
}

} // namespace ModDisplay

#endif // MODDISPLAY_H
