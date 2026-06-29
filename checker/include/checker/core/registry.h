// =============================================================================
// registry.h — 检查器注册表
//
// 持有工厂列表，统一构建所有 checker。扩展方式：
//   实现 IChecker 子类 T（需默认可构造、提供 static class_name()），
//   在 register_all.cpp 增加 `reg.add<T>();`，无需改动 core。
//
// 设计：checker 默认构造，运行期 Context 经 Runner -> run(ctx) 注入，
// 避免构造期依赖，便于单元测试与并发。
// =============================================================================
#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "checker/core/checker.h"

namespace checker {

class CheckerRegistry {
 public:
  using Factory = std::function<CheckerPtr()>;

  // 注册一个检查器类型。T 需默认可构造且提供 static std::string class_name()。
  template <class T>
  void add() {
    factories_.emplace_back(T::class_name(), []() -> CheckerPtr { return std::make_unique<T>(); });
  }

  // 构建所有 checker 实例（保持注册顺序）
  std::vector<CheckerPtr> build_all() const;

  // 调试用：已注册的检查项名
  std::vector<std::string> registered_names() const;

 private:
  std::vector<std::pair<std::string, Factory>> factories_;
};

// 在 register_all.cpp 中实现：集中注册全部内置检查器
void register_all(CheckerRegistry& reg);

}  // namespace checker
