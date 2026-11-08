#ifndef STUB_LANGUAGEMANAGER_H
#define STUB_LANGUAGEMANAGER_H

// 探针用桩：只提供 AppMessageBox 用到的语言枚举与实例接口。
#include <QString>

class LanguageManager
{
public:
    enum Language {
        Chinese,
        ChineseTraditional,
        English,
        Spanish,
        Japanese,
        Russian
    };

    static LanguageManager *instance();
    Language currentLanguage() const;
};

#endif // STUB_LANGUAGEMANAGER_H
