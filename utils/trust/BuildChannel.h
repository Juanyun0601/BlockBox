/**
 * @file   BuildChannel.h
 * @brief  构建渠道标识：区分「官方构建」与「非官方构建」
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 官方构建由发布脚本在编译时传入 BLOCKBOX_OFFICIAL_BUILD 宏
 * （见 BlockBox.pro，打包脚本会追加 DEFINES+=BLOCKBOX_OFFICIAL_BUILD）；
 * 本地开发、第三方二次打包都不会带这个宏，一律按「非官方构建」处理：
 *   - 主窗口标题追加「［非官方构建 · 风险自担］」，启动后提示一次风险；
 *   - 「设置 → 关于」里用告警色显示构建标识。
 *
 * 这里刻意用编译期宏而不是运行时文件校验：宏在打包脚本里完全可控，
 * 而文件校验的结果依赖网络清单，不能用来判断"自己是不是官方发布"。
 */
#pragma once

#include <QCoreApplication>
#include <QString>

namespace BuildChannel {

/** 是否官方构建（编译期决定，运行期不会变） */
inline bool isOfficialBuild()
{
#ifdef BLOCKBOX_OFFICIAL_BUILD
    return true;
#else
    return false;
#endif
}

/** 构建标识文案：官方构建 / 非官方构建 */
inline QString buildLabel()
{
    return isOfficialBuild()
               ? QCoreApplication::translate("BuildChannel", "官方构建")
               : QCoreApplication::translate("BuildChannel", "非官方构建");
}

/** 构建标识的说明文案（关于页「构建标识」一行的描述） */
inline QString buildDescription()
{
    if (isOfficialBuild())
        return QCoreApplication::translate(
            "BuildChannel",
            "当前版本由方块盒子官方打包发布，可在可信校验页核对安装包校验值。");
    return QCoreApplication::translate(
        "BuildChannel",
        "此版本不是官方发布，程序文件可能已被第三方修改，请从 mcblockbox.top 下载官方版本。");
}

/** 主窗口标题后缀（官方构建返回空串，不改变原有标题） */
inline QString windowTitleSuffix()
{
    return isOfficialBuild() ? QString()
                             : QStringLiteral("［非官方构建 · 风险自担］");
}

/** 启动后一次性风险提示的正文 */
inline QString riskNoticeText()
{
    return QCoreApplication::translate(
        "BuildChannel",
        "当前运行的是非官方构建：此版本不是官方发布，内容可能被第三方修改。"
        "为安全起见，请前往 mcblockbox.top 下载官方版本。");
}

} // namespace BuildChannel
