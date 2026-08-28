#include <cassert>
#include <chrono>
#include <filesystem>
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
#include "lms/model/private_include/qwen3_bmruntime.h"

namespace qifeng {
    namespace lms {

        constexpr const char* UTF8_FFFD = "\xef\xbf\xbd";

        constexpr const char* DEFAULT_SYSTEM_PROMPT = "You are a helpful assistant.";

        constexpr std::size_t IDX_BLOCK_INPUT_STATES = 0;
        constexpr std::size_t IDX_BLOCK_POSITION_IDS = 1;
        constexpr std::size_t IDX_BLOCK_ATTENTION_MASK = 2;
        constexpr std::size_t IDX_BLOCK_HISTORY_K = 3;
        constexpr std::size_t IDX_BLOCK_HISTORY_V = 4;

        constexpr std::size_t IDX_BLOCK_OUTPUT_STATES = 0;
        constexpr std::size_t IDX_BLOCK_K_CACHE = 1;
        constexpr std::size_t IDX_BLOCK_V_CACHE = 2;

        constexpr uint16_t FLOAT16_MASK_VALUE = 0xF0E2;
        constexpr uint16_t BFLOAT16_MASK_VALUE = 0xC61C;

        /**
         * 从 Json::Value 中解析采样参数 (repetition_penalty, temperature, top_p, top_k)。
         */
        static BMRuntimeQwen3Model::SampleParam parseSampleParamFromJson(const Json::Value& config) {
            float repetitionPenalty {1.0f};
            float temperature {};
            float topP {};
            int32_t topK {0};

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

            return BMRuntimeQwen3Model::SampleParam {repetitionPenalty, temperature, topP, topK};
        }

        /**
         * 完整复刻 chat_template.jinja 的全部渲染逻辑。
         * 输入 / 输出均为 UTF-8 编码。
         */
        std::string renderChatTemplate(const BMRuntimeQwen3ModelContext::ChatTemplateInput& in) {
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

            auto stripNewline = [](const std::string& s) -> std::string {
                auto start = s.find_first_not_of('\n');
                if (start == std::string::npos) {
                    return {};
                }
                auto end = s.find_last_not_of('\n');
                return s.substr(start, end - start + 1);
            };

            const auto& msgs = in.messages;
            const auto& tools = in.tools;

            if (msgs.empty()) {
                throw std::runtime_error {"Messages in input of chat template can not be empty."};
            }

            std::string out {};

            // ========================================================================
            // Phase 1: System 消息 + Tools 块
            // 对应模板第 1-16 行:
            //   {%- if tools %} ... {%- else %} ... {%- endif %}
            // ========================================================================

            if (!tools.empty()) {
                // ---- 有 tools: 把首个 system 消息嵌入 tools 块 ----
                fmt::format_to(std::back_inserter(out), "<|im_start|>system\n");

                if (msgs[0].role == "system") {
                    fmt::format_to(std::back_inserter(out), "{}\n\n", msgs[0].content);
                }

                out += R"(# Tools

You may call one or more functions to assist with the user query.

You are provided with function signatures within <tools></tools> XML tags:
<tools>)";

                for (const auto& tool : tools) {
                    fmt::format_to(std::back_inserter(out), "\n{}",
                                   common::utils::JsonToCompactString(tool));  // tojson 等价操作
                }

                out += R"(
</tools>

For each function call, return a json object with function name and arguments within <tool_call></tool_call> XML tags:
<tool_call>
{"name": <function-name>, "arguments": <args-json-object>}
</tool_call><|im_end|>
)";
            } else {
                // ---- 无 tools: 直接输出首个 system 消息 ----
                if (msgs[0].role == "system") {
                    fmt::format_to(std::back_inserter(out), "<|im_start|>system\n{}<|im_end|>\n", msgs[0].content);
                }
            }

            // ========================================================================
            // Phase 2: 查找最后一个 "真正" 的用户提问
            // 对应模板第 17-24 行:
            //   {%- set ns = namespace(multi_step_tool=true, last_query_index=...) %}
            //   {%- for message in messages[::-1] %}
            //       ... 找到 role=="user" 且 content 非 <tool_response> 包裹的消息
            // ========================================================================
            //
            // 目的: 区分"历史对话"和"当前轮次"。
            //   last_query_index 之前的 assistant 消息不带 <think> 包装 (历史),
            //   last_query_index 之后的 assistant 消息可能带 <think> 包装 (当前轮次)。

            // 默认值: 若没有找到真实用户提问, 则所有消息视为"当前轮次之后"
            // (Jinja 中 messages|length - 1 对空列表为 -1, 后续 index > -1 恒成立)
            int64_t lastQueryIndex = static_cast<int64_t>(msgs.size()) - 1;

            // 预编译 RE2 模式 (线程安全的一次性初始化, C++11 保证):
            //   toolResponsePattern: FullMatch, 判断是否 <tool_response>...</tool_response> 包裹
            //   thinkEndPattern:     PartialMatch, 判断 content 中是否包含 </think>
            // (?s) → DOTALL: . 匹配换行
            static const RE2 toolResponsePattern {R"((?s)<tool_response>.*</tool_response>)"};
            static const RE2 thinkEndPattern {"</think>"};

            // 倒序遍历
            for (size_t ri = msgs.size(); ri > 0; --ri) {
                const auto& msg = msgs[ri - 1];
                int64_t idx = static_cast<int64_t>(ri - 1);

                if (msg.role == "user") {
                    // 检查是否为 tool_response 包裹 (使用 re2)
                    bool isToolResp = RE2::FullMatch(msg.content, toolResponsePattern);
                    if (!isToolResp) {
                        lastQueryIndex = idx;
                        break;
                    }
                }
            }

            // ========================================================================
            // Phase 3: 逐条渲染消息
            // 对应模板第 25-83 行:
            //   {%- for message in messages %} ... {%- endfor %}
            // ========================================================================

            for (size_t i = 0; i < msgs.size(); ++i) {
                const auto& msg = msgs[i];

                // -- 确保 content 为字符串 (对应 message.content is string 检查) --
                // 我们的 ChatMessage::content 恒为 std::string, 所以始终为真。
                std::string content = msg.content;

                // ================================================================
                // 3a. user 消息 或 非首条 system 消息
                //     模板: {%- if (message.role == "user") or
                //                  (message.role == "system" and not loop.first) %}
                // ================================================================
                if (msg.role == "user" || (msg.role == "system" && i != 0)) {
                    fmt::format_to(std::back_inserter(out), "<|im_start|>{}\n{}<|im_end|>\n", msg.role, content);
                }
                // ================================================================
                // 3b. assistant 消息
                // ================================================================
                else if (msg.role == "assistant") {
                    // --- 提取 reasoning_content ---
                    // 模板第 34-42 行:
                    //   {%- if message.reasoning_content is string %}
                    //       {%- set reasoning_content = message.reasoning_content %}
                    //   {%- else %}
                    //       {%- if '</think>' in content %}
                    //           从 content 中解析 <think>...</think>
                    //       {%- endif %}
                    //   {%- endif %}

                    std::string reasoning {};

                    if (msg.reasoningContent.has_value()) {
                        reasoning = msg.reasoningContent.value();
                    } else {
                        // 使用 re2 检查 content 中是否包含 </think>
                        if (RE2::PartialMatch(content, thinkEndPattern)) {
                            // content.split('</think>')[0]
                            auto endPos = content.find("</think>");
                            std::string before = content.substr(0, endPos);
                            before = rstripNewline(before);

                            // .split('<think>')[-1]
                            auto thinkPos = before.rfind("<think>");
                            if (thinkPos != std::string::npos) {
                                reasoning = before.substr(thinkPos + 7);  // 7 = len("<think>")
                                reasoning = lstripNewline(reasoning);
                            }

                            // content = content.split('</think>')[-1].lstrip('\n')
                            auto lastEnd = content.rfind("</think>");
                            content = content.substr(lastEnd + 8);  // 8 = len("</think>")
                            content = lstripNewline(content);
                        }
                    }

                    // --- 渲染 assistant 消息头 ---
                    // 模板第 43-51 行:
                    //   {%- if loop.index0 > ns.last_query_index %}
                    //       {%- if loop.last or (not loop.last and reasoning_content) %}
                    //           <think> 包装
                    //       {%- else %}
                    //           直接输出 content
                    //       {%- endif %}
                    //   {%- else %}
                    //       直接输出 content
                    //   {%- endif %}

                    bool afterLastQuery = (static_cast<int64_t>(i) > lastQueryIndex);
                    bool isLastMsg = (i == msgs.size() - 1);

                    if (afterLastQuery) {
                        // loop.last or (not loop.last and reasoning_content)
                        // 简化: 是最后一条消息 或 有思考内容
                        if (isLastMsg || !reasoning.empty()) {
                            fmt::format_to(std::back_inserter(out),
                                           "<|im_start|>assistant\n<think>\n{}\n</think>\n\n{}",
                                           stripNewline(reasoning), lstripNewline(content));
                        } else {
                            fmt::format_to(std::back_inserter(out), "<|im_start|>assistant\n{}", content);
                        }
                    } else {
                        // 历史消息: 不带 <think> 包装
                        fmt::format_to(std::back_inserter(out), "<|im_start|>assistant\n{}", content);
                    }

                    // --- 渲染 tool_calls ---
                    // 模板第 52-70 行:
                    //   {%- if message.tool_calls %}
                    //       {%- for tool_call in message.tool_calls %}
                    //           前导换行条件: (loop.first and content) or (not loop.first)
                    //           tool_call.function 解包 (已在 ToolCall 构造时完成)
                    //           输出 <tool_call> JSON
                    //       {%- endfor %}
                    //   {%- endif %}

                    if (!msg.toolCalls.empty()) {
                        for (size_t tcIndex = 0; tcIndex < msg.toolCalls.size(); ++tcIndex) {
                            const auto& tc = msg.toolCalls[tcIndex];

                            // 前导换行:
                            //   - 第一个 tool_call 且 content 非空 → 换行隔开
                            //   - 非第一个 tool_call → 换行隔开
                            bool needNewline = (tcIndex == 0 && !content.empty()) || (tcIndex != 0);
                            if (needNewline) {
                                out += "\n";
                            }

                            auto argsStr = tc.arguments.isString() ? tc.arguments.asString()
                                                                   : common::utils::JsonToCompactString(tc.arguments);
                            fmt::format_to(std::back_inserter(out),
                                           R"(<tool_call>
{{"name": {}, "arguments": {}}}
</tool_call>)",
                                           tc.name, argsStr);
                        }
                    }

                    out += "<|im_end|>\n";
                }
                // ================================================================
                // 3c. tool 消息 — 连续 tool 消息共享 <|im_start|>user / <|im_end|>
                //     模板第 72-81 行:
                //       {%- if loop.first or (messages[loop.index0 - 1].role != "tool") %}
                //           {{- '<|im_start|>user' }}
                //       {%- endif %}
                //       ... tool_response 内容 ...
                //       {%- if loop.last or (messages[loop.index0 + 1].role != "tool") %}
                //           {{- '<|im_end|>\n' }}
                //       {%- endif %}
                // ================================================================
                else if (msg.role == "tool") {
                    bool prevIsTool = (i > 0 && msgs[i - 1].role == "tool");
                    bool nextIsTool = (i + 1 < msgs.size() && msgs[i + 1].role == "tool");

                    // 连续 tool 消息块的开头
                    if (i == 0 || !prevIsTool) {
                        out += "<|im_start|>user";
                    }

                    fmt::format_to(std::back_inserter(out), "\n<tool_response>\n{}\n</tool_response>", content);

                    // 连续 tool 消息块的结尾
                    if (i == msgs.size() - 1 || !nextIsTool) {
                        out += "<|im_end|>\n";
                    }
                }
            }

            // ========================================================================
            // Phase 4: 生成提示 (add_generation_prompt)
            // 对应模板第 84-89 行:
            //   {%- if add_generation_prompt %}
            //       {{- '<|im_start|>assistant\n' }}
            //       {%- if enable_thinking is defined and enable_thinking is false %}
            //           {{- '<think>\n\n</think>\n\n' }}
            //       {%- endif %}
            //   {%- endif %}
            // ========================================================================

            if (in.addGenerationPrompt) {
                fmt::format_to(std::back_inserter(out), "<|im_start|>assistant\n");
                if (!in.enableThinking) {
                    fmt::format_to(std::back_inserter(out), "<think>\n\n</think>\n\n");
                }
            }

            return out;
        }

        BMRuntimeQwen3ModelContext::ChatTemplateInput::ChatTemplateInput()
            : messages {}, tools {}, addGenerationPrompt {true}, enableThinking {false} {
        }

        BMRuntimeQwen3ModelContext::BMRuntimeQwen3ModelContext(std::shared_ptr<BMRuntimeQwen3Model> model)
            : ModelContext {std::move(model)}, mTemplateInput {}, mGenerationCanceled {false} {
        }

        BMRuntimeQwen3ModelContext::~BMRuntimeQwen3ModelContext() {
        }

        std::string BMRuntimeQwen3ModelContext::Generate(const ModelRequest& request) {
            std::string result {};
            StreamGenerateCallback callback = [this, &result](StreamData data) -> bool {
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

        void BMRuntimeQwen3ModelContext::StreamGenerate(const ModelRequest& request, StreamGenerateCallback callback) {
            DoStreamGenerate(request, callback);
        }

        void BMRuntimeQwen3ModelContext::CancelGeneration() {
            mGenerationCanceled = true;
        }

        void BMRuntimeQwen3ModelContext::Reset() {
            mTemplateInput = ChatTemplateInput {};
        }

        bool BMRuntimeQwen3ModelContext::DoStreamGenerate(const ModelRequest& request,
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

            auto modelPtr = std::static_pointer_cast<BMRuntimeQwen3Model>(mModel);

            std::optional<BMRuntimeQwen3Model::SampleParam> generateParam {std::nullopt};
            if (request.configJson.has_value()) {
                Json::Value configJson;
                Json::Reader reader;
                if (!reader.parse(request.configJson.value(), configJson)) {
                    throw std::runtime_error {"configJson parse failed: " + reader.getFormattedErrorMessages()};
                }

                generateParam = parseSampleParamFromJson(configJson);
            }

            mGenerationCanceled = false;
            bool ret = modelPtr->NonParallelizableStreamGenerate(input, generateParam, proxyCallback);

            templateInput.messages.push_back(ChatMessage {"assistant", output, std::nullopt, std::vector<ToolCall> {}});
            mTemplateInput = templateInput;

            return ret;
        }

        BMRuntimeQwen3Model::BMRuntimeQwen3Model(const std::string& tokenizerPath, const std::string& configPath,
                                                 const std::string& modelPath)
            : mTokenizer {nullptr}, mEosTokens {}, mSampleStrategy {SampleStrategy::GREEDY}, mDefaultSampleParam {1.0,
                                                                                                                  1.0,
                                                                                                                  1.0,
                                                                                                                  0},
              mGenerator {0}, mEmbed {nullptr}, mEmbedCache {nullptr}, mLmHead {nullptr}, mGreedyHead {nullptr},
              mSampleHead {nullptr}, mBlockerLayerSize {0}, mLayers {nullptr}, mHiddenBytes {0}, mKVBytes {0},
              mMaxInputTokenLength {0}, mMaxTokenLength {0}, mMaskValue {0}, mMutexNetwork {}, mNetworkInput {nullptr} {
            try {
                // 读取配置并初始化tokenizer
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
                        if (!eosTokenIdJson.isArray()) {
                            throw std::runtime_error {
                                "Config file param validation error: eos_token_id must be array of integer number."};
                        }
                        std::vector<int32_t> eosTokenIds;
                        for (const auto& elem : eosTokenIdJson) {
                            eosTokenIds.push_back(elem.asInt());
                        }
                        mEosTokens.insert(eosTokenIds.begin(), eosTokenIds.end());

                        if (configJson.isMember("do_sample")) {
                            const Json::Value& doSampleJson = configJson["do_sample"];
                            if (doSampleJson.isBool() && doSampleJson.asBool()) {
                                auto sampleParam = parseSampleParamFromJson(configJson);
                                if (sampleParam.topK < 1) {
                                    throw std::runtime_error {"Sample param validation error: top_k required."};
                                }
                                mDefaultSampleParam = sampleParam;
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
                        throw std::runtime_error {"Net architecture of bmodel is unexpected."};
                    }

                    mEmbedCache = const_cast<bm_net_info_t*>(::bmrt_get_network_info(mpBmrt, "embedding_cache"));
                    if (mEmbedCache == nullptr) {
                        throw std::runtime_error {"Net architecture of bmodel is unexpected."};
                    }

                    mLmHead = const_cast<bm_net_info_t*>(::bmrt_get_network_info(mpBmrt, "lm_head"));
                    if (mLmHead == nullptr) {
                        throw std::runtime_error {"Net architecture of bmodel is unexpected."};
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
                        throw std::runtime_error {"Net architecture of bmodel is unexpected."};
                    }

                    int blockSize = netSize - 3;
                    if (mGreedyHead != nullptr) {
                        blockSize--;
                    }
                    if (mSampleHead != nullptr) {
                        blockSize--;
                    }

                    if (blockSize <= 0 || blockSize % 2 != 0) {
                        throw std::runtime_error {"Net architecture of bmodel is unexpected."};
                    }

                    mBlockerLayerSize = static_cast<uint16_t>(blockSize / 2);
                    mLayers = std::make_unique<BlockNetwork[]>(mBlockerLayerSize);
                    for (uint16_t layerIndex = 0; layerIndex < mBlockerLayerSize; layerIndex++) {
                        std::string blockName {"block_" + std::to_string(layerIndex)};
                        std::string blockCacheName {"block_cache_" + std::to_string(layerIndex)};

                        const bm_net_info_t* block = ::bmrt_get_network_info(mpBmrt, blockName.c_str());
                        if (block == nullptr) {
                            throw std::runtime_error {"Net architecture of bmodel is unexpected."};
                        }

                        const bm_net_info_t* blockCache = ::bmrt_get_network_info(mpBmrt, blockCacheName.c_str());
                        if (blockCache == nullptr) {
                            throw std::runtime_error {"Net architecture of bmodel is unexpected."};
                        }

                        mLayers[layerIndex] = BlockNetwork {block, blockCache};
                    }

                    mHiddenBytes = static_cast<uint32_t>(::bm_mem_get_device_size(
                        mLayers[0].blockCache->stages[0].output_mems[IDX_BLOCK_OUTPUT_STATES]));
                    mKVBytes = static_cast<uint32_t>(
                        ::bm_mem_get_device_size(mLayers[0].blockCache->stages[0].output_mems[IDX_BLOCK_K_CACHE]));
                    mMaxInputTokenLength = static_cast<int32_t>(mEmbed->stages[0].input_shapes[0].dims[1]);
                    mMaxTokenLength = static_cast<int32_t>(
                        mLayers[0].blockCache->stages[0].input_shapes[IDX_BLOCK_HISTORY_K].dims[1]);
                    if (mMaxInputTokenLength > mMaxTokenLength) {
                        throw std::runtime_error {"Net architecture of bmodel is unexpected."};
                    }
                    if (mEmbedCache->output_dtypes[0] == BM_FLOAT16) {
                        mMaskValue = FLOAT16_MASK_VALUE;  // float16
                    } else if (mEmbedCache->output_dtypes[0] == BM_BFLOAT16) {
                        mMaskValue = BFLOAT16_MASK_VALUE;  // -9984 by bfloat16
                    } else {
                        throw std::runtime_error {
                            "Invalid attention dtype. Supported dtype are 'BM_FLOAT16' or 'BM_BFLOAT16'."};
                    }
                    if (!mLayers[0].block->is_dynamic) {
                        throw std::runtime_error {"Only dynamic block is supported."};
                    }
                }
            } catch (...) {
                Finalize();
                throw;
            }
        }

        BMRuntimeQwen3Model::~BMRuntimeQwen3Model() {
            Finalize();
        }

        void BMRuntimeQwen3Model::Finalize() {
            if (mTokenizer != nullptr) {
                ::free_tokenizer(mTokenizer);
                mTokenizer = nullptr;
            }
        }

        std::shared_ptr<ModelContext> BMRuntimeQwen3Model::CreateContext() {
            return std::make_shared<BMRuntimeQwen3ModelContext>(
                std::static_pointer_cast<BMRuntimeQwen3Model>(shared_from_this()));
        }

        std::string BMRuntimeQwen3Model::ModelName() const {
            return "qwen3";
        }

        std::string BMRuntimeQwen3Model::EngineName() const {
            return "bmruntime";
        }

        bool BMRuntimeQwen3Model::NonParallelizableStreamGenerate(const std::string& input,
                                                                  std::optional<SampleParam> param,
                                                                  StreamGenerateCallback callback) {
            assert(callback != nullptr);

            // 不能并发
            std::unique_lock<std::mutex> lock {mMutexNetwork};
            // 初始化公共变量network_input_
            if (mNetworkInput == nullptr) {
                mNetworkInput = std::make_unique<NetworkInput>();
                mNetworkInput->tokenLength = 0;
                mNetworkInput->tokenBuffer = std::make_unique<int32_t[]>(mMaxTokenLength);
                mNetworkInput->positionIdBuffer = std::make_unique<int32_t[]>(mMaxInputTokenLength);
                mNetworkInput->attentionMaskBuffer = std::make_unique<uint16_t[]>(mMaxTokenLength + 1);
            }

            SLOG_DEBUG << "Input = " << input;

            // 然后设置公共变量network_input_
            {
                // tokenizer encode
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
                // 设置token_length
                mNetworkInput->tokenLength = static_cast<int32_t>(result->len);
                // 设置tokens
                std::copy(reinterpret_cast<const int32_t*>(result->token_ids),
                          reinterpret_cast<const int32_t*>(result->token_ids + result->len),
                          mNetworkInput->tokenBuffer.get());
                std::fill_n(mNetworkInput->tokenBuffer.get() + static_cast<int32_t>(result->len),
                            mMaxTokenLength - static_cast<int32_t>(result->len), 0);
                ::free_tokenizer_encode_result(result);
                result = nullptr;

                // 设置position_ids
                for (int32_t i = 0; i < mNetworkInput->tokenLength; i++) {
                    mNetworkInput->positionIdBuffer[i] = i;
                }
                std::fill_n(mNetworkInput->positionIdBuffer.get() + mNetworkInput->tokenLength,
                            mMaxInputTokenLength - mNetworkInput->tokenLength, 0);

                // 设置block_cache_attention_masks
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
                if (word.find(UTF8_FFFD) == std::string::npos) {
                    outputTokens.clear();
                    if (!callback(StreamContent {word})) {
                        return true;
                    }
                }
                // 最后检查是否已经达到上限
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

            SLOG_INFO << "NonParallelizableStreamGenerate finished: "
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

        void BMRuntimeQwen3Model::CleanupAllNetworks() {
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

        void BMRuntimeQwen3Model::CleanupNetwork(const bm_net_info_t* network) {
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

        void BMRuntimeQwen3Model::Prefill(std::optional<SampleParam> param) {
            bm_status_t ret {};
            // 设置embedding网络的输入
            ret = ::bm_memcpy_s2d_partial_offset(
                mHandle, mEmbed->stages[0].input_mems[0], mNetworkInput->tokenBuffer.get(),
                static_cast<unsigned int>(mNetworkInput->tokenLength *
                                          sizeof(decltype(mNetworkInput->tokenBuffer)::element_type)),
                0);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during prefill."};
            }
            // 设置第0层block网络的输入(position_id)，会和后续层共享
            ret = ::bm_memcpy_s2d_partial_offset(
                mHandle, mLayers[0].block->stages[0].input_mems[IDX_BLOCK_POSITION_IDS],
                static_cast<void*>(mNetworkInput->positionIdBuffer.get()),
                static_cast<unsigned int>(mMaxInputTokenLength *
                                          sizeof(decltype(mNetworkInput->positionIdBuffer)::element_type)),
                0);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during prefill."};
            }
            // 设置第0层block网络的输入(attention_mask)，会和后续层共享
            {
                auto blockAttentionMasks = std::make_unique<uint16_t[]>(mNetworkInput->tokenLength);
                std::fill_n(blockAttentionMasks.get(), mNetworkInput->tokenLength, mMaskValue);
                for (int32_t i = 0; i < mNetworkInput->tokenLength; i++) {
                    blockAttentionMasks[i] = 0;

                    // 只设置动态网络需要用到的部分
                    ret = ::bm_memcpy_s2d_partial_offset(
                        mHandle, mLayers[0].block->stages[0].input_mems[IDX_BLOCK_ATTENTION_MASK],
                        static_cast<void*>(blockAttentionMasks.get()),
                        static_cast<unsigned int>(mNetworkInput->tokenLength *
                                                  sizeof(decltype(blockAttentionMasks)::element_type)),
                        static_cast<unsigned int>(i * mNetworkInput->tokenLength *
                                                  sizeof(decltype(blockAttentionMasks)::element_type)));
                    if (ret != BM_SUCCESS) {
                        throw std::runtime_error {"Error occurred during prefill."};
                    }
                }
            }
            // embedding
            ForwardNetwork(mEmbed);

            bm_device_mem_t outputMem = mEmbed->stages[0].output_mems[0];
            for (uint16_t layerIndex = 0; layerIndex < mBlockerLayerSize; layerIndex++) {
                ret = ::bm_memcpy_d2d_byte(mHandle,
                                           mLayers[layerIndex].block->stages[0].input_mems[IDX_BLOCK_INPUT_STATES], 0,
                                           outputMem, 0, mNetworkInput->tokenLength * mHiddenBytes);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during prefill."};
                }

                ForwardBlockNetworkWithDynamicLength(layerIndex, mNetworkInput->tokenLength);

                // 把block的kv拷贝到block_cache的kv cache中
                ret = ::bm_memcpy_d2d_byte(mHandle,
                                           mLayers[layerIndex].blockCache->stages[0].input_mems[IDX_BLOCK_HISTORY_K], 0,
                                           mLayers[layerIndex].block->stages[0].output_mems[IDX_BLOCK_K_CACHE], 0,
                                           mNetworkInput->tokenLength * mKVBytes);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during prefill."};
                }
                ret = ::bm_memcpy_d2d_byte(mHandle,
                                           mLayers[layerIndex].blockCache->stages[0].input_mems[IDX_BLOCK_HISTORY_V], 0,
                                           mLayers[layerIndex].block->stages[0].output_mems[IDX_BLOCK_V_CACHE], 0,
                                           mNetworkInput->tokenLength * mKVBytes);
                if (ret != BM_SUCCESS) {
                    throw std::runtime_error {"Error occurred during prefill."};
                }

                outputMem = mLayers[layerIndex].block->stages[0].output_mems[IDX_BLOCK_OUTPUT_STATES];
            }

            // 取最后一个token的output_states
            ret = ::bm_memcpy_d2d_byte(mHandle, mLmHead->stages[0].input_mems[0], 0, outputMem,
                                       (mNetworkInput->tokenLength - 1) * mHiddenBytes, mHiddenBytes);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during prefill."};
            }
            ForwardNetwork(mLmHead);

            int32_t token = Sample(param);

            mNetworkInput->tokenBuffer[mNetworkInput->tokenLength] = token;

            mNetworkInput->tokenLength++;
        }

        void BMRuntimeQwen3Model::Decode(std::optional<SampleParam> param) {
            assert(mNetworkInput->tokenLength > 0);
            int32_t positionId = mNetworkInput->tokenLength - 1;
            int32_t currentToken = mNetworkInput->tokenBuffer[positionId];

            // 设置block_cache网络的attention_mask
            mNetworkInput->attentionMaskBuffer[positionId] = 0;

            bm_status_t ret {};
            // 设置embed_cache网络的输入
            ret = ::bm_memcpy_s2d_partial_offset(mHandle, mEmbedCache->stages[0].input_mems[0],
                                                 static_cast<void*>(&currentToken), sizeof(decltype(currentToken)), 0);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during decode."};
            }
            // 设置第0层block_cache网络的输入(position_id, attention_mask)
            ret = ::bm_memcpy_s2d_partial_offset(mHandle,
                                                 mLayers[0].blockCache->stages[0].input_mems[IDX_BLOCK_POSITION_IDS],
                                                 static_cast<void*>(&positionId), sizeof(decltype(positionId)), 0);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during decode."};
            }
            ret = ::bm_memcpy_s2d_partial_offset(
                mHandle, mLayers[0].blockCache->stages[0].input_mems[IDX_BLOCK_ATTENTION_MASK],
                static_cast<void*>(mNetworkInput->attentionMaskBuffer.get()),
                static_cast<unsigned int>(sizeof(decltype(mNetworkInput->attentionMaskBuffer)::element_type) *
                                          (mMaxTokenLength + 1)),
                0);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during decode."};
            }

            ForwardNetwork(mEmbedCache);

            bm_device_mem_t outputMem = mEmbedCache->stages[0].output_mems[0];
            for (uint16_t layerIndex = 0; layerIndex < mBlockerLayerSize; layerIndex++) {
                ForwardBlockCacheNetworkForDecode(layerIndex, outputMem);
                outputMem = mLayers[layerIndex].blockCache->stages[0].output_mems[IDX_BLOCK_OUTPUT_STATES];
            }

            ret = ::bm_memcpy_d2d_byte(mHandle, mLmHead->stages[0].input_mems[0], 0, outputMem, 0, mHiddenBytes);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred during decode."};
            }
            ForwardNetwork(mLmHead);

            int32_t token = Sample(param);

            mNetworkInput->tokenBuffer[mNetworkInput->tokenLength] = token;

            mNetworkInput->tokenLength++;
        }

        void BMRuntimeQwen3Model::ForwardNetwork(const bm_net_info_t* network) {
            // 我们的模型只有一个stage
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

        void BMRuntimeQwen3Model::ForwardBlockNetworkWithDynamicLength(uint16_t layerIndex, int32_t actual_length) {
            // 我们的模型只有一个stage
            assert(layerIndex < mBlockerLayerSize);
            auto& block = mLayers[layerIndex].block;

            std::array<bm_tensor_t, 3> inputTensors {};
            std::array<bm_tensor_t, 3> outputTensors {};

            ::bmrt_tensor_with_device(
                &(inputTensors[IDX_BLOCK_INPUT_STATES]), block->stages[0].input_mems[IDX_BLOCK_INPUT_STATES],
                block->input_dtypes[IDX_BLOCK_INPUT_STATES], block->stages[0].input_shapes[IDX_BLOCK_INPUT_STATES]);
            ::bmrt_tensor_with_device(
                &(inputTensors[IDX_BLOCK_POSITION_IDS]), block->stages[0].input_mems[IDX_BLOCK_POSITION_IDS],
                block->input_dtypes[IDX_BLOCK_POSITION_IDS], block->stages[0].input_shapes[IDX_BLOCK_POSITION_IDS]);
            ::bmrt_tensor_with_device(
                &(inputTensors[IDX_BLOCK_ATTENTION_MASK]), block->stages[0].input_mems[IDX_BLOCK_ATTENTION_MASK],
                block->input_dtypes[IDX_BLOCK_ATTENTION_MASK], block->stages[0].input_shapes[IDX_BLOCK_ATTENTION_MASK]);

            ::bmrt_tensor_with_device(
                &(outputTensors[IDX_BLOCK_OUTPUT_STATES]), block->stages[0].output_mems[IDX_BLOCK_OUTPUT_STATES],
                block->output_dtypes[IDX_BLOCK_OUTPUT_STATES], block->stages[0].output_shapes[IDX_BLOCK_OUTPUT_STATES]);
            ::bmrt_tensor_with_device(
                &(outputTensors[IDX_BLOCK_K_CACHE]), block->stages[0].output_mems[IDX_BLOCK_K_CACHE],
                block->output_dtypes[IDX_BLOCK_K_CACHE], block->stages[0].output_shapes[IDX_BLOCK_K_CACHE]);
            ::bmrt_tensor_with_device(
                &(outputTensors[IDX_BLOCK_V_CACHE]), block->stages[0].output_mems[IDX_BLOCK_V_CACHE],
                block->output_dtypes[IDX_BLOCK_V_CACHE], block->stages[0].output_shapes[IDX_BLOCK_V_CACHE]);

            inputTensors[IDX_BLOCK_INPUT_STATES].shape.dims[1] = static_cast<int>(actual_length);
            inputTensors[IDX_BLOCK_POSITION_IDS].shape.dims[1] = static_cast<int>(actual_length);
            inputTensors[IDX_BLOCK_ATTENTION_MASK].shape.dims[2] = static_cast<int>(actual_length);
            inputTensors[IDX_BLOCK_ATTENTION_MASK].shape.dims[3] = static_cast<int>(actual_length);

            bool ok = ::bmrt_launch_tensor_ex(mpBmrt, block->name, inputTensors.data(), inputTensors.size(),
                                              outputTensors.data(), outputTensors.size(), true, false);
            if (!ok) {
                throw std::runtime_error {"Error occurred when forward block network with dynamic length."};
            }

            bm_status_t ret = ::bm_thread_sync(mHandle);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred when forward block network with dynamic length."};
            }
        }

        void BMRuntimeQwen3Model::ForwardBlockCacheNetworkForDecode(uint16_t layerIndex, bm_device_mem_t input_mem) {
            // 我们的模型只有一个stage
            assert(layerIndex < mBlockerLayerSize);
            auto& firstBlockCache = mLayers[0].blockCache;
            auto& blockCache = mLayers[layerIndex].blockCache;

            std::array<bm_tensor_t, 5> inputTensors {};
            std::array<bm_tensor_t, 3> outputTensors {};

            // 设置输入
            ::bmrt_tensor_with_device(&(inputTensors[IDX_BLOCK_INPUT_STATES]), input_mem,
                                      blockCache->input_dtypes[IDX_BLOCK_INPUT_STATES],
                                      blockCache->stages[0].input_shapes[IDX_BLOCK_INPUT_STATES]);

            // 复用第0层输入的position_ids和attention_mask
            ::bmrt_tensor_with_device(&(inputTensors[IDX_BLOCK_POSITION_IDS]),
                                      firstBlockCache->stages[0].input_mems[IDX_BLOCK_POSITION_IDS],
                                      firstBlockCache->input_dtypes[IDX_BLOCK_POSITION_IDS],
                                      firstBlockCache->stages[0].input_shapes[IDX_BLOCK_POSITION_IDS]);
            ::bmrt_tensor_with_device(&(inputTensors[IDX_BLOCK_ATTENTION_MASK]),
                                      firstBlockCache->stages[0].input_mems[IDX_BLOCK_ATTENTION_MASK],
                                      firstBlockCache->input_dtypes[IDX_BLOCK_ATTENTION_MASK],
                                      firstBlockCache->stages[0].input_shapes[IDX_BLOCK_ATTENTION_MASK]);

            // 使用本层的history_k和history_v
            ::bmrt_tensor_with_device(
                &(inputTensors[IDX_BLOCK_HISTORY_K]), blockCache->stages[0].input_mems[IDX_BLOCK_HISTORY_K],
                blockCache->input_dtypes[IDX_BLOCK_HISTORY_K], blockCache->stages[0].input_shapes[IDX_BLOCK_HISTORY_K]);
            ::bmrt_tensor_with_device(
                &(inputTensors[IDX_BLOCK_HISTORY_V]), blockCache->stages[0].input_mems[IDX_BLOCK_HISTORY_V],
                blockCache->input_dtypes[IDX_BLOCK_HISTORY_V], blockCache->stages[0].input_shapes[IDX_BLOCK_HISTORY_V]);

            int32_t kvOffset = (mNetworkInput->tokenLength - 1) * mKVBytes;

            bm_device_mem_t kCacheMem = ::bm_mem_from_device(
                blockCache->stages[0].input_mems[IDX_BLOCK_HISTORY_K].u.device.device_addr + kvOffset, mKVBytes);
            bm_device_mem_t vCacheMem = ::bm_mem_from_device(
                blockCache->stages[0].input_mems[IDX_BLOCK_HISTORY_V].u.device.device_addr + kvOffset, mKVBytes);

            ::bmrt_tensor_with_device(&(outputTensors[IDX_BLOCK_OUTPUT_STATES]),
                                      blockCache->stages[0].output_mems[IDX_BLOCK_OUTPUT_STATES],
                                      blockCache->output_dtypes[IDX_BLOCK_OUTPUT_STATES],
                                      blockCache->stages[0].output_shapes[IDX_BLOCK_OUTPUT_STATES]);
            ::bmrt_tensor_with_device(&(outputTensors[IDX_BLOCK_K_CACHE]), kCacheMem,
                                      blockCache->output_dtypes[IDX_BLOCK_K_CACHE],
                                      blockCache->stages[0].output_shapes[IDX_BLOCK_K_CACHE]);
            ::bmrt_tensor_with_device(&(outputTensors[IDX_BLOCK_V_CACHE]), vCacheMem,
                                      blockCache->output_dtypes[IDX_BLOCK_V_CACHE],
                                      blockCache->stages[0].output_shapes[IDX_BLOCK_V_CACHE]);

            bool ok = ::bmrt_launch_tensor_ex(mpBmrt, blockCache->name, inputTensors.data(), inputTensors.size(),
                                              outputTensors.data(), outputTensors.size(), true, false);
            if (!ok) {
                throw std::runtime_error {"Error occurred when forward block cache network for decode."};
            }

            bm_status_t ret = ::bm_thread_sync(mHandle);
            if (ret != BM_SUCCESS) {
                throw std::runtime_error {"Error occurred when forward block cache network for decode."};
            }
        }

        int32_t BMRuntimeQwen3Model::Sample(std::optional<SampleParam> param) {
            bm_status_t ret {};
            int32_t token {0};

            if (mLmHead->stages[0].output_shapes[0].dims[1] == 1) {
                // 快速路径：如果lm_head的输出只有一个时，不需要再执行采样
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
                    sampleParam.topK =
                        mSampleHead->stages[0].output_shapes[0].dims[1];  // 不能大于sample_head的最大输出长度
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

        std::shared_ptr<Model> CreateQwen3BmruntimeModel(const std::string& tokenizerPath,
                                                         const std::string& configPath, const std::string& modelPath) {
            return std::make_shared<BMRuntimeQwen3Model>(tokenizerPath, configPath, modelPath);
        }
    }  // namespace lms
}  // namespace qifeng
