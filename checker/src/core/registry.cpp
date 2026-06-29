// =============================================================================
// registry.cpp — 注册表实现
// =============================================================================
#include "checker/core/registry.h"

#include <string>

namespace checker {

std::vector<CheckerPtr> CheckerRegistry::build_all() const {
  std::vector<CheckerPtr> out;
  out.reserve(factories_.size());
  for (const auto& kv : factories_) {
    out.push_back(kv.second());
  }
  return out;
}

std::vector<std::string> CheckerRegistry::registered_names() const {
  std::vector<std::string> names;
  names.reserve(factories_.size());
  for (const auto& kv : factories_) names.push_back(kv.first);
  return names;
}

}  // namespace checker
