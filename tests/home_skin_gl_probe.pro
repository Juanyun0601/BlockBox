# 首页皮肤预览透明背景合成验证探针
QT       += core gui widgets opengl openglwidgets
CONFIG   += c++17
TEMPLATE  = app
INCLUDEPATH += .. ../components ../utils
HEADERS   += ../components/Skin3DWidget.h \
             ../utils/SkinEditorDocument.h
SOURCES   += ../components/Skin3DWidget.cpp \
             ../utils/SkinEditorDocument.cpp \
             home_skin_gl_probe.cpp
