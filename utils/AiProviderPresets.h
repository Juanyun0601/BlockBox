/**
 * @file   AiProviderPresets.h
 * @brief  内置 LLM 服务商预设清单（设置页 / 模型选择窗口共用，单一数据源）
 * @author BlockBox Team
 * @date   2026-09-24
 *
 * 每个条目：
 *  - key        短标识，同时用于按模型 displayName 首词匹配（如 DeepSeek / Kimi / 快意）
 *  - name       设置页展示名
 *  - defaultUrl 默认 API 端点（OpenAI 兼容 /chat/completions 拼接基准）
 *  - configKey  设置键前缀，读写 ai/<configKey>/{url,key,selectedModel,selectedModelName}
 *  - urlHint    预设模型 apiUrl 特征子串，用于从 :/resources/ai_models.json 筛选该服务商模型，
 *               并在模型选择窗口把已保存的 url/key 精确套用到对应模型（优先于首词匹配，
 *               避免「DeepSeek V3 (SiliconFlow)」这类同名不同源被官方 DeepSeek 配置覆盖）
 */
#ifndef AIPROVIDERPRESETS_H
#define AIPROVIDERPRESETS_H

#include <QList>
#include <QString>

struct AiProviderPreset
{
    QString key;
    QString name;
    QString defaultUrl;
    QString configKey;
    QString urlHint;
};

/// 内置服务商全量清单（顺序即设置页展示顺序）
inline QList<AiProviderPreset> aiProviderPresets()
{
    return {
        // ── 常用（原有 8 家） ──
        {QStringLiteral("DeepSeek"),   QStringLiteral("DeepSeek"),
         QStringLiteral("https://api.deepseek.com/v1"), QStringLiteral("ai/provider_deepseek"),
         QStringLiteral("deepseek.com")},
        {QStringLiteral("OpenAI"),     QStringLiteral("OpenAI"),
         QStringLiteral("https://api.openai.com/v1"), QStringLiteral("ai/provider_openai"),
         QStringLiteral("openai.com")},
        {QStringLiteral("Anthropic"),  QStringLiteral("Anthropic"),
         QStringLiteral("https://api.anthropic.com"), QStringLiteral("ai/provider_anthropic"),
         QStringLiteral("anthropic.com")},
        {QStringLiteral("Gemini"),     QStringLiteral("Google Gemini"),
         QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"),
         QStringLiteral("ai/provider_gemini"), QStringLiteral("googleapis.com")},
        {QStringLiteral("OpenRouter"), QStringLiteral("OpenRouter"),
         QStringLiteral("https://openrouter.ai/api/v1"), QStringLiteral("ai/provider_openrouter"),
         QStringLiteral("openrouter.ai")},
        {QStringLiteral("GLM"),        QStringLiteral("智谱 GLM"),
         QStringLiteral("https://open.bigmodel.cn/api/paas/v4"), QStringLiteral("ai/provider_glm"),
         QStringLiteral("bigmodel.cn")},
        {QStringLiteral("Qwen"),       QStringLiteral("通义千问"),
         QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1"),
         QStringLiteral("ai/provider_qwen"), QStringLiteral("dashscope.aliyuncs")},
        {QStringLiteral("ERNIE"),      QStringLiteral("文心一言"),
         QStringLiteral("https://qianfan.baidubce.com/v2"), QStringLiteral("ai/provider_ernie"),
         QStringLiteral("baidubce.com")},

        // ── 国内 ──
        {QStringLiteral("Kimi"),       QStringLiteral("Kimi（月之暗面）"),
         QStringLiteral("https://api.moonshot.cn/v1"), QStringLiteral("ai/provider_kimi"),
         QStringLiteral("moonshot.cn")},
        {QStringLiteral("Doubao"),     QStringLiteral("豆包（火山方舟）"),
         QStringLiteral("https://ark.cn-beijing.volces.com/api/v3"),
         QStringLiteral("ai/provider_doubao"), QStringLiteral("volces.com")},
        {QStringLiteral("SiliconFlow"),QStringLiteral("硅基流动"),
         QStringLiteral("https://api.siliconflow.cn/v1"), QStringLiteral("ai/provider_siliconflow"),
         QStringLiteral("siliconflow.cn")},
        {QStringLiteral("Hunyuan"),    QStringLiteral("腾讯混元"),
         QStringLiteral("https://api.hunyuan.cloud.tencent.com/v1"),
         QStringLiteral("ai/provider_hunyuan"), QStringLiteral("hunyuan.cloud.tencent.com")},
        {QStringLiteral("TencentLKEAP"), QStringLiteral("腾讯云 DeepSeek"),
         QStringLiteral("https://api.lkeap.cloud.tencent.com/v1"),
         QStringLiteral("ai/provider_tencent_lkeap"), QStringLiteral("lkeap.cloud.tencent.com")},
        {QStringLiteral("MiniMax"),    QStringLiteral("MiniMax"),
         QStringLiteral("https://api.minimax.chat/v1"), QStringLiteral("ai/provider_minimax"),
         QStringLiteral("minimax.chat")},
        {QStringLiteral("Spark"),      QStringLiteral("讯飞星火"),
         QStringLiteral("https://spark-api-open.xf-yun.com/v1"),
         QStringLiteral("ai/provider_spark"), QStringLiteral("xf-yun.com")},
        {QStringLiteral("Baichuan"),   QStringLiteral("百川智能"),
         QStringLiteral("https://api.baichuan-ai.com/v1"), QStringLiteral("ai/provider_baichuan"),
         QStringLiteral("baichuan-ai.com")},
        {QStringLiteral("Step"),       QStringLiteral("阶跃星辰"),
         QStringLiteral("https://api.stepfun.com/v1"), QStringLiteral("ai/provider_step"),
         QStringLiteral("stepfun.com")},
        {QStringLiteral("Yi"),         QStringLiteral("零一万物"),
         QStringLiteral("https://api.lingyiwanwu.com/v1"), QStringLiteral("ai/provider_yi"),
         QStringLiteral("lingyiwanwu.com")},
        {QStringLiteral("InternLM"),   QStringLiteral("书生·浦语"),
         QStringLiteral("https://api.intern-ai.org.cn/v1"), QStringLiteral("ai/provider_internlm"),
         QStringLiteral("intern-ai.org.cn")},
        {QStringLiteral("360"),        QStringLiteral("360 智脑"),
         QStringLiteral("https://api.360.cn/v1"), QStringLiteral("ai/provider_360"),
         QStringLiteral("api.360.cn")},
        {QStringLiteral("SenseChat"),  QStringLiteral("商汤商商量"),
         QStringLiteral("https://api.sensenova.cn/v1"), QStringLiteral("ai/provider_sensechat"),
         QStringLiteral("sensenova.cn")},
        {QStringLiteral("XVERSE"),     QStringLiteral("元象 XVERSE"),
         QStringLiteral("https://api.xverse.cn/v1"), QStringLiteral("ai/provider_xverse"),
         QStringLiteral("xverse.cn")},
        {QStringLiteral("MiniCPM"),    QStringLiteral("面壁 MiniCPM"),
         QStringLiteral("https://api.modelbest.cn/v1"), QStringLiteral("ai/provider_minicpm"),
         QStringLiteral("modelbest.cn")},
        {QStringLiteral("天工"),        QStringLiteral("天工"),
         QStringLiteral("https://api.tiangong.cn/v1"), QStringLiteral("ai/provider_tiangong"),
         QStringLiteral("tiangong.cn")},
        {QStringLiteral("Pangu"),      QStringLiteral("华为云盘古"),
         QStringLiteral("https://pangu.huaweicloud.com/api"), QStringLiteral("ai/provider_pangu"),
         QStringLiteral("huaweicloud.com")},
        {QStringLiteral("CloudWalk"),  QStringLiteral("云从 CloudWalk"),
         QStringLiteral("https://api.cloudwalk.com/v1"), QStringLiteral("ai/provider_cloudwalk"),
         QStringLiteral("api.cloudwalk.com")},
        {QStringLiteral("OrionStar"),  QStringLiteral("猎户 OrionStar"),
         QStringLiteral("https://api.orionstar.com/v1"), QStringLiteral("ai/provider_orionstar"),
         QStringLiteral("api.orionstar.com")},
        {QStringLiteral("TRS"),        QStringLiteral("TRS"),
         QStringLiteral("https://api.trs.com.cn/v1"), QStringLiteral("ai/provider_trs"),
         QStringLiteral("api.trs.com.cn")},
        {QStringLiteral("Unisound"),   QStringLiteral("云知声"),
         QStringLiteral("https://api.unisound.com/v1"), QStringLiteral("ai/provider_unisound"),
         QStringLiteral("api.unisound.com")},
        {QStringLiteral("快意"),        QStringLiteral("快意（快手）"),
         QStringLiteral("https://api.kuaishou.com/v1"), QStringLiteral("ai/provider_kwai"),
         QStringLiteral("api.kuaishou.com")},

        // ── 国际 ──
        {QStringLiteral("Grok"),       QStringLiteral("xAI Grok"),
         QStringLiteral("https://api.x.ai/v1"), QStringLiteral("ai/provider_grok"),
         QStringLiteral("api.x.ai")},
        {QStringLiteral("Mistral"),    QStringLiteral("Mistral"),
         QStringLiteral("https://api.mistral.ai/v1"), QStringLiteral("ai/provider_mistral"),
         QStringLiteral("api.mistral.ai")},
        {QStringLiteral("Groq"),       QStringLiteral("Groq"),
         QStringLiteral("https://api.groq.com/openai/v1"), QStringLiteral("ai/provider_groq"),
         QStringLiteral("api.groq.com")},
        {QStringLiteral("Perplexity"), QStringLiteral("Perplexity"),
         QStringLiteral("https://api.perplexity.ai"), QStringLiteral("ai/provider_perplexity"),
         QStringLiteral("api.perplexity.ai")},
        {QStringLiteral("Together"),   QStringLiteral("Together"),
         QStringLiteral("https://api.together.xyz/v1"), QStringLiteral("ai/provider_together"),
         QStringLiteral("api.together.xyz")},
        {QStringLiteral("Fireworks"),  QStringLiteral("Fireworks"),
         QStringLiteral("https://api.fireworks.ai/inference/v1"),
         QStringLiteral("ai/provider_fireworks"), QStringLiteral("fireworks.ai")},
        {QStringLiteral("DeepInfra"),  QStringLiteral("DeepInfra"),
         QStringLiteral("https://api.deepinfra.com/v1/openai"),
         QStringLiteral("ai/provider_deepinfra"), QStringLiteral("deepinfra.com")},
        {QStringLiteral("Cerebras"),   QStringLiteral("Cerebras"),
         QStringLiteral("https://api.cerebras.ai/v1"), QStringLiteral("ai/provider_cerebras"),
         QStringLiteral("cerebras.ai")},
        {QStringLiteral("Cohere"),     QStringLiteral("Cohere"),
         QStringLiteral("https://api.cohere.com/v1"), QStringLiteral("ai/provider_cohere"),
         QStringLiteral("api.cohere.com")},
        {QStringLiteral("NVIDIA"),     QStringLiteral("NVIDIA"),
         QStringLiteral("https://integrate.api.nvidia.com/v1"), QStringLiteral("ai/provider_nvidia"),
         QStringLiteral("integrate.api.nvidia.com")},
        {QStringLiteral("HuggingFace"),QStringLiteral("HuggingFace"),
         QStringLiteral("https://api-inference.huggingface.co/v1"),
         QStringLiteral("ai/provider_huggingface"), QStringLiteral("huggingface.co")},
        {QStringLiteral("Meta"),       QStringLiteral("Meta Llama"),
         QStringLiteral("https://api.llama-api.com/v1"), QStringLiteral("ai/provider_meta"),
         QStringLiteral("llama-api.com")},
        {QStringLiteral("AI21"),       QStringLiteral("AI21"),
         QStringLiteral("https://api.ai21.com/studio/v1"), QStringLiteral("ai/provider_ai21"),
         QStringLiteral("ai21.com")},
        {QStringLiteral("Lepton"),     QStringLiteral("Lepton"),
         QStringLiteral("https://api.lepton.ai/v1"), QStringLiteral("ai/provider_lepton"),
         QStringLiteral("lepton.ai")},
        {QStringLiteral("Replicate"),  QStringLiteral("Replicate"),
         QStringLiteral("https://api.replicate.com/v1"), QStringLiteral("ai/provider_replicate"),
         QStringLiteral("api.replicate.com")},
        {QStringLiteral("Scaleway"),   QStringLiteral("Scaleway"),
         QStringLiteral("https://api.scaleway.ai/v1"), QStringLiteral("ai/provider_scaleway"),
         QStringLiteral("api.scaleway.ai")},
        {QStringLiteral("Inflection"), QStringLiteral("Inflection"),
         QStringLiteral("https://api.inflection.ai/v1"), QStringLiteral("ai/provider_inflection"),
         QStringLiteral("api.inflection.ai")},
    };
}

/// 按短标识查预设；未找到返回空 key 条目
inline AiProviderPreset aiProviderPresetByKey(const QString &key)
{
    for (const AiProviderPreset &p : aiProviderPresets()) {
        if (p.key == key)
            return p;
    }
    return {};
}

#endif // AIPROVIDERPRESETS_H
