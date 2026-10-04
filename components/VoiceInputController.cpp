/**
 * @file   VoiceInputController.cpp
 * @brief  AI 助手语音输入控制器实现（录音 + 语音转写）
 * @author BlockBox Team
 * @date   2026-09-12
 */

#include "VoiceInputController.h"

#include <QAudio>
#include <QAudioFormat>
#include <QAudioSource>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>

namespace
{
// 录音上限：避免误触后长时间占用麦克风、也控制转写耗时
constexpr int kMaxRecordingMs = 60 * 1000;
// 低于该字节数视为没有有效语音（16kHz/16bit/单声道下约 100ms）
constexpr qint64 kMinPcmBytes = 3200;

// 默认使用 SiliconFlow 的 SenseVoice：OpenAI 协议兼容、中文效果好、有免费额度
constexpr const char *kDefaultApiUrl = "https://api.siliconflow.cn/v1/audio/transcriptions";
constexpr const char *kDefaultModel = "FunAudioLLM/SenseVoiceSmall";

void appendU32(QByteArray *out, quint32 v)
{
    out->append(char(v & 0xFF));
    out->append(char((v >> 8) & 0xFF));
    out->append(char((v >> 16) & 0xFF));
    out->append(char((v >> 24) & 0xFF));
}

void appendU16(QByteArray *out, quint16 v)
{
    out->append(char(v & 0xFF));
    out->append(char((v >> 8) & 0xFF));
}
} // namespace

VoiceInputController::VoiceInputController(QObject *parent)
    : QObject(parent)
{
    m_maxDurationTimer = new QTimer(this);
    m_maxDurationTimer->setSingleShot(true);
    connect(m_maxDurationTimer, &QTimer::timeout, this, &VoiceInputController::stopAndTranscribe);
}

VoiceInputController::~VoiceInputController()
{
    cancel();
}

VoiceInputController::Config VoiceInputController::Config::load()
{
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    Config cfg;
    cfg.apiUrl = settings.value(QStringLiteral("voice/apiUrl"), QString::fromUtf8(kDefaultApiUrl)).toString();
    cfg.model = settings.value(QStringLiteral("voice/model"), QString::fromUtf8(kDefaultModel)).toString();
    cfg.apiKey = settings.value(QStringLiteral("voice/apiKey")).toString();
    return cfg;
}

void VoiceInputController::Config::save() const
{
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    settings.setValue(QStringLiteral("voice/apiUrl"), apiUrl);
    settings.setValue(QStringLiteral("voice/model"), model);
    settings.setValue(QStringLiteral("voice/apiKey"), apiKey);
}

bool VoiceInputController::startRecording()
{
    if (m_state != Idle)
    {
        return false;
    }

    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull())
    {
        emit errorOccurred(tr("未检测到可用的麦克风设备，请检查系统音频输入设置。"));
        return false;
    }

    // 优先 16kHz 单声道 16bit（转写接口的标准格式），设备不支持时改用其首选格式
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format))
    {
        format = device.preferredFormat();
    }

    m_pendingConfig = Config::load();

    m_pcmBuffer = new QBuffer(this);
    if (!m_pcmBuffer->open(QIODevice::WriteOnly))
    {
        delete m_pcmBuffer;
        m_pcmBuffer = nullptr;
        emit errorOccurred(tr("麦克风缓冲区初始化失败。"));
        return false;
    }

    m_audioSource = new QAudioSource(device, format, this);
    m_format = m_audioSource->format(); // Qt 可能按设备能力调整，WAV 头以实际格式为准
    m_audioSource->start(m_pcmBuffer);

    if (m_audioSource->error() != QAudio::NoError)
    {
        cleanupAudio();
        emit errorOccurred(tr("麦克风打开失败（错误码 %1），请确认麦克风可用且未被其他程序占用。")
                               .arg(int(m_audioSource->error())));
        return false;
    }

    m_maxDurationTimer->start(kMaxRecordingMs);
    setState(Recording);
    return true;
}

void VoiceInputController::stopAndTranscribe()
{
    if (m_state != Recording)
    {
        return;
    }

    m_maxDurationTimer->stop();
    if (m_audioSource)
    {
        m_audioSource->stop();
    }
    const QByteArray pcm = m_pcmBuffer ? m_pcmBuffer->data() : QByteArray();
    cleanupAudio();

    if (pcm.size() < kMinPcmBytes)
    {
        emit errorOccurred(tr("未录制到有效语音，请按住思路说完再点击停止。"));
        return; // 保持在 Idle
    }

    setState(Transcribing);
    upload(buildWav(pcm));
}

void VoiceInputController::cancel()
{
    m_maxDurationTimer->stop();
    if (m_state == Recording)
    {
        if (m_audioSource)
        {
            m_audioSource->stop();
        }
        cleanupAudio();
        setState(Idle);
    }
    else if (m_state == Transcribing && m_reply)
    {
        // abort 会触发 finished，由 onTranscribeReplyFinished 统一回到 Idle
        m_reply->abort();
    }
}

void VoiceInputController::setState(State state)
{
    if (m_state == state)
    {
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
}

void VoiceInputController::cleanupAudio()
{
    if (m_audioSource)
    {
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
    }
    if (m_pcmBuffer)
    {
        m_pcmBuffer->deleteLater();
        m_pcmBuffer = nullptr;
    }
}

QByteArray VoiceInputController::buildWav(const QByteArray &pcm) const
{
    // 标准无压缩 PCM WAV 头（44 字节）+ 数据
    const int channels = qMax(1, m_format.channelCount());
    const int sampleRate = qMax(1, m_format.sampleRate());

    QByteArray wav;
    wav.reserve(pcm.size() + 44);
    wav.append("RIFF");
    appendU32(&wav, quint32(36 + pcm.size()));
    wav.append("WAVE");
    wav.append("fmt ");
    appendU32(&wav, 16);                      // fmt 块长度
    appendU16(&wav, 1);                       // PCM
    appendU16(&wav, quint16(channels));
    appendU32(&wav, quint32(sampleRate));
    appendU32(&wav, quint32(sampleRate * channels * 2)); // 字节率（16bit）
    appendU16(&wav, quint16(channels * 2));   // 块对齐
    appendU16(&wav, 16);                      // 位深
    wav.append("data");
    appendU32(&wav, quint32(pcm.size()));
    wav.append(pcm);
    return wav;
}

void VoiceInputController::upload(const QByteArray &wav)
{
    if (!m_nam)
    {
        m_nam = new QNetworkAccessManager(this);
    }

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"speech.wav\"")));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("audio/wav")));
    filePart.setBody(wav);
    multiPart->append(filePart);

    QHttpPart modelPart;
    modelPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant(QStringLiteral("form-data; name=\"model\"")));
    modelPart.setBody(m_pendingConfig.model.toUtf8());
    multiPart->append(modelPart);

    QNetworkRequest request{QUrl(m_pendingConfig.apiUrl)};
    request.setRawHeader("Authorization", ("Bearer " + m_pendingConfig.apiKey).toUtf8());
    request.setTransferTimeout(60000); // 60s 音频的转写需要一定时间

    m_reply = m_nam->post(request, multiPart);
    multiPart->setParent(m_reply); // 随 reply 一起释放
    connect(m_reply, &QNetworkReply::finished, this, &VoiceInputController::onTranscribeReplyFinished);
}

void VoiceInputController::onTranscribeReplyFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
    {
        return;
    }
    reply->deleteLater();

    // 主动取消：静默回到 Idle，不产生结果或报错
    if (reply->error() == QNetworkReply::OperationCanceledError)
    {
        setState(Idle);
        return;
    }

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError)
    {
        QString detail;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject())
        {
            detail = doc.object().value(QStringLiteral("message"))
                         .toString(doc.object().value(QStringLiteral("error")).toString());
        }
        QString msg = tr("语音识别失败：%1").arg(reply->errorString());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status > 0)
        {
            msg += tr("（HTTP %1）").arg(status);
        }
        if (!detail.isEmpty())
        {
            msg += QStringLiteral("\n") + detail;
        }
        setState(Idle);
        emit errorOccurred(msg);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QString text = doc.object().value(QStringLiteral("text")).toString().trimmed();
    setState(Idle);
    if (text.isEmpty())
    {
        emit errorOccurred(tr("服务未返回识别文本，请检查模型 ID 是否正确。"));
        return;
    }
    emit transcriptionFinished(text);
}
