#include "lms/model.h"

namespace qifeng {

    namespace lms {

        ModelContext::CanceledException::CanceledException(const char* msg) : std::runtime_error {msg} {
        }

        ModelContext::CanceledException::CanceledException(const std::string& msg) : std::runtime_error {msg} {
        }

        ModelContext::ModelContext(std::shared_ptr<Model> model) : mModel {model} {
        }
    }  // namespace lms

}  // namespace qifeng