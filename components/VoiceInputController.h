/**
 * @file   VoiceInputController.h
 * @brief  AI 助手语音输入控制器（录音 + 语音转写）
 * @author BlockBox Team
 * @date   2026-09-12
 */

#ifndef VOICEINPUTCONTROLLER_H
#define VOICEINPUTCONTROLLER_H

#include <QAudioFormat>
#include <QBuffer>
#include <QObject>
#include <QString>

class QAudioSource;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/**
 * @brief 语音输入控制器：麦克风录音 → OpenAI 兼容转写接口
 *
 * 录音采用 16kHz 单声道 16bit PCM（设备不支持时取最近可用格式），停止后打包为
 * WAV，以 multipart/form-data 上传到所配置的转写端点，解析 JSON 响应中的
 * "text" 字段作为识别结果。
 *
 * 协议兼容 OpenAI Whisper API（/v1/audio/transcriptions），因此以下服务均可使用：
 *  - SiliconFlow（默认）：FunAudioLLM/SenseVoiceSmall，中文识别效果好且有免费额度
 *  - OpenAI：whisper-1 / gpt-4o-transcribe
 *  - Groq / DeepInfra 等托管 Whisper
 *  - 本地 whisper.cpp / faster-whisper-server（API Key 随意填写即可）
 *
 * 状态机：Idle → Recording（点击停止后）→ Transcribing → Idle。
 */
class VoiceInputController : public QObject
{
    Q_OBJECT

public:
    explicit VoiceInputController(QObject *parent = nullptr);
    ~VoiceInputController() override;

    /** 控制器状态 */
    enum State
    {
        Idle,          ///< 空闲
        Recording,     ///< 录音中
        Transcribing   ///< 上传转写中
    };
    Q_ENUM(State)

    /** 转写服务配置（OpenAI 兼容 /v1/audio/transcriptions 端点） */
    struct Config
    {
        QString apiUrl;   // 完整转写端点 URL
        QString model;    // 模型 ID，如 FunAudioLLM/SenseVoiceSmall、whisper-1
        QString apiKey;   // Bearer 密钥（本地服务可随意填写）

        /** 从 QSettings("BlockBox","BlockBox") 的 voice/* 键读取，缺省为 SiliconFlow */
        static Config load();

        /** 保存到 QSettings("BlockBox","BlockBox") 的 voice/* 键 */
        void save() const;

        /** 是否已具备发起转写的最小条件 */
        bool isConfigured() const { return !apiUrl.isEmpty() && !apiKey.isEmpty(); }
    };

    State state() const { return m_state; }

public slots:
    /**
     * @brief 开始录音
     * @return 成功返回 true；设备缺失/打开失败返回 false（错误经 errorOccurred 抛出）
     */
    bool startRecording();

    /** 停止录音并上传转写（仅在 Recording 状态有效） */
    void stopAndTranscribe();

    /** 放弃当前录音或取消进行中的转写请求，回到 Idle 且不产生结果 */
    void cancel();

signals:
    /** 状态切换（用于刷新按钮图标与提示） */
    void stateChanged(VoiceInputController::State state);

    /** 转写成功，text 为识别出的文本（已 trim） */
    void transcriptionFinished(const QString &text);

    /** 录音或转写失败，message 为可直接展示给用户的描述 */
    void errorOccurred(const QString &message);

private slots:
    void onTranscribeReplyFinished();

private:
    void setState(State state);
    QByteArray buildWav(const QByteArray &pcm) const;
    void upload(const QByteArray &wav);
    void cleanupAudio();

    QAudioSource *m_audioSource = nullptr;
    QBuffer *m_pcmBuffer = nullptr;     // 录音期间累积原始 PCM 数据
    QAudioFormat m_format;              // 实际生效的音频格式（写 WAV 头用）
    QTimer *m_maxDurationTimer = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    Config m_pendingConfig;             // 发起录音时的配置快照（上传时使用）
    State m_state = Idle;
};

#endif // VOICEINPUTCONTROLLER_H
