/**
 * @file   LanguageManager.h
 * @brief  多语言管理器类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QApplication>
#include <QFuture>
#include <QMutex>
#include <QObject>
#include <QSettings>
#include <QtConcurrent>
#include <QTranslator>

class LanguageManager : public QObject
{
    Q_OBJECT

public:
    enum Language {
        Chinese,
        ChineseTraditional,
        English,
        Spanish
    };

    explicit LanguageManager(QObject *parent = nullptr);
    ~LanguageManager();

    static LanguageManager *instance();
    void setLanguage(Language lang);
    Language currentLanguage() const;

    // 仅持久化语言偏好（写入 QSettings），不安装翻译器、不发送 languageChanged。
    // 供首次引导"语言设置"步骤使用：选定后重启应用，由下次启动 loadLanguageSync() 干净加载，
    // 避免在已构建的向导上触发 LanguageChange 实时重译导致崩溃。
    void setLanguagePreference(Language lang);

    // 同步加载语言——在创建任何 UI 之前调用，确保 tr() 使用正确的翻译
    void loadLanguageSync();

    // 异步初始化
    QFuture<void> initializeAsync();

    // 检查是否已初始化完成
    bool isInitialized() const;

signals:
    // 初始化完成信号
    void initialized();

    // 语言已变更（UI 可连接此信号执行 retranslateUi）
    void languageChanged();

private:
    static LanguageManager *m_instance;
    QTranslator *m_translator;
    Language m_currentLanguage;

    // 线程安全
    mutable QMutex m_mutex;
    bool m_initialized;

    void setLanguageInternal(Language lang);
};

#endif // LANGUAGEMANAGER_H
