#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_ERROR_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_ERROR_H

#define ERR_MSG_LEN 256

// clang-format off
#define MAKE_ERROR_CODE(modelID, errorCode) (((uint16_t)(modelID) << 16) | ((uint16_t)(errorCode) & 0xFFFF))
// clang-format on
// 条件不成立时抛出指定错误码

struct CommonException {
    int errorCode;
    int line;
    const char* func;
    // 新增参数
    int prr[5];
    int prrCount;
    CommonException(int err, int l, const char* fn) : errorCode(err), line(l), func(fn), prr {0}, prrCount(0) {
    }
    // 新增可变参数构造函数
    CommonException(int err, int l, const char* fn, int p1, int p2, int p3, int p4, int p5, int cnt)
        : errorCode(err), line(l), func(fn), prr {p1, p2, p3, p4, p5}, prrCount(cnt) {
    }
};

#define THROW(errorCode) throw CommonException((errorCode), __LINE__, __func__)
#define THROW_IF(case_expr, errorCode)                              \
    do {                                                            \
        if ((case_expr))                                            \
            throw CommonException((errorCode), __LINE__, __func__); \
    } while (0)
#define THROW_IF_FAIL(expr)                                  \
    do {                                                     \
        int _ret = (expr);                                   \
        if (_ret != ERR_OK)                                  \
            throw CommonException(_ret, __LINE__, __func__); \
    } while (0)
// 支持最多5个参数
#define THROW_IF_PRR(case_expr, ...)                                            \
    do {                                                                        \
        if ((case_expr)) {                                                      \
            throw CommonException(ERR_RUNTIME, __LINE__, __func__, __VA_ARGS__, \
                                  sizeof((int[]) {__VA_ARGS__}) / sizeof(int)); \
        }                                                                       \
    } while (0)

#define ERR_OK 0x0             // 无错误
#define ERR_UNKNOWN 0x1        // 未知错误
#define ERR_INVALID_PARAM 0x2  // 参数无效
#define ERR_TIMEOUT 0x3        // 超时
#define ERR_NOT_FOUND 0x4      // 未找到
#define ERR_NO_MEMORY 0x5      // 内存不足
#define ERR_BUSY 0x6           // 资源忙
#define ERR_PERMISSION 0x7     // 权限不足
#define ERR_UNSUPPORTED 0x8    // 不支持的操作
#define ERR_IO 0x9             // IO错误
#define ERR_NETWORK 0xa        // 网络错误
#define ERR_OVERFLOW 0xb       // 溢出
#define ERR_UNDERFLOW 0xc      // 下溢
#define ERR_OVERFSIZE 0xd      // 大小超限
#define ERR_RUNTIME 0xe        // 程序运行异常
#define ERR_TIMER_FAILD 0xf    // 定时器申请失败
#define ERR_FAILED 0x10        // 操作失败
#define ERR_STOPPED_TASK 0x11  // 任务已停止
#endif                         // QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_ERROR_H
