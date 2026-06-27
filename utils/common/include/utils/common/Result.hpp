// utils::common - 公共模块
// Result<T>:泛型返回类型,承载值 + 错误码 + 错误消息。
//
// 设计动机:让本工具集所有公开 API 都返回 Result<T>,而不是抛异常。
// 这样可以让本库适用于禁用异常的嵌入式 / 边缘运行时,
// 同时让错误传播在每个调用点都显式可见。
//
// 错误码以 int 存储,各子库可使用自己的 ErrorCode 枚举
// (只要能隐式转 int),从而保留领域语义不混淆。
#pragma once

#include <string>
#include <utility>

namespace utils
{

// 泛型 Result<T>:要么持有值(Ok),要么持有错误码 + 消息。
// 对小尺寸 T 拷贝代价很低;对大尺寸 T 建议用 move 构造。
template <typename T> class Result
{
  public:
	// 构造一个持有默认值(Ok)的 Result。
	Result() = default;

	bool IsOk() const { return mCode == 0; }
	explicit operator bool() const { return IsOk(); }

	// 访问存储的值。在 !IsOk() 时调用为未定义行为。
	const T& Value() const { return mValue; }
	T& Value() { return mValue; }

	int Code() const { return mCode; }
	const std::string& Message() const { return mMessage; }

	static Result Ok(T value)
	{
		Result r;
		r.mValue = std::move(value);
		r.mCode = 0;
		return r;
	}

	// 接受任意可转 int 的枚举作为错误码,便于各子库复用同一 Result 模板。
	template <typename ErrorCodeT>
	static Result Fail(ErrorCodeT code, std::string msg = "")
	{
		Result r;
		r.mCode = static_cast<int>(code);
		r.mMessage = std::move(msg);
		return r;
	}

  private:
	T mValue{};
	int mCode = 0;
	std::string mMessage;
};

// void 特化:只承载状态,不含值。
template <> class Result<void>
{
  public:
	Result() = default;

	bool IsOk() const { return mCode == 0; }
	explicit operator bool() const { return IsOk(); }

	int Code() const { return mCode; }
	const std::string& Message() const { return mMessage; }

	static Result Ok() { return Result{}; }

	template <typename ErrorCodeT>
	static Result Fail(ErrorCodeT code, std::string msg = "")
	{
		Result r;
		r.mCode = static_cast<int>(code);
		r.mMessage = std::move(msg);
		return r;
	}

  private:
	int mCode = 0;
	std::string mMessage;
};

} // namespace utils
