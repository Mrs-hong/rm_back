#include "internal/lms/lifecycle.h"
#include "internal/lms/lms_hm_model_manager.h"
// #include "internal/lms/lms_model_manager.h"

namespace qifeng_ca {
    namespace lms {
        // 全局LMS初始化状态(atomic, 跨线程读取)
        // false: 未就绪(尚未初始化或初始化失败); true: 已就绪, 可创建Summarizer
        static std::atomic<bool> gLmsReady {false};

        bool InitLms() {
            // 初始化LMS模型(纪要总结所需)
            bool ok = qifeng_ca::LmsHmModelManager::GetInstance().Initialize();
            SetLmsReady(ok);
            return ok;
        }

        bool ShutdownLms() {
            // 关闭LMS模型(纪要总结所需)
            SetLmsReady(false);
            qifeng_ca::LmsHmModelManager::GetInstance().Shutdown();
            return true;
        }

        bool IsLmsReady() {
            return gLmsReady.load(std::memory_order_acquire);
        }

        void SetLmsReady(bool ready) {
            gLmsReady.store(ready, std::memory_order_release);
        }
    }  // namespace lms

}  // namespace qifeng_ca