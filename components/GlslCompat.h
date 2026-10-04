/**
 * @file   GlslCompat.h
 * @brief  GLSL 版本兼容宏：桌面 OpenGL 3.3 Core / 移动端 OpenGL ES 3.0
 *
 * Android 与 HarmonyOS 上 Qt 走 OpenGL ES 后端，桌面 `#version 330 core`
 * 无法编译。GLSL 300 es 与 330 core 语法几乎一致（in/out、layout(location)、
 * texture() 均可用），差异只有版本头和片元着色器需要显式 float 精度声明，
 * 因此只需替换头部即可让同一份着色器在两端工作。
 * 需要配合 .pro 中 android 分支的 `DEFINES += QT_OPENGL_ES_2` 与
 * QSurfaceFormat::setRenderableType(OpenGLES) 使用。
 */
#pragma once

#include <QtGlobal>

#if defined(QT_OPENGL_ES_2) || defined(QT_OPENGL_ES_3) || defined(Q_OS_ANDROID) || defined(PLATFORM_ANDROID) || defined(HARMONY_OS)
#  define BLOCKBOX_GLES 1
   // 片元着色器：300 es 要求显式精度限定符
#  define BLOCKBOX_GLSL_VS_HEADER "#version 300 es\n"
#  define BLOCKBOX_GLSL_FS_HEADER "#version 300 es\nprecision mediump float;\nprecision mediump int;\n"
#else
#  define BLOCKBOX_GLES 0
#  define BLOCKBOX_GLSL_VS_HEADER "#version 330 core\n"
#  define BLOCKBOX_GLSL_FS_HEADER "#version 330 core\n"
#endif
