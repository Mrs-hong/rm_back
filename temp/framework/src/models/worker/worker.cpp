/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "common/logger.h"
#include "models/worker/worker.h"

namespace qifeng {

    namespace {
        bm_shape_t make_bm_shape(const std::vector<int>& dims) {
            bm_shape_t shape {};
            shape.num_dims = static_cast<int>(dims.size());
            for (size_t i = 0; i < dims.size() && i < BM_MAX_DIMS_NUM; ++i) {
                shape.dims[i] = dims[i];
            }
            return shape;
        }

        std::vector<int> bm_shape_to_vector(const bm_shape_t& shape) {
            std::vector<int> result;
            result.reserve(static_cast<size_t>(shape.num_dims));
            for (int i = 0; i < shape.num_dims; ++i) {
                result.push_back(shape.dims[i]);
            }
            return result;
        }
    }  // namespace

    ModelWorker::ModelWorker(const std::string& modelPath, int tpuId, IOMode ioMode)
        : mModelPath(modelPath), mTpuId(tpuId), mIoMode(ioMode) {
        mTensorPool = std::make_unique<TensorPool>();
        Init();
    }

    ModelWorker::~ModelWorker() {
        std::lock_guard<std::mutex> lock1(mInitLock);
        std::lock_guard<std::mutex> lock2(mProcessLock);

        mTensorPool.reset();

        if (mpBmrt) {
            bmrt_destroy(mpBmrt);
            mpBmrt = nullptr;
        }

        if (mHandle) {
            bm_dev_free(mHandle);
            mHandle = nullptr;
        }

        FLOG_DEBUG("ModelWorker destroyed");
    }

    bool ModelWorker::IsReady() const {
        std::lock_guard<std::mutex> lock(mInitLock);
        return mInitialized;
    }

    bool ModelWorker::Init() {
        if (!mInitialized) {
            std::lock_guard<std::mutex> lock(mInitLock);
            if (!mInitialized) {
                try {
                    bm_status_t ret = bm_dev_request(&mHandle, mTpuId);
                    THROW_IF(ret != BM_SUCCESS, ERR_RUNTIME);

                    mpBmrt = bmrt_create(mHandle);
                    THROW_IF(!mpBmrt, ERR_RUNTIME);

                    bool ok = bmrt_load_bmodel(mpBmrt, mModelPath.c_str());
                    THROW_IF(!ok, ERR_RUNTIME);

                    int netCount = bmrt_get_network_number(mpBmrt);
                    THROW_IF(netCount <= 0, ERR_RUNTIME);

                    for (int i = 0; i < netCount; ++i) {
                        const char* netName = bmrt_get_network_name(mpBmrt, i);
                        THROW_IF(!netName, ERR_RUNTIME);

                        std::string name(netName);
                        mGraphNames.push_back(name);

                        if (mDefaultGraphName.empty()) {
                            mDefaultGraphName = name;
                        }

                        const bm_net_info_t* netInfo = bmrt_get_network_info(mpBmrt, name.c_str());
                        THROW_IF(!netInfo, ERR_RUNTIME);
                        mNetInfos[name] = netInfo;

                        std::vector<std::string>& inputNames = mGraphInputNames[name];
                        for (int j = 0; j < netInfo->input_num; ++j) {
                            if (netInfo->input_names[j]) {
                                inputNames.emplace_back(netInfo->input_names[j]);
                            }
                        }

                        std::vector<std::string>& outputNames = mGraphOutputNames[name];
                        for (int j = 0; j < netInfo->output_num; ++j) {
                            if (netInfo->output_names[j]) {
                                outputNames.emplace_back(netInfo->output_names[j]);
                            }
                        }
                    }

                    mInitialized = true;
                    SLOG_INFO << "ModelWorker: Model initialized, bmodel: " << mModelPath
                              << ", graph count: " << mGraphNames.size();
                } catch (const CommonException& e) {
                    SLOG_ERROR << "ModelWorker: Initialization failed with CommonException: " << e.errorCode;
                    return false;
                } catch (const std::exception& e) {
                    SLOG_ERROR << "ModelWorker: Initialization failed: " << e.what();
                    return false;
                } catch (...) {
                    SLOG_ERROR << "ModelWorker: Unknown exception during init";
                    return false;
                }
            }
        }
        return mInitialized;
    }

    int ModelWorker::Process(const ModelInput& input, ModelOutput& output) {
        return Process(mDefaultGraphName, input, output);
    }

    int ModelWorker::Process(const std::string& graphName, const ModelInput& input, ModelOutput& output) {
        SLOG_DEBUG << "ModelWorker::Process started with graph: " << graphName;

        std::lock_guard<std::mutex> lock(mProcessLock);

        const bm_net_info_t* netInfo = GetNetInfo(graphName);
        if (!netInfo) {
            SLOG_ERROR << "ModelWorker::Process - graph not found: " << graphName;
            return ERR_NOT_FOUND;
        }

        std::unordered_map<std::string, BmTensor> inputTensors;
        std::unordered_map<std::string, BmTensor> outputTensors;
        int result = ERR_OK;

        try {
            inputTensors = mTensorPool->AcquireInputTensors(mHandle, mpBmrt, graphName, netInfo);
            outputTensors = mTensorPool->AcquireOutputTensors(mHandle, mpBmrt, graphName, netInfo);

            THROW_IF(inputTensors.size() != static_cast<size_t>(netInfo->input_num), ERR_RUNTIME);
            THROW_IF(outputTensors.size() != static_cast<size_t>(netInfo->output_num), ERR_RUNTIME);

            for (int i = 0; i < netInfo->input_num; ++i) {
                const char* name = netInfo->input_names[i];
                THROW_IF(!name, ERR_RUNTIME);
                std::string nameStr(name);

                auto it = inputTensors.find(nameStr);
                THROW_IF(it == inputTensors.end(), ERR_NOT_FOUND);

                BmTensor& bmTensor = it->second;
                bm_shape_t curShape = bmTensor.shape();

                std::string shapeStr;
                for (int d = 0; d < curShape.num_dims; ++d) {
                    shapeStr += std::to_string(curShape.dims[d]);
                    if (d < curShape.num_dims - 1)
                        shapeStr += "x";
                }
                SLOG_DEBUG << "ModelWorker::Process - input[" << i << "] " << nameStr << " shape: [" << shapeStr << "]";

                auto dataIt = input.data.find(nameStr);
                THROW_IF(dataIt == input.data.end(), ERR_NOT_FOUND);
                const void* srcData = dataIt->second;
                THROW_IF(!srcData, ERR_INVALID_PARAM);

                size_t copyBytes = 0;
                auto shapeIt = input.shapes.find(nameStr);
                if (shapeIt != input.shapes.end()) {
                    THROW_IF(curShape.num_dims == 0, ERR_INVALID_PARAM);
                    THROW_IF(static_cast<int>(shapeIt->second.size()) != curShape.num_dims, ERR_INVALID_PARAM);

                    for (int d = 0; d < curShape.num_dims; ++d) {
                        THROW_IF(shapeIt->second[static_cast<size_t>(d)] > curShape.dims[d], ERR_INVALID_PARAM);
                    }

                    bm_shape_t newShape = make_bm_shape(shapeIt->second);
                    bmTensor.reshape(mHandle, mpBmrt, newShape);

                    size_t elemCount = 1;
                    for (int dim : shapeIt->second) {
                        elemCount *= static_cast<size_t>(dim);
                    }
                    copyBytes = elemCount * sizeof(float);
                } else {
                    size_t elemCount = bmTensor.size();
                    copyBytes = elemCount * sizeof(float);
                    SLOG_WARN << "ModelWorker::Process - No shape info for input: " << nameStr
                              << ", using tensor size: " << elemCount;
                }

                size_t maxBytes = bmTensor.byteSize();
                if (copyBytes > maxBytes) {
                    copyBytes = maxBytes;
                    SLOG_WARN << "ModelWorker::Process - copyBytes clamped to tensor byteSize: " << maxBytes;
                }

                bm_status_t st = bm_memcpy_s2d_partial(mHandle, bmTensor.tensor.device_mem, const_cast<void*>(srcData),
                                                       static_cast<unsigned int>(copyBytes));
                THROW_IF(st != BM_SUCCESS, ERR_RUNTIME);
            }

            std::vector<bm_tensor_t> inputArr(static_cast<size_t>(netInfo->input_num));
            for (int i = 0; i < netInfo->input_num; ++i) {
                auto it = inputTensors.find(netInfo->input_names[i]);
                THROW_IF(it == inputTensors.end(), ERR_NOT_FOUND);
                inputArr[static_cast<size_t>(i)] = it->second.tensor;
            }

            std::vector<bm_tensor_t> outputArr(static_cast<size_t>(netInfo->output_num));
            for (int i = 0; i < netInfo->output_num; ++i) {
                auto it = outputTensors.find(netInfo->output_names[i]);
                THROW_IF(it == outputTensors.end(), ERR_NOT_FOUND);
                outputArr[static_cast<size_t>(i)] = it->second.tensor;
            }

            bool launchOk = bmrt_launch_tensor_ex(mpBmrt, graphName.c_str(), inputArr.data(), netInfo->input_num,
                                                  outputArr.data(), netInfo->output_num, true, false);
            THROW_IF(!launchOk, ERR_RUNTIME);

            bm_status_t syncSt = bm_thread_sync(mHandle);
            THROW_IF(syncSt != BM_SUCCESS, ERR_RUNTIME);

            for (int i = 0; i < netInfo->output_num; ++i) {
                const char* name = netInfo->output_names[i];
                THROW_IF(!name, ERR_RUNTIME);
                std::string nameStr(name);

                auto outIt = output.data.find(nameStr);
                if (outIt == output.data.end() || !outIt->second) {
                    SLOG_WARN << "ModelWorker::Process - Output buffer not provided for: " << nameStr;
                    continue;
                }

                auto it = outputTensors.find(nameStr);
                THROW_IF(it == outputTensors.end(), ERR_NOT_FOUND);
                BmTensor& bmTensor = it->second;
                bm_shape_t outShape = bmTensor.shape();

                size_t expectedElements = 0;
                auto shapeIt = output.shapes.find(nameStr);
                if (shapeIt != output.shapes.end()) {
                    expectedElements = 1;
                    for (int dim : shapeIt->second) {
                        expectedElements *= static_cast<size_t>(dim);
                    }
                } else {
                    expectedElements = bmTensor.size();
                    SLOG_WARN << "ModelWorker::Process - No shape info for output: " << nameStr
                              << ", using tensor size: " << expectedElements;
                }

                size_t tensorElements = bmTensor.size();
                size_t actualElements = std::min(tensorElements, expectedElements);
                size_t copyBytes = actualElements * sizeof(float);
                size_t maxBytes = bmTensor.byteSize();
                if (copyBytes > maxBytes) {
                    copyBytes = maxBytes;
                }

                bm_status_t st = bm_memcpy_d2s_partial(mHandle, outIt->second, bmTensor.tensor.device_mem,
                                                       static_cast<unsigned int>(copyBytes));
                THROW_IF(st != BM_SUCCESS, ERR_RUNTIME);

                output.shapes[nameStr] = bm_shape_to_vector(outShape);
            }

            result = ERR_OK;
        } catch (const CommonException& e) {
            SLOG_ERROR << "ModelWorker::Process - Inference failed with CommonException: " << e.errorCode;
            result = e.errorCode;
        } catch (const std::exception& e) {
            SLOG_ERROR << "ModelWorker::Process - Inference failed: " << e.what();
            result = ERR_UNKNOWN;
        } catch (...) {
            SLOG_ERROR << "ModelWorker::Process - Inference failed with unknown exception";
            result = ERR_UNKNOWN;
        }

        if (mTensorPool) {
            mTensorPool->ReleaseTensors(graphName, inputTensors, true);
            mTensorPool->ReleaseTensors(graphName, outputTensors, false);
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
        std::lock_guard<std::mutex> lock(mInitLock);
        if (mTensorPool) {
            return mTensorPool->GetStats();
        }
        return "TensorPool is not initialized";
    }

    const bm_net_info_t* ModelWorker::GetNetInfo(const std::string& graphName) const {
        auto it = mNetInfos.find(graphName);
        if (it != mNetInfos.end()) {
            return it->second;
        }
        return nullptr;
    }

}  // namespace qifeng
