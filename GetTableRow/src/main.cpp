/**
 * @file main.cpp
 * @brief 定时查询 MySQL 数据库表行数的测试服务程序
 *
 * 功能：每 5 秒连接一次本地 MySQL 数据库，查询 test_db.test_table
 * 的行数并打印。 信号处理：支持 Ctrl+C (SIGINT) 和 kill (SIGTERM) 优雅退出。
 */

#include <csignal>
#include <ctime>
#include <iostream>
#include <mysql/mysql.h>
#include <unistd.h>

// 数据库连接配置
static const char *DB_HOST = "127.0.0.1";   // 数据库主机地址
static const unsigned int DB_PORT = 3306;   // MySQL 端口号
static const char *DB_USER = "user1";       // 数据库用户名
static const char *DB_PASS = "123";         // 数据库密码
static const char *DB_NAME = "test_db";     // 目标数据库名
static const char *DB_TABLE = "test_table"; // 目标表名

// 定时间隔（秒）
static const unsigned int INTERVAL_SECONDS = 5;

// 全局运行标志，用于信号处理后优雅退出
static volatile sig_atomic_t g_running = 1;

/**
 * @brief 信号处理函数
 * @param signo 捕获到的信号编号
 */
void signal_handler(int signo) {
  // 仅处理中断和终止信号
  if (signo == SIGINT || signo == SIGTERM) {
    std::cout << "\n[信息] 收到信号 " << signo << "，服务即将停止..."
              << std::endl;
    g_running = 0;
  }
}

/**
 * @brief 获取当前格式化时间字符串
 * @return 形如 "2024-01-01 12:00:00" 的字符串
 */
std::string get_current_time_str() {
  time_t now = time(nullptr);
  struct tm *local_time = localtime(&now);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", local_time);
  return std::string(buf);
}

/**
 * @brief 连接 MySQL 数据库
 * @return 成功返回 MYSQL 指针，失败返回 nullptr
 */
MYSQL *connect_mysql() {
  MYSQL *conn = mysql_init(nullptr);
  if (conn == nullptr) {
    std::cerr << "[错误] mysql_init 失败: 内存不足" << std::endl;
    return nullptr;
  }

  // 建立连接（使用 CLIENT_MULTI_STATEMENTS 支持多语句）
  if (mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT,
                         nullptr, 0) == nullptr) {
    std::cerr << "[错误] 连接数据库失败: " << mysql_error(conn) << std::endl;
    mysql_close(conn);
    return nullptr;
  }

  // 设置字符集为 utf8mb4，避免中文乱码
  if (mysql_set_character_set(conn, "utf8mb4") != 0) {
    std::cerr << "[警告] 设置字符集失败: " << mysql_error(conn) << std::endl;
  }

  return conn;
}

/**
 * @brief 查询指定表的行数
 * @param conn 已连接的 MYSQL 对象
 * @return 表行数，查询失败返回 -1
 */
long query_row_count(MYSQL *conn) {
  // 构造查询 SQL
  char sql[256];
  snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s", DB_TABLE);

  // 执行查询
  if (mysql_query(conn, sql) != 0) {
    std::cerr << "[错误] 查询失败: " << mysql_error(conn) << std::endl;
    return -1;
  }

  // 获取结果集
  MYSQL_RES *result = mysql_store_result(conn);
  if (result == nullptr) {
    std::cerr << "[错误] 获取结果集失败: " << mysql_error(conn) << std::endl;
    return -1;
  }

  // 读取第一行第一列（COUNT(*) 结果）
  MYSQL_ROW row = mysql_fetch_row(result);
  long count = 0;
  if (row != nullptr && row[0] != nullptr) {
    count = strtol(row[0], nullptr, 10);
  }

  // 释放结果集
  mysql_free_result(result);

  return count;
}

/**
 * @brief 主函数：注册信号、循环定时查询
 */
int main(int argc, char *argv[]) {
  // 注册信号处理器（优雅退出）
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  std::cout << "========================================" << std::endl;
  std::cout << "  GetTableRow 定时查询服务启动" << std::endl;
  std::cout << "  目标: " << DB_HOST << ":" << DB_PORT << "/" << DB_NAME << "."
            << DB_TABLE << std::endl;
  std::cout << "  间隔: " << INTERVAL_SECONDS << " 秒" << std::endl;
  std::cout << "  按 Ctrl+C 停止服务" << std::endl;
  std::cout << "========================================" << std::endl;

  while (g_running) {
    // 每次循环新建连接（简单可靠，适合测试场景）
    MYSQL *conn = connect_mysql();
    if (conn != nullptr) {
      long row_count = query_row_count(conn);
      if (row_count >= 0) {
        std::cout << "[" << get_current_time_str()
                  << "] 当前表行数: " << row_count << std::endl;
      }
      mysql_close(conn);
    }

    // 按设定间隔休眠，支持秒级中断退出
    for (unsigned int i = 0; i < INTERVAL_SECONDS && g_running; ++i) {
      sleep(1);
    }
  }

  std::cout << "[信息] 服务已安全退出。" << std::endl;
  return 0;
}
