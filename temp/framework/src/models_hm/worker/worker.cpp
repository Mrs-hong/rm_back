/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "common/logger.h"
#include "models_hm/worker/worker.h"

#include <algorithm>
#include <filesystem>

namespace qifeng {

    namespace {
        // 从模型路径提取文件名（不含扩展名）作为图名
        // TCIM 的 .hmm/.hmms 单文件即单模型，对应 sophon bmodel 里的单个 network
        std::string ExtractGraphName(const std::string& path) {
            std::filesystem::path p(path);
            return p.stem().string();
        }

        // 计算形状的元素总数
        size_t ShapeElemCount(const std::vector<int64_t>& shape) {
            size_t n = 1;
            for (int64_t d : shape) {
                n *= static_cast<size_t>(d);
            }
            return n;
        }

        size_t ShapeElemCount(const std::vector<int>& shape) {
            size_t n = 1;
            for (int d : shape) {
                n *= static_cast<size_t>(d);
            }
            return n;
        }
    }  // namespace

    ModelWorker::ModelWorker(const std::string& modelPath, int tpuId, IOMode ioMode)
        : mModelPath(modelPath), mTpuId(tpuId), mIoMode(ioMode) {
        Init();
    }

    ModelWorker::~ModelWorker() {
        std::lock_guard<std::mutex> lock1(mInitLock);
        std::lock_guard<std::mutex> lock2(mProcessLock);

        // TCIM Module / Stream 由 shared_ptr / RAII 自动释放，此处显式 reset 保证顺序
        mModules.clear();
        mModule.reset();

        FLOG_DEBUG("ModelWorker (TCIM) destroyed");
    }

    bool ModelWorker::IsReady() const {
        std::lock_guard<std::mutex> lock(mInitLock);
        return mInitialized;
    }

    std::shared_ptr<tcim::Module> ModelWorker::LoadModuleFile(const std::string& filePath,
                                                              const std::string& graphName) {
        tcim::Module::Option option(mTpuId);
        auto module = std::make_shared<tcim::Module>(tcim::Module::LoadFromFile(filePath, option));

        tcim::Status initSt = module->GetInitStatus();
        THROW_IF(initSt != tcim::Status::OK, ERR_RUNTIME);

        tcim::Status streamSt = module->SetStream(mStream);
        THROW_IF(streamSt != tcim::Status::OK, ERR_RUNTIME);

        // 注册输入/输出节点名
        std::vector<std::string>& inputNames = mGraphInputNames[graphName];
        size_t inNum = module->GetInputNum();
        inputNames.reserve(inNum);
        for (size_t i = 0; i < inNum; ++i) {
            inputNames.emplace_back(module->GetInputName(static_cast<int>(i)));
        }

        std::vector<std::string>& outputNames = mGraphOutputNames[graphName];
        size_t outNum = module->GetOutputNum();
        outputNames.reserve(outNum);
        for (size_t i = 0; i < outNum; ++i) {
            outputNames.emplace_back(module->GetOutputName(static_cast<int>(i)));
        }

        SLOG_INFO << "ModelWorker(TCIM): Loaded graph: " << graphName << ", file: " << filePath << ", inputs: " << inNum
                  << ", outputs: " << outNum;

        return module;
    }

    bool ModelWorker::Init() {
        if (!mInitialized) {
            std::lock_guard<std::mutex> lock(mInitLock);
            if (!mInitialized) {
                // 若模型为 .onnx，则由子类（如 VADWorker）通过 onnxruntime CPU 后端管理，
                // 基类不加载 TCIM Module，仅标记初始化成功以避免子类后端被上层误判为失败。
                const bool isOnnx =
                    (mModelPath.size() >= 5) && (mModelPath.compare(mModelPath.size() - 5, 5, ".onnx") == 0);
                if (isOnnx) {
                    mDefaultGraphName = ExtractGraphName(mModelPath);
                    mGraphNames.clear();
                    mGraphNames.push_back(mDefaultGraphName);
                    mInitialized = true;
                    SLOG_INFO << "ModelWorker: ONNX 模型由子类 CPU 后端管理, file: " << mModelPath
                              << ", graph: " << mDefaultGraphName;
                    return mInitialized;
                }

                try {
                    mDefaultGraphName = ExtractGraphName(mModelPath);
                    mGraphNames.clear();
                    mGraphNames.push_back(mDefaultGraphName);

                    mModule = LoadModuleFile(mModelPath, mDefaultGraphName);
                    mModules[mDefaultGraphName] = mModule;

                    mInitialized = true;
                    SLOG_INFO << "ModelWorker(TCIM): Model initialized, file: " << mModelPath
                              << ", graph: " << mDefaultGraphName;
                } catch (const CommonException& e) {
                    SLOG_ERROR << "ModelWorker(TCIM): Initialization failed with CommonException: " << e.errorCode;
                    return false;
                } catch (const std::exception& e) {
                    SLOG_ERROR << "ModelWorker(TCIM): Initialization failed: " << e.what();
                    return false;
                } catch (...) {
                    SLOG_ERROR << "ModelWorker(TCIM): Unknown exception during init";
                    return false;
                }
            }
        }
        return mInitialized;
    }

    bool ModelWorker::LoadGraph(const std::string& graphName, const std::string& filePath) {
        std::lock_guard<std::mutex> lock(mInitLock);
        try {
            auto module = LoadModuleFile(filePath, graphName);
            mModules[graphName] = module;
            mGraphNames.push_back(graphName);
            SLOG_INFO << "ModelWorker(TCIM): Additional graph loaded: " << graphName << " from " << filePath;
            return true;
        } catch (const CommonException& e) {
            SLOG_ERROR << "ModelWorker(TCIM): LoadGraph failed for " << graphName << ": CommonException "
                       << e.errorCode;
            return false;
        } catch (const std::exception& e) {
            SLOG_ERROR << "ModelWorker(TCIM): LoadGraph failed for " << graphName << ": " << e.what();
            return false;
        } catch (...) {
            SLOG_ERROR << "ModelWorker(TCIM): LoadGraph failed for " << graphName << ": unknown exception";
            return false;
        }
    }

    int ModelWorker::Process(const ModelInput& input, ModelOutput& output) {
        return Process(mDefaultGraphName, input, output);
    }

    int ModelWorker::Process(const std::string& graphName, const ModelInput& input, ModelOutput& output) {
        std::lock_guard<std::mutex> lock(mProcessLock);

        // 选择对应图名的 Module，找不到则回退到默认 Module
        auto moduleIt = mModules.find(graphName);
        std::shared_ptr<tcim::Module> module = (moduleIt != mModules.end()) ? moduleIt->second : mModule;

        if (!module) {
            SLOG_ERROR << "ModelWorker(TCIM)::Process - module not loaded for graph: " << graphName;
            return ERR_RUNTIME;
        }

        // 使用对应图名的输入/输出节点名
        auto inIt = mGraphInputNames.find(graphName);
        auto outIt = mGraphOutputNames.find(graphName);
        if (inIt == mGraphInputNames.end() || outIt == mGraphOutputNames.end()) {
            // 回退到默认图名
            inIt = mGraphInputNames.find(mDefaultGraphName);
            outIt = mGraphOutputNames.find(mDefaultGraphName);
        }
        if (inIt == mGraphInputNames.end() || outIt == mGraphOutputNames.end()) {
            SLOG_ERROR << "ModelWorker(TCIM)::Process - no I/O names for graph: " << graphName;
            return ERR_RUNTIME;
        }

        const std::vector<std::string>& inputNames = inIt->second;
        const std::vector<std::string>& outputNames = outIt->second;

        int result = ERR_OK;

        try {
            // ── 设置输入：将用户 host 数据包装为 TCIM Tensor 并 SetInput ──
            for (const std::string& name : inputNames) {
                auto dataIt = input.data.find(name);
                if (dataIt == input.data.end() || !dataIt->second) {
                    SLOG_WARN << "ModelWorker(TCIM)::Process - No input data for: " << name;
                    continue;
                }
                const void* srcData = dataIt->second;

                // 查询模型对该输入的 TensorInfo（形状/类型由模型定义）
                tcim::TensorInfo info = module->GetInputInfo(name);

                // 模型 shape 的总元素数和内存大小
                size_t modelElems = ShapeElemCount(info.Shape());
                tcim::DataType modelDtype = info.DataType();

                // 用户可传 shapes 指定实际有效元素数
                size_t userElems = modelElems;
                auto shapeIt = input.shapes.find(name);
                if (shapeIt != input.shapes.end()) {
                    userElems = ShapeElemCount(shapeIt->second);
                }
                size_t copyElems = std::min(userElems, modelElems);

                if (modelDtype == tcim::DataType::FLOAT32 || modelDtype == tcim::DataType::FLOAT16) {
                    // 浮点输入（ASR/VAD/SV 的 fbank、state 等）：用户统一按 FLOAT32 提供数据。
                    // 创建 F32 host tensor 拷贝，再按需 AsType 转换到模型 dtype（F16），
                    // 绝不能把 float 字节直接当模型 dtype 字节拷贝。
                    size_t userFloatBytes = copyElems * sizeof(float);
                    tcim::TensorInfo f32Info = info.AsType(tcim::DataType::FLOAT32);
                    size_t f32MemSize = f32Info.MemSize();
                    tcim::Tensor hostTensor = tcim::Tensor::CreateHostTensor(f32Info, f32MemSize, nullptr);
                    if (hostTensor.GetInitStatus() != tcim::Status::OK) {
                        SLOG_ERROR << "ModelWorker(TCIM)::Process - CreateHostTensor failed for: " << name;
                        return ERR_RUNTIME;
                    }
                    void* dst = hostTensor.Data();
                    if (dst && srcData && userFloatBytes > 0) {
                        std::memset(dst, 0, f32MemSize);
                        std::memcpy(dst, srcData, userFloatBytes);
                    }

                    tcim::Tensor finalTensor = hostTensor;
                    if (modelDtype != tcim::DataType::FLOAT32) {
                        finalTensor = hostTensor.AsType(modelDtype, true);
                        if (finalTensor.GetInitStatus() != tcim::Status::OK) {
                            SLOG_ERROR << "ModelWorker(TCIM)::Process - Input AsType(" << static_cast<int>(modelDtype)
                                       << ") failed for: " << name;
                            return ERR_RUNTIME;
                        }
                    }
                    tcim::Status st = module->SetInput(name, finalTensor);
                    THROW_IF(st != tcim::Status::OK, ERR_RUNTIME);
                } else {
                    // 整型等输入（PUNC 的 text token id 等）：用户数据已按模型 dtype 提供，
                    // 直接按模型 dtype 的原始字节拷贝。
                    size_t modelMemSize = info.MemSize();
                    tcim::Tensor hostTensor = tcim::Tensor::CreateHostTensor(info, modelMemSize, nullptr);
                    if (hostTensor.GetInitStatus() != tcim::Status::OK) {
                        SLOG_ERROR << "ModelWorker(TCIM)::Process - CreateHostTensor failed for: " << name;
                        return ERR_RUNTIME;
                    }
                    void* dst = hostTensor.Data();
                    size_t userBytes = copyElems * info.DataTypeSize();
                    if (dst && srcData && userBytes > 0) {
                        std::memset(dst, 0, modelMemSize);
                        std::memcpy(dst, srcData, userBytes);
                    }
                    tcim::Status st = module->SetInput(name, hostTensor);
                    THROW_IF(st != tcim::Status::OK, ERR_RUNTIME);
                }
            }

            // ── 执行推理 ──
            tcim::Status runSt = module->Run(true);  // sync=true，同步执行
            THROW_IF(runSt != tcim::Status::OK, ERR_RUNTIME);

            // ── 获取输出：GetOutput -> ToHost -> 类型转换 -> 拷贝到用户 buffer ──
            for (const std::string& name : outputNames) {
                auto outDataIt = output.data.find(name);
                if (outDataIt == output.data.end() || !outDataIt->second) {
                    SLOG_WARN << "ModelWorker(TCIM)::Process - Output buffer not provided for: " << name;
                    continue;
                }

                tcim::Tensor devOut = module->GetOutput(name);
                tcim::Tensor hostOut = devOut.ToHost(true);  // contiguous=true，拷贝到 host 连续内存

                const tcim::TensorInfo& outInfo = hostOut.Info();
                size_t tensorElems = ShapeElemCount(outInfo.Shape());
                tcim::DataType dtype = outInfo.DataType();

                // 若模型输出非 FLOAT32，转换成 FLOAT32 再拷贝（用户 buffer 按 float32 解释）
                tcim::Tensor casted = hostOut;
                if (dtype != tcim::DataType::FLOAT32) {
                    casted = hostOut.AsType(tcim::DataType::FLOAT32, true);
                    if (casted.GetInitStatus() != tcim::Status::OK) {
                        SLOG_ERROR << "ModelWorker(TCIM)::Process - AsType(FLOAT32) failed for: " << name;
                        return ERR_RUNTIME;
                    }
                }
                const tcim::TensorInfo& finalInfo = casted.Info();
                size_t elemSize = finalInfo.DataTypeSize();  // FLOAT32 -> 4

                // 用户期望的元素数
                size_t expectedElems = tensorElems;
                auto shapeIt = output.shapes.find(name);
                if (shapeIt != output.shapes.end()) {
                    expectedElems = ShapeElemCount(shapeIt->second);
                }

                size_t actualElems = std::min(tensorElems, expectedElems);
                size_t copyBytes = actualElems * elemSize;

                void* dst = outDataIt->second;
                std::memcpy(dst, casted.Data(), copyBytes);

                // 回写实际输出形状
                std::vector<int> outShape;
                outShape.reserve(finalInfo.Shape().size());
                for (int64_t d : finalInfo.Shape()) {
                    outShape.push_back(static_cast<int>(d));
                }
                output.shapes[name] = outShape;
            }

            result = ERR_OK;
        } catch (const CommonException& e) {
            SLOG_ERROR << "ModelWorker(TCIM)::Process - Inference failed with CommonException: " << e.errorCode;
            result = e.errorCode;
        } catch (const std::exception& e) {
            SLOG_ERROR << "ModelWorker(TCIM)::Process - Inference failed: " << e.what();
            result = ERR_UNKNOWN;
        } catch (...) {
            SLOG_ERROR << "ModelWorker(TCIM)::Process - Inference failed with unknown exception";
            result = ERR_UNKNOWN;
        }

        return result;
    }

    std::vector<std::string> ModelWorker::GetGraphNames() const {
        std::lock_guard<std::mutex> lock(mInitLock);
        return mGraphNames;
    }

    std::vector<std::string> ModelWorker::GetInputNames(const std::string& graphName) const {
        std::lock_guard<std::mutex> lock(mInitLock);
        auto it = mGraphInputNames.find(graphName);
        if (it != mGraphInputNames.end()) {
            return it->second;
        }
        return {};
    }

    std::vector<std::string> ModelWorker::GetOutputNames(const std::string& graphName) const {
        std::lock_guard<std::mutex> lock(mInitLock);
        auto it = mGraphOutputNames.find(graphName);
        if (it != mGraphOutputNames.end()) {
            return it->second;
        }
        return {};
    }

    std::string ModelWorker::GetTensorPoolStats() const {
        // TCIM 版本无 TensorPool，Module 内部托管张量内存
        return "TCIM Module manages tensors internally (no TensorPool)";
    }

    tcim::TensorInfo ModelWorker::GetInputInfo(const std::string& name) const {
        if (mModule) {
            return mModule->GetInputInfo(name);
        }
        return tcim::TensorInfo {};
    }

    tcim::TensorInfo ModelWorker::GetOutputInfo(const std::string& name) const {
        if (mModule) {
            return mModule->GetOutputInfo(name);
        }
        return tcim::TensorInfo {};
    }

    tcim::TensorInfo ModelWorker::GetInputInfo(const std::string& graphName, const std::string& name) const {
        auto it = mModules.find(graphName);
        if (it != mModules.end() && it->second) {
            return it->second->GetInputInfo(name);
        }
        // 回退到默认 Module
        if (mModule) {
            return mModule->GetInputInfo(name);
        }
        return tcim::TensorInfo {};
    }

    tcim::TensorInfo ModelWorker::GetOutputInfo(const std::string& graphName, const std::string& name) const {
        auto it = mModules.find(graphName);
        if (it != mModules.end() && it->second) {
            return it->second->GetOutputInfo(name);
        }
        if (mModule) {
            return mModule->GetOutputInfo(name);
        }
        return tcim::TensorInfo {};
    }

}  // namespace qifeng
