#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>

#include "spdlog/fmt/bundled/core.h"
#include "spdlog/fmt/bundled/format.h"

#include "re2/re2.h"

#include "utfcpp/utf8.h"

#include "common/logger.h"
#include "common/utils/file_utils.h"
#include "common/utils/json_utils.h"
#include "lms/model/private_include/qwen3_5_bmruntime.h"

namespace qifeng {
    namespace lms {

        constexpr const char* UTF8_FFFD = "\xef\xbf\xbd";

        constexpr const char* DEFAULT_SYSTEM_PROMPT = "You are a helpful assistant.";

        // 标准 attention 的 tensor 索引
        constexpr std::size_t IDX_STD_INPUT_STATES = 0;
        constexpr std::size_t IDX_STD_POSITION_IDS = 1;
        constexpr std::size_t IDX_STD_ATTENTION_MASK = 2;
        constexpr std::size_t IDX_STD_HISTORY_K = 3;
        constexpr std::size_t IDX_STD_HISTORY_V = 4;
        constexpr std::size_t IDX_STD_OUTPUT_STATES = 0;
        constexpr std::size_t IDX_STD_K_CACHE = 1;
        constexpr std::size_t IDX_STD_V_CACHE = 2;

        constexpr std::size_t IDX_LIN_DYN_INPUT_STATES = 0;
        constexpr std::size_t IDX_LIN_DYN_RECURRENT_STATES = 1;
        constexpr std::size_t IDX_LIN_DYN_OUTPUT_STATES = 0;
        constexpr std::size_t IDX_LIN_DYN_OUTPUT_CONV_STATES = 1;

        constexpr std::size_t IDX_LIN_CACHE_INPUT_STATES = 0;
        constexpr std::size_t IDX_LIN_CACHE_INPUT_CONV_STATE = 1;
        constexpr std::size_t IDX_LIN_CACHE_INPUT_RECURRENT_STATE = 2;
        constexpr std::size_t IDX_LIN_CACHE_OUTPUT_STATES = 0;
        constexpr std::size_t IDX_LIN_CACHE_OUTPUT_CONV_STATE = 1;

        constexpr uint16_t FLOAT16_MASK_VALUE = 0xF0E2;
        constexpr uint16_t BFLOAT16_MASK_VALUE = 0xC61C;

        static BMRuntimeQwen35Model::SampleParam parseSampleParamFromJson(const Json::Value& config) {
            // 思考模式的默认参数
            float repetitionPenalty {1.1f};
            float temperature {1.0f};
            float topP {0.95f};
            int32_t topK {20};

            if (config.isMember("repetition_penalty")) {
                const Json::Value& val = config["repetition_penalty"];
                if (!val.isDouble()) {
                    throw std::runtime_error {
                        "Sample param validation error: repetition_penalty must be absent or float number."};
                }
                repetitionPenalty = val.asFloat();
            }

            if (!config.isMember("temperature")) {
                throw std::runtime_error {"Sample param validation error: temperature required."};
            }
            const Json::Value& temperatureVal = config["temperature"];
            if (!temperatureVal.isDouble()) {
                throw std::runtime_error {"Sample param validation error: temperature must be float number."};
            }
            temperature = temperatureVal.asFloat();

            if (!config.isMember("top_p")) {
                throw std::runtime_error {"Sample param validation error: top_p required."};
            }
            const Json::Value& topPVal = config["top_p"];
            if (!topPVal.isDouble()) {
                throw std::runtime_error {"Sample param validation error: top_p must be float number."};
            }
            topP = topPVal.asFloat();

            if (config.isMember("top_k")) {
                const Json::Value& topKVal = config["top_k"];
                if (!topKVal.isInt()) {
                    throw std::runtime_error {"Sample param validation error: top_k must be integer number."};
                }
                topK = topKVal.asInt();
            }
            return BMRuntimeQwen35Model::SampleParam {repetitionPenalty, temperature, topP, topK};
        }

        // ========================================================================
        // renderChatTemplate — 对齐 qwen3.5 chat_template.jinja
        // ========================================================================
        std::string renderChatTemplate(const BMRuntimeQwen35ModelContext::ChatTemplateInput& in) {
            auto lstripNewline = [](const std::string& s) -> std::string {
                auto pos = s.find_first_not_of('\n');
                if (pos == std::string::npos) {
                    return {};
                }
                return s.substr(pos);
            };

            auto rstripNewline = [](const std::string& s) -> std::string {
                auto pos = s.find_last_not_of('\n');
                if (pos == std::string::npos) {
                    return {};
                }
                return s.substr(0, pos + 1);
            };

            // 去除首尾所有空白字符（对齐 jinja 模板里的 |trim）
            auto trimWs = [](const std::string& s) -> std::string {
                auto start = s.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) {
                    return {};
                }
                auto end = s.find_last_not_of(" \t\r\n");
                return s.substr(start, end - start + 1);
            };

            const auto& msgs = in.messages;
            const auto& tools = in.tools;

            if (msgs.empty()) {
                throw std::runtime_error {"Messages in input of chat template can not be empty."};
            }

            std::string out {};

            if (!tools.empty()) {
                // 工具调用模板与 qwen3.5 chat_template.jinja 对齐：
                //   1) 头部固定说明 + <tools> 内嵌各工具 JSON
                //   2) 采用 <function=...>/<parameter=...> 调用格式说明
                //   3) system 消息内容放在说明块之后（非空时以 "\n\n" 前缀追加）
                out += "<|im_start|>system\n";
                out += "# Tools\n\nYou have access to the following functions:\n\n<tools>";

                for (const auto& tool : tools) {
                    fmt::format_to(std::back_inserter(out), "\n{}", common::utils::JsonToCompactString(tool));
                }

                out += R"(
</tools>

If you choose to call a function ONLY reply in the following format with NO suffix:

<tool_call>
<function=example_function_name>
<parameter=example_parameter_1>
value_1
</parameter>
<parameter=example_parameter_2>
This is the value for the second parameter
that can span
multiple lines
</parameter>
</function>
</tool_call>

<IMPORTANT>
Reminder:
- Function calls MUST follow the specified format: an inner <function=...></function> block must be nested within <tool_call></tool_call> XML tags
- Required parameters MUST be specified
- You may provide optional reasoning for your function call in natural language BEFORE the function call, but NOT after
- If there is no function call available, answer the question like normal with your current knowledge and do not tell the user about function calls
</IMPORTANT>)";

                if (msgs[0].role == "system") {
                    std::string sysContent = trimWs(msgs[0].content);
                    if (!sysContent.empty()) {
                        out += "\n\n";
                        out += sysContent;
                    }
                }
                out += "<|im_end|>\n";
            } else {
                if (msgs[0].role == "system") {
                    fmt::format_to(std::back_inserter(out), "<|im_start|>system\n{}<|im_end|>\n", msgs[0].content);
                }
            }

            int64_t lastQueryIndex = static_cast<int64_t>(msgs.size()) - 1;

            static const RE2 toolResponsePattern {R"((?s)<tool_response>.*</tool_response>)"};
            static const RE2 thinkEndPattern {"</think>"};

            for (size_t ri = msgs.size(); ri > 0; --ri) {
                const auto& msg = msgs[ri - 1];
                int64_t idx = static_cast<int64_t>(ri - 1);

                if (msg.role == "user") {
                    bool isToolResp = RE2::FullMatch(msg.content, toolResponsePattern);
                    if (!isToolResp) {
                        lastQueryIndex = idx;
                        break;
                    }
                }
            }

            for (size_t i = 0; i < msgs.size(); ++i) {
                const auto& msg = msgs[i];
                std::string content = msg.content;

                if (msg.role == "user" || (msg.role == "system" && i != 0)) {
                    fmt::format_to(std::back_inserter(out), "<|im_start|>{}\n{}<|im_end|>\n", msg.role, content);
                } else if (msg.role == "assistant") {
                    std::string reasoning {};

                    if (msg.reasoningContent.has_value()) {
                        reasoning = msg.reasoningContent.value();
                    } else {
                        if (RE2::PartialMatch(content, thinkEndPattern)) {
                            auto endPos = content.find("</think>");
                            std::string before = content.substr(0, endPos);
                            before = rstripNewline(before);

                            // 对齐 qwen3.5 模板 split('<think>')[-1] 的语义：
                            // 有 <think> 时取其后内容；没有 <think>（如本引擎生成的补全，
                            // 开头 <think> 在 prompt 里而不在存储文本中）时取整段 before。
                            auto thinkPos = before.rfind("<think>");
                            if (thinkPos != std::string::npos) {
                                reasoning = before.substr(thinkPos + 7);
                            } else {
                                reasoning = before;
                            }
                            reasoning = lstripNewline(reasoning);

                            auto lastEnd = content.rfind("</think>");
                            content = content.substr(lastEnd + 8);
                            content = lstripNewline(content);
                        }
                    }

                    bool afterLastQuery = (static_cast<int64_t>(i) > lastQueryIndex);

                    // 与 qwen3.5 模板一致：最后一个 user query 之后的 assistant 消息，
                    // 无论 reasoning 是否为空都渲染 <think>...</think> 块（不再附加额外条件）。
                    if (afterLastQuery) {
                        fmt::format_to(std::back_inserter(out), "<|im_start|>assistant\n<think>\n{}\n</think>\n\n{}",
                                       trimWs(reasoning), lstripNewline(content));
                    } else {
                        fmt::format_to(std::back_inserter(out), "<|im_start|>assistant\n{}", content);
                    }

                    if (!msg.toolCalls.empty()) {
                        for (size_t tcIndex = 0; tcIndex < msg.toolCalls.size(); ++tcIndex) {
                            const auto& tc = msg.toolCalls[tcIndex];

                            // 前缀空白（对齐 qwen3.5 模板）：
                            //   第一个 tool_call 且正文非空 → "\n\n"；正文为空 → 无前缀
                            //   其余 tool_call → "\n"
                            if (tcIndex == 0) {
                                if (!trimWs(content).empty()) {
                                    out += "\n\n";
                                }
                            } else {
                                out += "\n";
                            }

                            fmt::format_to(std::back_inserter(out), "<tool_call>\n<function={}>\n", tc.name);

                            if (tc.arguments.isObject()) {
                                for (const auto& argName : tc.arguments.getMemberNames()) {
                                    const Json::Value& argValue = tc.arguments[argName];
                                    fmt::format_to(std::back_inserter(out), "<parameter={}>\n", argName);
                                    // 对象/数组 → 紧凑 JSON；字符串 → 原文；其他标量 → JSON 文本
                                    std::string valueStr;
                                    if (argValue.isObject() || argValue.isArray()) {
                                        valueStr = common::utils::JsonToCompactString(argValue);
                                    } else if (argValue.isString()) {
                                        valueStr = argValue.asString();
                                    } else {
                                        valueStr = common::utils::JsonToCompactString(argValue);
                                    }
                                    out += valueStr;
                                    out += "\n</parameter>\n";
                                }
                            }

                            out += "</function>\n</tool_call>";
                        }
                    }

                    out += "<|im_end|>\n";
                } else if (msg.role == "tool") {
                    bool prevIsTool = (i > 0 && msgs[i - 1].role == "tool");
                    bool nextIsTool = (i + 1 < msgs.size() && msgs[i + 1].role == "tool");

                    if (i == 0 || !prevIsTool) {
                        out += "<|im_start|>user";
                    }

                    fmt::format_to(std::back_inserter(out), "\n<tool_response>\n{}\n</tool_response>", content);

                    if (i == msgs.size() - 1 || !nextIsTool) {
                        out += "<|im_end|>\n";
                    }
                }
            }

            if (in.addGenerationPrompt) {
                fmt::format_to(std::back_inserter(out), "<|im_start|>assistant\n");
                if (!in.enableThinking) {
                    fmt::format_to(std::back_inserter(out), "<think>\n\n</think>\n\n");
                } else {
                    // qwen3.5 模板：enable_thinking 为真时强制打开 <think>\n，模型据此生成思考内容。
                    // 这是 qwen3.5 与 qwen3 chat_template 的关键差异；缺失该前缀会让模型收到与训练
                    // 时不一致的上下文（prompt 以 "assistant\n" 结尾而非 "assistant\n<think>\n"），
                    // 从而输出不连贯的乱序 token。
                    fmt::format_to(std::back_inserter(out), "<think>\n");
                }
            }

            return out;
        }

        // ========================================================================
        // ChatTemplateInput / ModelContext — 与 qwen3 完全一致
        // ========================================================================

        BMRuntimeQwen35ModelContext::ChatTemplateInput::ChatTemplateInput()
            : messages {}, tools {}, addGenerationPrompt {true}, enableThinking {false} {
        }

        BMRuntimeQwen35ModelContext::BMRuntimeQwen35ModelContext(std::shared_ptr<BMRuntimeQwen35Model> model)
            : ModelContext {std::move(model)}, mTemplateInput {}, mGenerationCanceled {false} {
        }

        BMRuntimeQwen35ModelContext::~BMRuntimeQwen35ModelContext() {
        }

        std::string BMRuntimeQwen35ModelContext::Generate(const ModelRequest& request) {
            std::string result {};
            StreamGenerateCallback callback = [&result](StreamData data) -> bool {
                if (std::holds_alternative<StreamContent>(data)) {
                    const auto& content = std::get<StreamContent>(data);
                    result.append(content.sequence);
                } else if (std::holds_alternative<StreamError>(data)) {
                    const auto& error = std::get<StreamError>(data);
                    throw std::runtime_error {error.what};
                }
                return true;
            };

            DoStreamGenerate(request, callback);

            return result;
        }

        void BMRuntimeQwen35ModelContext::StreamGenerate(const ModelRequest& request, StreamGenerateCallback callback) {
            DoStreamGenerate(request, callback);
        }

        void BMRuntimeQwen35ModelContext::CancelGeneration() {
            mGenerationCanceled = true;
        }

        void BMRuntimeQwen35ModelContext::Reset() {
            mTemplateInput = ChatTemplateInput {};
            mGenerationCanceled = false;
        }

        bool BMRuntimeQwen35ModelContext::DoStreamGenerate(const ModelRequest& request,
                                                           StreamGenerateCallback callback) {
            ChatTemplateInput templateInput = mTemplateInput;

            {
                ChatMessage systemMessage {"system",
                                           request.systemPrompt.empty() ? DEFAULT_SYSTEM_PROMPT : request.systemPrompt,
                                           std::nullopt, std::vector<ToolCall> {}};
                if (templateInput.messages.empty()) {
                    templateInput.messages.push_back(systemMessage);
                } else {
                    templateInput.messages[0] = systemMessage;
                }
            }

            templateInput.messages.push_back(
                ChatMessage {"user", request.userPrompt, std::nullopt, std::vector<ToolCall> {}});
            templateInput.enableThinking = request.enableThinking;

            std::string input = renderChatTemplate(templateInput);
            // SLOG_DEBUG << "[DoStreamGenerate] after render prompt=" << input;

            if (!utf8::is_valid(input)) {
                throw std::runtime_error {"Invalid input = " + input};
            }

            std::string output {};
            auto proxyCallback = [this, &output, callback](StreamData data) -> bool {
                bool expected {true};
                if (mGenerationCanceled.compare_exchange_strong(expected, false)) {
                    throw ModelContext::CanceledException {"This Generation has been canceled."};
                }

                if (std::holds_alternative<StreamContent>(data)) {
                    const auto& content = std::get<StreamContent>(data);
                    output.append(content.sequence);
                }
                return callback(data);
            };

            auto modelPtr = std::static_pointer_cast<BMRuntimeQwen35Model>(mModel);

            std::optional<BMRuntimeQwen35Model::SampleParam> generateParam {std::nullopt};
            if (request.configJson.has_value()) {
                Json::Value configJson;
                Json::Reader reader;
                if (!reader.parse(request.configJson.value(), configJson)) {
                    throw std::runtime_error {"configJson parse failed: " + reader.getFormattedErrorMessages()};
                }

                generateParam = parseSampleParamFromJson(configJson);
            }
            SLOG_DEBUG << "[DoStreamGenerate] after parseSampleParamFromJson repetitionPenalty="
                       << generateParam->repetitionPenalty << ", temperature=" << generateParam->temperature
                       << ", topP=" << generateParam->topP << ", topK=" << generateParam->topK;
            mGenerationCanceled = false;
            bool ret = modelPtr->NonParallelizableStreamGenerate(input, generateParam, proxyCallback);

            templateInput.messages.push_back(ChatMessage {"assistant", output, std::nullopt, std::vector<ToolCall> {}});
            mTemplateInput = templateInput;

            return ret;
        }

        // void BMRuntimeQwen35Model::InitConfigHandlers() {
        //     mConfigHandlers = {{"audio_start_token_id", [this](const Json::Value& v) { mAudioBosToken = v.asInt();
        //     }},
        //                        {"audio_end_token_id", [this](const Json::Value& v) { mAudioEosToken = v.asInt(); }},
        //                        {"eos_token_id", [this](const Json::Value& v) {
        //                             if (v.isArray()) {
        //                                 for (const auto& t : v)
        //                                     mEosTokens.insert(t.asInt());
        //                             } else {
        //                                 mEosTokens.insert(v.asInt());
        //                             }
        //                         }}};
        // }

        // ========================================================================
        // BMRuntimeQwen35Model 构造函数
        // 关键变化：检测每层是标准 attention 还是线性 attention，
        // 用第一个标准 attention 层来获取 mMaxTokenLength / mKVBytes。
        // ========================================================================

        BMRuntimeQwen35Model::BMRuntimeQwen35Model(const std::string& tokenizerPath, const std::string& configPath,
                                                   const std::string& modelPath)
            : mTokenizer {nullptr}, mEosTokens {}, mSampleStrategy {SampleStrategy::GREEDY},
              mDefaultSampleParam {}, mEmbed {nullptr}, mEmbedCache {nullptr}, mLmHead {nullptr}, mGreedyHead {nullptr},
              mSampleHead {nullptr}, mBlockerLayerSize {0}, mLayers {nullptr}, mFirstStandardLayerIndex {0},
              mHiddenBytes {0}, mKVBytes {0}, mConvBytes {0}, mMaxInputTokenLength {0}, mMaxTokenLength {0},
              mMaskValue {0}, mMutexNetwork {}, mNetworkInput {nullptr} {
            std::vector<BlockType> configLayerTypes;
            uint16_t firstLinearLayerIndex = 0;
            mGenerator.seed(std::random_device {}());
            try {
                // 读取配置并初始化 tokenizer
                {
                    std::filesystem::path tokenizerU8path = std::filesystem::path(tokenizerPath);
                    std::string tokenizerJson = common::utils::LoadAllContentFromFile(tokenizerU8path);
                    mTokenizer = ::new_tokenizer_from_json(tokenizerJson.c_str());
                    if (mTokenizer == nullptr) {
                        const char* err = ::tokenizer_get_last_error();
                        throw std::runtime_error {err == nullptr ? "Tokenizer init failed." : err};
                    }
                }

                // 读取并解析 config 文件
                if (!configPath.empty()) {
                    std::string configJsonString =
                        common::utils::LoadAllContentFromFile(std::filesystem::path(configPath));
                    try {
                        Json::Value configJson;
                        Json::Reader reader;
                        if (!reader.parse(configJsonString, configJson)) {
                            throw std::runtime_error {"Failed to parse config file: " +
                                                      reader.getFormattedErrorMessages()};
                        }

                        if (!configJson.isMember("eos_token_id")) {
                            throw std::runtime_error {"Config file param validation error: eos_token_id required."};
                        }
                        const Json::Value& eosTokenIdJson = configJson["eos_token_id"];
                        if (eosTokenIdJson.isArray()) {
                            for (const auto& v : eosTokenIdJson) {
                                mEosTokens.insert(v.asInt());
                            }
                        } else {
                            mEosTokens.insert(eosTokenIdJson.asInt());
                        }

                        for (const char* stopTokenText : {"<|im_end|>", "<|end|>", "<|endoftext|>"}) {
                            TokenizerEncodeResult* stopEnc = ::tokenizer_encode(mTokenizer, stopTokenText, false);
                            if (stopEnc != nullptr) {
                                // 仅当该特殊符被编码为单个 token 时才认为其存在于词表中
                                if (stopEnc->len == 1) {
                                    mEosTokens.insert(static_cast<int32_t>(stopEnc->token_ids[0]));
                                }
                                ::free_tokenizer_encode_result(stopEnc);
                            }
                        }

                        // if (!configJson.isMember("layer_types")) {
                        //     throw std::runtime_error {"Config file param validation error: layer_types required."};
                        // }
                        const Json::Value& ltJson = configJson["layer_types"];
                        if (!ltJson.isArray() || ltJson.size() == 0) {
                            throw std::runtime_error {
                                "Config file param validation error: layer_types must be non-empty array."};
                        }
                        configLayerTypes.reserve(ltJson.size());
                        bool foundLinear = false;
                        for (Json::ArrayIndex i = 0; i < ltJson.size(); ++i) {
                            const std::string& typeStr = ltJson[i].asString();
                            if (typeStr == "full_attention") {
                                configLayerTypes.push_back(BlockType::STANDARD_ATTENTION);
                            } else if (typeStr == "linear_attention") {
                                if (!foundLinear) {
                                    firstLinearLayerIndex = static_cast<uint16_t>(i);
                                    foundLinear = true;
                                }
                                configLayerTypes.push_back(BlockType::LINEAR_ATTENTION);
                            } else {
                                throw std::runtime_error {"Config file param validation error: unknown layer_type '" +
                                                          typeStr + "'."};
                            }
                        }
                    } catch (const std::exception& e) {
                        throw std::runtime_error {"Param validation error: config file parse failed, reason = " +
                                                  std::string {e.what()}};
                    }
                }

                // 加载 bmodel 网络
                {
                    bool ret = ::bmrt_load_bmodel(mpBmrt, modelPath.c_str());
                    if (!ret) {
                        throw std::runtime_error {"Failed to load bmodel."};
                    }

                    int netSize = ::bmrt_get_network_number(mpBmrt);
                    if (netSize <= 3) {
                        throw std::runtime_error {"Net size of bmodel is unexpected."};
                    }

                    mEmbed = const_cast<bm_net_info_t*>(::bmrt_get_network_info(mpBmrt, "embedding"));
                    if (mEmbed == nullptr) {
                        throw std::runtime_error {"Net architecture of bmodel is unexpected: missing embedding."};
                    }

                    mEmbedCache = const_cast<bm_net_info_t*>(::bmrt_get_network_info(mpBmrt, "embedding_cache"));
                    if (mEmbedCache == nullptr) {
                        throw std::runtime_error {"Net architecture of bmodel is unexpected: missing embedding_cache."};
                    }

                    mLmHead = const_cast<bm_net_info_t*>(::bmrt_get_network_info(mpBmrt, "lm_head"));
                    if (mLmHead == nullptr) {
                        throw std::runtime_error {"Net architecture of bmodel is unexpected: missing lm_head."};
                    }

                    mGreedyHead = const_cast<bm_net_info_t*>(::bmrt_get_network_info(mpBmrt, "greedy_head"));
                    if (mGreedyHead == nullptr) {
                        SLOG_WARN << "Greedy strategy is unsupported by this model.";
                    }

                    mSampleHead = const_cast<bm_net_info_t*>(::bmrt_get_network_info(mpBmrt, "sample_head"));
                    if (mSampleHead == nullptr) {
                        SLOG_WARN << "Sample strategy is unsupported by this model.";
                    }

                    if (mSampleHead == nullptr && mGreedyHead == nullptr) {
                        throw std::runtime_error {"Net architecture of bmodel is unexpected: no sampling head."};
                    }

                    int blockSize = netSize - 3;
                    if (mGreedyHead != nullptr) {
                        blockSize--;
                    }
                    if (mSampleHead != nullptr) {
                        blockSize--;
                    }

                    if (blockSize <= 0 || blockSize % 2 != 0) {
                        throw std::runtime_error {"Net architecture of bmodel is unexpected: invalid block count."};
                    }

                    mBlockerLayerSize = static_cast<uint16_t>(blockSize / 2);
                    mLayers = std::make_unique<BlockNetwork[]>(mBlockerLayerSize);

                    // 记录第一个标准 attention 层的索引（用于共享 position_ids / attention_mask）
                    bool foundStandard = false;

                    for (uint16_t layerIndex = 0; layerIndex < mBlockerLayerSize; layerIndex++) {
                        std::string blockName {"block_" + std::to_string(layerIndex)};
                        std::string blockCacheName {"block_cache_" + std::to_string(layerIndex)};

                        const bm_net_info_t* block = ::bmrt_get_network_info(mpBmrt, blockName.c_str());
                        if (block == nullptr) {
                            throw std::runtime_error {"Net architecture of bmodel is unexpected: missing " + blockName};
                        }

                        const bm_net_info_t* blockCache = ::bmrt_get_network_info(mpBmrt, blockCacheName.c_str());
                        if (blockCache == nullptr) {
                            throw std::runtime_error {"Net architecture of bmodel is unexpected: missing " +
                                                      blockCacheName};
                        }

                        // 根据 config 中的 layer_types 确定 attention 类型
                        if (layerIndex >= configLayerTypes.size()) {
                            throw std::runtime_error {"Config layer_types count mismatch with bmodel block count."};
                        }
                        BlockType type = configLayerTypes[layerIndex];
                        if (type == BlockType::STANDARD_ATTENTION && !foundStandard) {
                            mFirstStandardLayerIndex = layerIndex;
                            foundStandard = true;
                        }

                        mLayers[layerIndex] = BlockNetwork {block, blockCache, type};
                    }

                    if (!foundStandard) {
                        throw std::runtime_error {
                            "Net architecture of bmodel is unexpected: no standard attention layer found."};
                    }

                    // 从第一个标准 attention 层获取模型参数
                    const auto& stdBlockCache = mLayers[mFirstStandardLayerIndex].blockCache;

                    mHiddenBytes = static_cast<uint32_t>(
                        ::bm_mem_get_device_size(mLayers[0].blockCache->stages[0].output_mems[IDX_STD_OUTPUT_STATES]));

                    mKVBytes = static_cast<uint32_t>(
                        ::bm_mem_get_device_size(stdBlockCache->stages[0].output_mems[IDX_STD_K_CACHE]));

                    // 从线性 attention 层获取 conv_state 大小
                    mConvBytes = static_cast<uint32_t>(::bm_mem_get_device_size(
                        mLayers[firstLinearLayerIndex].block->stages[0].output_mems[IDX_LIN_DYN_OUTPUT_CONV_STATES]));

                    mMaxInputTokenLength = static_cast<int32_t>(mEmbed->stages[0].input_shapes[0].dims[1]);

                    // 从第一个标准 attention 层获取 mMaxTokenLength
                    mMaxTokenLength =
                        static_cast<int32_t>(stdBlockCache->stages[0].input_shapes[IDX_STD_HISTORY_K].dims[1]);

                    if (mMaxInputTokenLength > mMaxTokenLength) {
                        throw std::runtime_error {"Net architecture of bmodel is unexpected: maxInputTokenLength (" +
                                                  std::to_string(mMaxInputTokenLength) + ") > maxTokenLength (" +
                                                  std::to_string(mMaxTokenLength) + ")"};
                    }

                    if (mEmbedCache->output_dtypes[0] == BM_FLOAT16) {
                        mMaskValue = FLOAT16_MASK_VALUE;
                    } else if (mEmbedCache->output_dtypes[0] == BM_BFLOAT16) {
                        mMaskValue = BFLOAT16_MASK_VALUE;
                    } else {
                        throw std::runtime_error {
                            "Invalid attention dtype. Supported dtype are 'BM_FLOAT16' or 'BM_BFLOAT16'."};
                    }

                    // 所有 dynamic block 都应该允许动态形状
                    if (!mLayers[0].block->is_dynamic) {
                        throw std::runtime_error {"Only dynamic block is supported."};
                    }
                }
            } catch (...) {
                Finalize();
                throw;
            }
        }

        BMRuntimeQwen35Model::~BMRuntimeQwen35Model() {
            Finalize();
        }

        void BMRuntimeQwen35Model::Finalize() {
            if (mTokenizer != nullptr) {
                ::free_tokenizer(mTokenizer);
                mTokenizer = nullptr;
            }
        }

        std::shared_ptr<ModelContext> BMRuntimeQwen35Model::CreateContext() {
            return std::make_shared<BMRuntimeQwen35ModelContext>(
                std::static_pointer_cast<BMRuntimeQwen35Model>(shared_from_this()));
        }

        std::string BMRuntimeQwen35Model::ModelName() const {
            return "qwen3.5";
        }

        std::string BMRuntimeQwen35Model::EngineName() const {
            return "bmruntime";
        }

        // ========================================================================
        // NonParallelizableStreamGenerate — 与 qwen3 基本一致
        // ========================================================================

        bool BMRuntimeQwen35Model::NonParallelizableStreamGenerate(const std::string& input,
                                                                   std::optional<SampleParam> param,
                                                                   StreamGenerateCallback callback) {
            assert(callback != nullptr);

            std::unique_lock<std::mutex> lock {mMutexNetwork};

            if (mNetworkInput == nullptr) {
                mNetworkInput = std::make_unique<NetworkInput>();
                mNetworkInput->tokenLength = 0;
                mNetworkInput->tokenBuffer = std::make_unique<int32_t[]>(mMaxTokenLength);
                // qwen3.5 position_ids 为 3 维: [3, seqlen]
                mNetworkInput->positionIdBuffer = std::make_unique<int32_t[]>(3 * mMaxInputTokenLength);
                mNetworkInput->attentionMaskBuffer = std::make_unique<uint16_t[]>(mMaxTokenLength + 1);
            }

            // SLOG_DEBUG << "[NonParallelizableStreamGenerate] Input = " << input;

            {
                TokenizerEncodeResult* result = ::tokenizer_encode(mTokenizer, input.c_str(), false);
                if (result == nullptr) {
                    const char* err = ::tokenizer_get_last_error_in_handle(mTokenizer);
                    callback(StreamError {err == nullptr ? "Tokenizer encode failed."
                                                         : "Tokenizer encode failed, reason = " + std::string {err}});
                    return false;
                }
                if (static_cast<int32_t>(result->len) >= mMaxInputTokenLength) {
                    ::free_tokenizer_encode_result(result);
                    callback(StreamError {"Input is too long."});
                    return false;
                }
                mNetworkInput->tokenLength = static_cast<int32_t>(result->len);
                std::copy(reinterpret_cast<const int32_t*>(result->token_ids),
                          reinterpret_cast<const int32_t*>(result->token_ids + result->len),
                          mNetworkInput->tokenBuffer.get());
                std::fill_n(mNetworkInput->tokenBuffer.get() + static_cast<int32_t>(result->len),
                            mMaxTokenLength - static_cast<int32_t>(result->len), 0);
                ::free_tokenizer_encode_result(result);
                result = nullptr;

                // qwen3.5 position_ids 为 3 维，每维填充 [0, 1, 2, ..., tokenLength-1]。
                // block 的 position_ids tensor 形状为 [3, tokenLength] 连续内存，prefill 时
                // 按 tokenLength*3 连续拷贝（见 Prefill 中的 s2d），因此这里必须以 tokenLength
                // 为步长把三段 ramp 连续排布（与 python 参考实现 3*[0..tokenLength-1] 一致）。
                // 若使用 mMaxInputTokenLength 作为步长，dim1/dim2 会被误填为 0，导致 MROPE 错误。
                for (int32_t dim = 0; dim < 3; dim++) {
                    for (int32_t i = 0; i < mNetworkInput->tokenLength; i++) {
                        mNetworkInput->positionIdBuffer[dim * mNetworkInput->tokenLength + i] = i;
                    }
                }

                std::fill_n(mNetworkInput->attentionMaskBuffer.get(), mNetworkInput->tokenLength, 0);
                mNetworkInput->attentionMaskBuffer[mMaxTokenLength] = 0;
                std::fill_n(mNetworkInput->attentionMaskBuffer.get() + mNetworkInput->tokenLength,
                            mMaxTokenLength - mNetworkInput->tokenLength, mMaskValue);
            }

            const int32_t inputTokenCount = mNetworkInput->tokenLength;

            const auto prefillStartTime = std::chrono::steady_clock::now();

            CleanupAllNetworks();
            Prefill(param);

            const auto decodeStartTime = std::chrono::steady_clock::now();

            std::vector<int32_t> outputTokens {};
            while (mNetworkInput->tokenLength <= mMaxTokenLength) {
                int32_t currentToken = mNetworkInput->tokenBuffer[mNetworkInput->tokenLength - 1];
                if (mEosTokens.find(currentToken) != mEosTokens.end()) {
                    break;
                }
                outputTokens.push_back(currentToken);
                std::string word {};
                {
                    TokenizerDecodeResult* result = ::tokenizer_decode(
                        mTokenizer, reinterpret_cast<const uint32_t*>(outputTokens.data()), outputTokens.size(), false);
                    if (result == nullptr) {
                        const char* err = ::tokenizer_get_last_error_in_handle(mTokenizer);
                        callback(StreamError {err == nullptr
                                                  ? "Tokenizer decode failed."
                                                  : "Tokenizer decode failed, reason = " + std::string {err}});
                        return false;
                    }

                    word = std::string {result->text};
                    ::free_tokenizer_decode_result(result);
                }
                // SLOG_INFO << "Tokenizer decode result = " << word;
                if (word.find(UTF8_FFFD) == std::string::npos) {
                    outputTokens.clear();
                    if (!callback(StreamContent {word})) {
                        return true;
                    }
                }
                if (mNetworkInput->tokenLength == mMaxTokenLength) {
                    break;
                }
                Decode(param);
            }

            const auto decodeEndTime = std::chrono::steady_clock::now();
            const int32_t outputTokenCount = mNetworkInput->tokenLength - inputTokenCount;
            const auto prefillTime =
                std::chrono::duration_cast<std::chrono::milliseconds>(decodeStartTime - prefillStartTime).count();
            const float prefillSpeed = prefillTime == 0 ? 0.0f : inputTokenCount * 1000.0f / prefillTime;
            const auto decodeTime =
                std::chrono::duration_cast<std::chrono::milliseconds>(decodeEndTime - decodeStartTime).count();
            const float decodeSpeed = decodeTime == 0 ? 0.0f : outputTokenCount * 1000.0f / decodeTime;

            SLOG_DEBUG << "NonParallelizableStreamGenerate finished: "
                       << "totalTime = " << (prefillTime + decodeTime) << " ms, "
                       << "inputToken = " << inputTokenCount << " , "
                       << "prefillTime = " << prefillTime << " ms, "
                       << "prefillSpeed = " << prefillSpeed << " token/s,   "
                       << "outputToken = " << outputTokenCount << " , "
                       << "decodeTime = " << decodeTime << " ms, "
                       << "decodeSpeed = " << decodeSpeed << " token/s";

            callback(StreamEnd {mNetworkInput->tokenLength > mMaxTokenLength ? StreamEnd::Reason::EXCEED_TOKEN_LIMIT
                                                                             : StreamEnd::Reason::STOPPED});

            return true;
        }

        void BMRuntimeQwen35Model::CleanupAllNetworks() {
            CleanupNetwork(mEmbed);
            CleanupNetwork(mEmbedCache);
            CleanupNetwork(mLmHead);
            if (mGreedyHead != nullptr) {
                CleanupNetwork(mGreedyHead);
            }
            if (mSampleHead != nullptr) {
                CleanupNetwork(mSampleHead);
            }
            for (uint16_t layerIndex = 0; layerIndex < mBlockerLayerSize; layerIndex++) {
                CleanupNetwork(mLayers[layerIndex].block);
                CleanupNetwork(mLayers[layerIndex].blockCache);
            }
        }

        void BMRuntimeQwen35Model::CleanupNetwork(const bm_net_info_t* network) {
            bm_status_t ret {};
            int value = 0;
            for (int i = 0; i < network->input_num; i++) {
                ret = ::bm_memset_device_ext(mHandle, &value, 1, network->stages[0].input_mems[i]);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during network cleanup."};
                }
            }
            for (int i = 0; i < network->output_num; i++) {
                ret = ::bm_memset_device_ext(mHandle, &value, 1, network->stages[0].output_mems[i]);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during network cleanup."};
                }
            }
        }

        // ========================================================================
        // Prefill — 混合 attention 版本
        // ========================================================================

        void BMRuntimeQwen35Model::Prefill(std::optional<SampleParam> param) {
            bm_status_t ret {};
            int32_t tokLen = mNetworkInput->tokenLength;

            // 1. 设置 embedding 网络输入
            ret = ::bm_memcpy_s2d_partial_offset(
                mHandle, mEmbed->stages[0].input_mems[0], mNetworkInput->tokenBuffer.get(),
                static_cast<unsigned int>(tokLen * sizeof(decltype(mNetworkInput->tokenBuffer)::element_type)), 0);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during prefill (embedding input)."};
            }

            // 2. 构建因果 attention_mask（下三角=0可attend，上三角=mask），后续每层单独写入
            auto blockAttentionMasks = std::make_unique<uint16_t[]>(tokLen * tokLen);
            {
                std::fill_n(blockAttentionMasks.get(), tokLen * tokLen, mMaskValue);
                for (int32_t i = 0; i < tokLen; i++) {
                    int32_t rowOffset = i * tokLen;
                    for (int32_t j = 0; j <= i; j++) {
                        blockAttentionMasks[rowOffset + j] = 0;
                    }
                }
            }

            // 3. embedding forward
            ForwardNetwork(mEmbed);

            // 4. 逐层 forward（position_ids / attention_mask 每标准层单独写入）
            bm_device_mem_t outputMem = mEmbed->stages[0].output_mems[0];
            for (uint16_t layerIndex = 0; layerIndex < mBlockerLayerSize; layerIndex++) {
                const auto& layer = mLayers[layerIndex];

                // 复制 input_states 到当前层的 dynamic block
                ret = ::bm_memcpy_d2d_byte(mHandle, layer.block->stages[0].input_mems[IDX_STD_INPUT_STATES], 0,
                                           outputMem, 0, tokLen * mHiddenBytes);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during prefill (input_states copy)."};
                }

                // 对标准 attention 层，写入 position_ids 和 attention_mask
                if (layer.type == BlockType::STANDARD_ATTENTION) {
                    ret = ::bm_memcpy_s2d_partial_offset(
                        mHandle, layer.block->stages[0].input_mems[IDX_STD_POSITION_IDS],
                        static_cast<void*>(mNetworkInput->positionIdBuffer.get()),
                        static_cast<unsigned int>(tokLen * 3 *
                                                  sizeof(decltype(mNetworkInput->positionIdBuffer)::element_type)),
                        0);
                    if (ret != BM_SUCCESS) {
                        throw std::runtime_error {"Error occurred during prefill (position_ids)."};
                    }
                    ret = ::bm_memcpy_s2d_partial_offset(
                        mHandle, layer.block->stages[0].input_mems[IDX_STD_ATTENTION_MASK],
                        static_cast<void*>(blockAttentionMasks.get()),
                        static_cast<unsigned int>(tokLen * tokLen * sizeof(uint16_t)), 0);
                    if (ret != BM_SUCCESS) {
                        throw std::runtime_error {"Error occurred during prefill (attention_mask)."};
                    }
                } else {
                    // 线性 attention 层：prefill 前必须把 recurrent state 清零（SSM 初始状态为 0）。
                    // 与 python 参考实现 chat.cpp forward_first 中每个非 FA 层 launch 前的
                    // empty(in_tensors[1]) 完全一致。仅靠 CleanupAllNetworks 一次性清零不够——
                    // 开启了 BM_RUNTIME_SHARE_MEM 后，embedding 及前序层 launch 会复用共享显存，
                    // 可能覆盖后续线性层的 recurrent state I/O，导致 SSM 起始状态为脏数据。
                    int32_t zeroValue = 0;
                    ret = ::bm_memset_device_ext(mHandle, &zeroValue, 1,
                                                 layer.block->stages[0].input_mems[IDX_LIN_DYN_RECURRENT_STATES]);
                    if (ret != BM_SUCCESS) {
                        throw std::runtime_error {"Error occurred during prefill (recurrent_state reset)."};
                    }
                }

                ForwardBlockNetworkWithDynamicLength(layerIndex, tokLen);

                if (layer.type == BlockType::STANDARD_ATTENTION) {
                    // 标准 attention: 拷贝 KV cache 从 dynamic block 到 block_cache
                    ret = ::bm_memcpy_d2d_byte(mHandle, layer.blockCache->stages[0].input_mems[IDX_STD_HISTORY_K], 0,
                                               layer.block->stages[0].output_mems[IDX_STD_K_CACHE], 0,
                                               mNetworkInput->tokenLength * mKVBytes);
                    if (ret != BM_SUCCESS) {
                        throw std::runtime_error {"Error occurred during prefill (history_k copy)."};
                    }
                    ret = ::bm_memcpy_d2d_byte(mHandle, layer.blockCache->stages[0].input_mems[IDX_STD_HISTORY_V], 0,
                                               layer.block->stages[0].output_mems[IDX_STD_V_CACHE], 0,
                                               mNetworkInput->tokenLength * mKVBytes);
                    if (ret != BM_SUCCESS) {
                        throw std::runtime_error {"Error occurred during prefill (history_v copy)."};
                    }
                } else {
                    // 线性 attention: 拷贝 conv_states 从 dynamic block 到 block_cache
                    ret = ::bm_memcpy_d2d_byte(
                        mHandle, layer.blockCache->stages[0].input_mems[IDX_LIN_CACHE_INPUT_CONV_STATE], 0,
                        layer.block->stages[0].output_mems[IDX_LIN_DYN_OUTPUT_CONV_STATES], 0, mConvBytes);
                    if (ret != BM_SUCCESS) {
                        throw std::runtime_error {"Error occurred during prefill (conv_state copy)."};
                    }
                    // 初始化 block_cache 的 recurrent_state（从 dynamic block 零初始化的输入拷贝）
                    ret = ::bm_memcpy_d2d_byte(
                        mHandle, layer.blockCache->stages[0].input_mems[IDX_LIN_CACHE_INPUT_RECURRENT_STATE], 0,
                        layer.block->stages[0].input_mems[IDX_LIN_DYN_RECURRENT_STATES], 0,
                        static_cast<uint32_t>(::bm_mem_get_device_size(
                            layer.blockCache->stages[0].input_mems[IDX_LIN_CACHE_INPUT_RECURRENT_STATE])));
                    if (ret != BM_SUCCESS) {
                        throw std::runtime_error {"Error occurred during prefill (recurrent_state copy)."};
                    }
                }

                outputMem = layer.block->stages[0].output_mems[IDX_STD_OUTPUT_STATES];
            }

            // 5. 取最后一个 token 的 output_states 送入 lm_head
            ret = ::bm_memcpy_d2d_byte(mHandle, mLmHead->stages[0].input_mems[0], 0, outputMem,
                                       (mNetworkInput->tokenLength - 1) * mHiddenBytes, mHiddenBytes);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during prefill (lm_head input)."};
            }
            ForwardNetwork(mLmHead);

            int32_t token = Sample(param);
            mNetworkInput->tokenBuffer[mNetworkInput->tokenLength] = token;
            mNetworkInput->tokenLength++;
        }

        void BMRuntimeQwen35Model::Decode(std::optional<SampleParam> param) {
            assert(mNetworkInput->tokenLength > 0);
            int32_t positionId = mNetworkInput->tokenLength - 1;
            int32_t currentToken = mNetworkInput->tokenBuffer[positionId];

            // 更新 attention_mask：仅解除对“上一个 token”所在 KV slot 的屏蔽。
            // 当前 token 的 KV 本步会写入 slot positionId，因此该 slot 在本步仍需保持屏蔽，
            // 当前 token 通过 mask 末尾 (index == mMaxTokenLength) 那一位做自注意力（恒为 0）。
            // 这与 python 参考实现 forward_next 完全一致：
            //   for (i = history_length-1; i < SEQLEN; i++) mask[i] = mask_value;
            // 其中 history_length == tokenLength，即 slot [positionId, SEQLEN-1] 屏蔽。
            mNetworkInput->attentionMaskBuffer[positionId - 1] = 0;

            bm_status_t ret {};

            // 1. 设置 embedding_cache 网络输入
            ret = ::bm_memcpy_s2d_partial_offset(mHandle, mEmbedCache->stages[0].input_mems[0],
                                                 static_cast<void*>(&currentToken), sizeof(decltype(currentToken)), 0);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during decode (embedding_cache input)."};
            }

            // 2. embedding_cache forward
            ForwardNetwork(mEmbedCache);

            // 3. 逐层 forward block_cache（position_ids / attention_mask 每层单独写入）
            int32_t positionIds3D[3] = {positionId, positionId, positionId};
            int32_t kvOffset = (mNetworkInput->tokenLength - 1) * mKVBytes;
            bm_device_mem_t outputMem = mEmbedCache->stages[0].output_mems[0];
            for (uint16_t layerIndex = 0; layerIndex < mBlockerLayerSize; layerIndex++) {
                ForwardBlockCacheNetworkForDecode(layerIndex, outputMem, positionIds3D, kvOffset);
                outputMem = mLayers[layerIndex].blockCache->stages[0].output_mems[IDX_STD_OUTPUT_STATES];
            }

            // 5. lm_head
            ret = ::bm_memcpy_d2d_byte(mHandle, mLmHead->stages[0].input_mems[0], 0, outputMem, 0, mHiddenBytes);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during decode (lm_head input)."};
            }
            ForwardNetwork(mLmHead);

            int32_t token = Sample(param);
            mNetworkInput->tokenBuffer[mNetworkInput->tokenLength] = token;
            mNetworkInput->tokenLength++;
        }

        // ========================================================================
        // ForwardNetwork — 通用，与 qwen3 完全一致
        // ========================================================================

        void BMRuntimeQwen35Model::ForwardNetwork(const bm_net_info_t* network) {
            std::unique_ptr<bm_tensor_t[]> inputTensors = std::make_unique<bm_tensor_t[]>(network->input_num);
            std::unique_ptr<bm_tensor_t[]> outputTensors = std::make_unique<bm_tensor_t[]>(network->output_num);

            for (int i = 0; i < network->input_num; i++) {
                ::bmrt_tensor_with_device(&(inputTensors[i]), network->stages[0].input_mems[i],
                                          network->input_dtypes[i], network->stages[0].input_shapes[i]);
            }
            for (int i = 0; i < network->output_num; i++) {
                ::bmrt_tensor_with_device(&(outputTensors[i]), network->stages[0].output_mems[i],
                                          network->output_dtypes[i], network->stages[0].output_shapes[i]);
            }

            bool ok = ::bmrt_launch_tensor_ex(mpBmrt, network->name, inputTensors.get(), network->input_num,
                                              outputTensors.get(), network->output_num, true, false);
            if (!ok) {
                throw std::runtime_error {"Error occurred when forward network " + std::string {network->name} + "."};
            }

            bm_status_t ret = ::bm_thread_sync(mHandle);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred when forward network " + std::string {network->name} + "."};
            }
        }

        // ========================================================================
        // ForwardBlockNetworkWithDynamicLength — 混合 attention 版本
        // ========================================================================

        void BMRuntimeQwen35Model::ForwardBlockNetworkWithDynamicLength(uint16_t layerIndex, int32_t actual_length) {
            assert(layerIndex < mBlockerLayerSize);
            const auto& layer = mLayers[layerIndex];
            const auto& block = layer.block;

            if (layer.type == BlockType::STANDARD_ATTENTION) {
                // 标准 attention: 3 inputs, 3 outputs（每层已单独写入 position_ids / attention_mask）
                std::array<bm_tensor_t, 3> inputTensors {};
                std::array<bm_tensor_t, 3> outputTensors {};

                ::bmrt_tensor_with_device(
                    &(inputTensors[IDX_STD_INPUT_STATES]), block->stages[0].input_mems[IDX_STD_INPUT_STATES],
                    block->input_dtypes[IDX_STD_INPUT_STATES], block->stages[0].input_shapes[IDX_STD_INPUT_STATES]);
                ::bmrt_tensor_with_device(
                    &(inputTensors[IDX_STD_POSITION_IDS]), block->stages[0].input_mems[IDX_STD_POSITION_IDS],
                    block->input_dtypes[IDX_STD_POSITION_IDS], block->stages[0].input_shapes[IDX_STD_POSITION_IDS]);
                ::bmrt_tensor_with_device(
                    &(inputTensors[IDX_STD_ATTENTION_MASK]), block->stages[0].input_mems[IDX_STD_ATTENTION_MASK],
                    block->input_dtypes[IDX_STD_ATTENTION_MASK], block->stages[0].input_shapes[IDX_STD_ATTENTION_MASK]);

                ::bmrt_tensor_with_device(
                    &(outputTensors[IDX_STD_OUTPUT_STATES]), block->stages[0].output_mems[IDX_STD_OUTPUT_STATES],
                    block->output_dtypes[IDX_STD_OUTPUT_STATES], block->stages[0].output_shapes[IDX_STD_OUTPUT_STATES]);
                ::bmrt_tensor_with_device(
                    &(outputTensors[IDX_STD_K_CACHE]), block->stages[0].output_mems[IDX_STD_K_CACHE],
                    block->output_dtypes[IDX_STD_K_CACHE], block->stages[0].output_shapes[IDX_STD_K_CACHE]);
                ::bmrt_tensor_with_device(
                    &(outputTensors[IDX_STD_V_CACHE]), block->stages[0].output_mems[IDX_STD_V_CACHE],
                    block->output_dtypes[IDX_STD_V_CACHE], block->stages[0].output_shapes[IDX_STD_V_CACHE]);

                inputTensors[IDX_STD_INPUT_STATES].shape.dims[1] = static_cast<int>(actual_length);
                inputTensors[IDX_STD_POSITION_IDS].shape.dims[1] = static_cast<int>(actual_length);
                inputTensors[IDX_STD_ATTENTION_MASK].shape.dims[2] = static_cast<int>(actual_length);
                inputTensors[IDX_STD_ATTENTION_MASK].shape.dims[3] = static_cast<int>(actual_length);

                bool ok = ::bmrt_launch_tensor_ex(mpBmrt, block->name, inputTensors.data(), inputTensors.size(),
                                                  outputTensors.data(), outputTensors.size(), true, false);
                if (!ok) {
                    throw std::runtime_error {
                        "Error occurred when forward block network with dynamic length (standard)."};
                }
            } else {
                // 线性 attention: 2 inputs, 2 outputs
                std::array<bm_tensor_t, 2> inputTensors {};
                std::array<bm_tensor_t, 2> outputTensors {};

                ::bmrt_tensor_with_device(&(inputTensors[IDX_LIN_DYN_INPUT_STATES]),
                                          block->stages[0].input_mems[IDX_LIN_DYN_INPUT_STATES],
                                          block->input_dtypes[IDX_LIN_DYN_INPUT_STATES],
                                          block->stages[0].input_shapes[IDX_LIN_DYN_INPUT_STATES]);
                ::bmrt_tensor_with_device(&(inputTensors[IDX_LIN_DYN_RECURRENT_STATES]),
                                          block->stages[0].input_mems[IDX_LIN_DYN_RECURRENT_STATES],
                                          block->input_dtypes[IDX_LIN_DYN_RECURRENT_STATES],
                                          block->stages[0].input_shapes[IDX_LIN_DYN_RECURRENT_STATES]);

                ::bmrt_tensor_with_device(&(outputTensors[IDX_LIN_DYN_OUTPUT_STATES]),
                                          block->stages[0].output_mems[IDX_LIN_DYN_OUTPUT_STATES],
                                          block->output_dtypes[IDX_LIN_DYN_OUTPUT_STATES],
                                          block->stages[0].output_shapes[IDX_LIN_DYN_OUTPUT_STATES]);
                ::bmrt_tensor_with_device(&(outputTensors[IDX_LIN_DYN_OUTPUT_CONV_STATES]),
                                          block->stages[0].output_mems[IDX_LIN_DYN_OUTPUT_CONV_STATES],
                                          block->output_dtypes[IDX_LIN_DYN_OUTPUT_CONV_STATES],
                                          block->stages[0].output_shapes[IDX_LIN_DYN_OUTPUT_CONV_STATES]);

                inputTensors[IDX_LIN_DYN_INPUT_STATES].shape.dims[1] = static_cast<int>(actual_length);

                bool ok = ::bmrt_launch_tensor_ex(mpBmrt, block->name, inputTensors.data(), inputTensors.size(),
                                                  outputTensors.data(), outputTensors.size(), true, false);
                if (!ok) {
                    throw std::runtime_error {
                        "Error occurred when forward block network with dynamic length (linear)."};
                }
            }

            bm_status_t syncRet = ::bm_thread_sync(mHandle);
            if (syncRet != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred when forward block network with dynamic length."};
            }
        }

        // ========================================================================
        // ForwardBlockCacheNetworkForDecode — 混合 attention 版本
        // ========================================================================

        void BMRuntimeQwen35Model::ForwardBlockCacheNetworkForDecode(uint16_t layerIndex, bm_device_mem_t inputMem,
                                                                     const int32_t* positionIds3D, int32_t kvOffset) {
            assert(layerIndex < mBlockerLayerSize);
            const auto& layer = mLayers[layerIndex];
            const auto& blockCache = layer.blockCache;

            if (layer.type == BlockType::STANDARD_ATTENTION) {
                // 标准 attention: 5 inputs, 3 outputs，每层单独写入 position_ids / attention_mask
                std::array<bm_tensor_t, 5> inputTensors {};
                std::array<bm_tensor_t, 3> outputTensors {};

                ::bmrt_tensor_with_device(&(inputTensors[IDX_STD_INPUT_STATES]), inputMem,
                                          blockCache->input_dtypes[IDX_STD_INPUT_STATES],
                                          blockCache->stages[0].input_shapes[IDX_STD_INPUT_STATES]);

                ::bmrt_tensor_with_device(&(inputTensors[IDX_STD_POSITION_IDS]),
                                          blockCache->stages[0].input_mems[IDX_STD_POSITION_IDS],
                                          blockCache->input_dtypes[IDX_STD_POSITION_IDS],
                                          blockCache->stages[0].input_shapes[IDX_STD_POSITION_IDS]);
                bm_memcpy_s2d(mHandle, inputTensors[IDX_STD_POSITION_IDS].device_mem,
                              (void*)(const_cast<int32_t*>(positionIds3D)));

                ::bmrt_tensor_with_device(&(inputTensors[IDX_STD_ATTENTION_MASK]),
                                          blockCache->stages[0].input_mems[IDX_STD_ATTENTION_MASK],
                                          blockCache->input_dtypes[IDX_STD_ATTENTION_MASK],
                                          blockCache->stages[0].input_shapes[IDX_STD_ATTENTION_MASK]);
                bm_memcpy_s2d(mHandle, inputTensors[IDX_STD_ATTENTION_MASK].device_mem,
                              (void*)mNetworkInput->attentionMaskBuffer.get());

                ::bmrt_tensor_with_device(
                    &(inputTensors[IDX_STD_HISTORY_K]), blockCache->stages[0].input_mems[IDX_STD_HISTORY_K],
                    blockCache->input_dtypes[IDX_STD_HISTORY_K], blockCache->stages[0].input_shapes[IDX_STD_HISTORY_K]);
                ::bmrt_tensor_with_device(
                    &(inputTensors[IDX_STD_HISTORY_V]), blockCache->stages[0].input_mems[IDX_STD_HISTORY_V],
                    blockCache->input_dtypes[IDX_STD_HISTORY_V], blockCache->stages[0].input_shapes[IDX_STD_HISTORY_V]);

                bm_device_mem_t kCacheMem = ::bm_mem_from_device(
                    blockCache->stages[0].input_mems[IDX_STD_HISTORY_K].u.device.device_addr + kvOffset, mKVBytes);
                bm_device_mem_t vCacheMem = ::bm_mem_from_device(
                    blockCache->stages[0].input_mems[IDX_STD_HISTORY_V].u.device.device_addr + kvOffset, mKVBytes);

                ::bmrt_tensor_with_device(&(outputTensors[IDX_STD_OUTPUT_STATES]),
                                          blockCache->stages[0].output_mems[IDX_STD_OUTPUT_STATES],
                                          blockCache->output_dtypes[IDX_STD_OUTPUT_STATES],
                                          blockCache->stages[0].output_shapes[IDX_STD_OUTPUT_STATES]);
                ::bmrt_tensor_with_device(&(outputTensors[IDX_STD_K_CACHE]), kCacheMem,
                                          blockCache->output_dtypes[IDX_STD_K_CACHE],
                                          blockCache->stages[0].output_shapes[IDX_STD_K_CACHE]);
                ::bmrt_tensor_with_device(&(outputTensors[IDX_STD_V_CACHE]), vCacheMem,
                                          blockCache->output_dtypes[IDX_STD_V_CACHE],
                                          blockCache->stages[0].output_shapes[IDX_STD_V_CACHE]);

                bool ok = ::bmrt_launch_tensor_ex(mpBmrt, blockCache->name, inputTensors.data(), inputTensors.size(),
                                                  outputTensors.data(), outputTensors.size(), true, false);
                if (!ok) {
                    throw std::runtime_error {"Error occurred when forward block cache network for decode (standard)."};
                }
            } else {
                // 线性 attention: 3 inputs, 2 outputs
                std::array<bm_tensor_t, 3> inputTensors {};
                std::array<bm_tensor_t, 2> outputTensors {};

                ::bmrt_tensor_with_device(&(inputTensors[IDX_LIN_CACHE_INPUT_STATES]), inputMem,
                                          blockCache->input_dtypes[IDX_LIN_CACHE_INPUT_STATES],
                                          blockCache->stages[0].input_shapes[IDX_LIN_CACHE_INPUT_STATES]);
                ::bmrt_tensor_with_device(&(inputTensors[IDX_LIN_CACHE_INPUT_CONV_STATE]),
                                          blockCache->stages[0].input_mems[IDX_LIN_CACHE_INPUT_CONV_STATE],
                                          blockCache->input_dtypes[IDX_LIN_CACHE_INPUT_CONV_STATE],
                                          blockCache->stages[0].input_shapes[IDX_LIN_CACHE_INPUT_CONV_STATE]);
                ::bmrt_tensor_with_device(&(inputTensors[IDX_LIN_CACHE_INPUT_RECURRENT_STATE]),
                                          blockCache->stages[0].input_mems[IDX_LIN_CACHE_INPUT_RECURRENT_STATE],
                                          blockCache->input_dtypes[IDX_LIN_CACHE_INPUT_RECURRENT_STATE],
                                          blockCache->stages[0].input_shapes[IDX_LIN_CACHE_INPUT_RECURRENT_STATE]);

                ::bmrt_tensor_with_device(&(outputTensors[IDX_LIN_CACHE_OUTPUT_STATES]),
                                          blockCache->stages[0].output_mems[IDX_LIN_CACHE_OUTPUT_STATES],
                                          blockCache->output_dtypes[IDX_LIN_CACHE_OUTPUT_STATES],
                                          blockCache->stages[0].output_shapes[IDX_LIN_CACHE_OUTPUT_STATES]);
                ::bmrt_tensor_with_device(&(outputTensors[IDX_LIN_CACHE_OUTPUT_CONV_STATE]),
                                          blockCache->stages[0].output_mems[IDX_LIN_CACHE_OUTPUT_CONV_STATE],
                                          blockCache->output_dtypes[IDX_LIN_CACHE_OUTPUT_CONV_STATE],
                                          blockCache->stages[0].output_shapes[IDX_LIN_CACHE_OUTPUT_CONV_STATE]);

                bool ok = ::bmrt_launch_tensor_ex(mpBmrt, blockCache->name, inputTensors.data(), inputTensors.size(),
                                                  outputTensors.data(), outputTensors.size(), true, false);
                if (!ok) {
                    throw std::runtime_error {"Error occurred when forward block cache network for decode (linear)."};
                }
            }

            bm_status_t syncRet = ::bm_thread_sync(mHandle);
            if (syncRet != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred when forward block cache network for decode."};
            }
        }

        // ========================================================================
        // Sample — 与 qwen3 完全一致
        // ========================================================================

        int32_t BMRuntimeQwen35Model::Sample(std::optional<SampleParam> param) {
            bm_status_t ret {};
            int32_t token {0};

            if (mLmHead->stages[0].output_shapes[0].dims[1] == 1) {
                ret = ::bm_memcpy_d2s_partial_offset(mHandle, static_cast<void*>(&token),
                                                     mLmHead->stages[0].output_mems[0], sizeof(decltype(token)), 0);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during sample."};
                }
                return token;
            }

            SampleStrategy sampleStrategy = param ? SampleStrategy::SAMPLE : mSampleStrategy;
            if (sampleStrategy == SampleStrategy::SAMPLE && mSampleHead == nullptr) {
                sampleStrategy = SampleStrategy::GREEDY;
            } else if (sampleStrategy == SampleStrategy::GREEDY && mGreedyHead == nullptr) {
                sampleStrategy = SampleStrategy::SAMPLE;
            }

            bm_device_mem_t lmHeadOutMem = mLmHead->stages[0].output_mems[0];
            uint32_t memSize = static_cast<uint32_t>(::bm_mem_get_device_size(lmHeadOutMem));
            if (sampleStrategy == SampleStrategy::GREEDY) {
                ret = ::bm_memcpy_d2d_byte(mHandle, mGreedyHead->stages[0].input_mems[0], 0, lmHeadOutMem, 0, memSize);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during sample."};
                }
                ForwardNetwork(mGreedyHead);
                ret = ::bm_memcpy_d2s_partial_offset(mHandle, static_cast<void*>(&token),
                                                     mGreedyHead->stages[0].output_mems[0], sizeof(decltype(token)), 0);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during sample."};
                }
            } else if (sampleStrategy == SampleStrategy::SAMPLE) {
                SampleParam sampleParam = param ? param.value() : mDefaultSampleParam;
                if (sampleParam.topK < 1 || sampleParam.topK >= mSampleHead->stages[0].output_shapes[0].dims[1]) {
                    sampleParam.topK = mSampleHead->stages[0].output_shapes[0].dims[1];
                }
                sampleParam.temperature = std::max(sampleParam.temperature, 0.0f);
                sampleParam.repetitionPenalty = std::max(sampleParam.repetitionPenalty, 0.0f);
                sampleParam.topP = std::max(sampleParam.topP, 0.0f);
                sampleParam.topP = std::min(sampleParam.topP, 1.0f);

                bm_device_mem_t logitsMem = mSampleHead->stages[0].input_mems[0];
                bm_device_mem_t inputIdsMem = mSampleHead->stages[0].input_mems[1];
                bm_device_mem_t penaltyMem = mSampleHead->stages[0].input_mems[2];
                bm_device_mem_t temperatureMem = mSampleHead->stages[0].input_mems[3];
                bm_device_mem_t topKMem = mSampleHead->stages[0].input_mems[4];
                bm_device_mem_t topPMem = mSampleHead->stages[0].input_mems[5];

                ret = ::bm_memcpy_d2d_byte(mHandle, logitsMem, 0, lmHeadOutMem, 0, memSize);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during sample."};
                }

                ret = ::bm_memcpy_s2d_partial_offset(
                    mHandle, inputIdsMem, static_cast<void*>(mNetworkInput->tokenBuffer.get()),
                    static_cast<unsigned int>(sizeof(decltype(mNetworkInput->tokenBuffer)::element_type) *
                                              mMaxTokenLength),
                    0);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during sample."};
                }
                ret = ::bm_memcpy_s2d_partial_offset(mHandle, penaltyMem,
                                                     static_cast<void*>(&(sampleParam.repetitionPenalty)),
                                                     sizeof(decltype(sampleParam.repetitionPenalty)), 0);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during sample."};
                }
                ret = ::bm_memcpy_s2d_partial_offset(mHandle, temperatureMem,
                                                     static_cast<void*>(&(sampleParam.temperature)),
                                                     sizeof(decltype(sampleParam.temperature)), 0);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during sample."};
                }
                ret = ::bm_memcpy_s2d_partial_offset(mHandle, topKMem, static_cast<void*>(&(sampleParam.topK)),
                                                     sizeof(decltype(sampleParam.topK)), 0);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during sample."};
                }
                ret = ::bm_memcpy_s2d_partial_offset(mHandle, topPMem, static_cast<void*>(&(sampleParam.topP)),
                                                     sizeof(decltype(sampleParam.topP)), 0);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during sample."};
                }

                ForwardNetwork(mSampleHead);

                std::unique_ptr<float[]> probs = std::make_unique<float[]>(sampleParam.topK);
                std::unique_ptr<int32_t[]> tokens = std::make_unique<int32_t[]>(sampleParam.topK);

                ret = ::bm_memcpy_d2s_partial_offset(
                    mHandle, probs.get(), mSampleHead->stages[0].output_mems[0],
                    static_cast<unsigned int>(sizeof(decltype(probs)::element_type) * sampleParam.topK), 0);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during sample."};
                }
                ret = ::bm_memcpy_d2s_partial_offset(
                    mHandle, tokens.get(), mSampleHead->stages[0].output_mems[1],
                    static_cast<unsigned int>(sizeof(decltype(tokens)::element_type) * sampleParam.topK), 0);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during sample."};
                }

                std::discrete_distribution<int32_t> distribution {probs.get(), probs.get() + sampleParam.topK};

                int32_t index = distribution(mGenerator);
                token = tokens[index];
            } else {
                assert(false);
            }

            return token;
        }

        std::shared_ptr<Model> CreateQwen35BmruntimeModel(const std::string& tokenizerPath,
                                                          const std::string& configPath, const std::string& modelPath) {
            return std::make_shared<BMRuntimeQwen35Model>(tokenizerPath, configPath, modelPath);
        }

    }  // namespace lms
}  // namespace qifeng
