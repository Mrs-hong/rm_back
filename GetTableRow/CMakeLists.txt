# ============================================
# CMake 构建配置文件
# 项目：GetTableRow 定时数据库查询服务
# ============================================

# 要求最低 CMake 版本
cmake_minimum_required(VERSION 3.10)

# 项目名称及版本
project(GetTableRow VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 MySQL 客户端库和头文件路径
# 优先使用 mysql_config 获取配置信息
find_program(MYSQL_CONFIG_EXECUTABLE mysql_config)
if(MYSQL_CONFIG_EXECUTABLE)
    # 通过 mysql_config 获取编译参数和链接参数
    execute_process(COMMAND ${MYSQL_CONFIG_EXECUTABLE} --cflags
                    OUTPUT_VARIABLE MYSQL_CFLAGS
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${MYSQL_CONFIG_EXECUTABLE} --libs
                    OUTPUT_VARIABLE MYSQL_LIBS
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
    message(FATAL_ERROR "未找到 mysql_config，请确认已安装 libmysqlclient-dev")
endif()

# 查找头文件目录（备用方式）
find_path(MYSQL_INCLUDE_DIR mysql.h
    PATHS /usr/include/mysql /usr/local/include/mysql
    DOC "MySQL 头文件目录"
)

# 查找库文件（备用方式）
find_library(MYSQL_LIBRARY mysqlclient
    PATHS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/local/lib
    DOC "MySQL 客户端库"
)

if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR "未找到 MySQL 开发库，请安装 libmysqlclient-dev")
endif()

# 输出找到的 MySQL 配置（便于调试）
message(STATUS "MySQL include dir: ${MYSQL_INCLUDE_DIR}")
message(STATUS "MySQL library: ${MYSQL_LIBRARY}")
message(STATUS "MySQL libs: ${MYSQL_LIBS}")

# 创建可执行目标
add_executable(get_table_row src/main.cpp)

# 添加头文件搜索路径
target_include_directories(get_table_row PRIVATE ${MYSQL_INCLUDE_DIR})

# 添加编译选项（来自 mysql_config）
separate_arguments(MYSQL_CFLAGS_LIST UNIX_COMMAND "${MYSQL_CFLAGS}")
target_compile_options(get_table_row PRIVATE ${MYSQL_CFLAGS_LIST})

# 链接 MySQL 库
separate_arguments(MYSQL_LIBS_LIST UNIX_COMMAND "${MYSQL_LIBS}")
target_link_libraries(get_table_row PRIVATE ${MYSQL_LIBS_LIST})

# 如果上述链接方式有问题，降级为直接链接 mysqlclient
if(NOT MYSQL_LIBS)
    target_link_libraries(get_table_row PRIVATE ${MYSQL_LIBRARY})
endif()

# 安装规则：将可执行文件安装到 bin 目录
install(TARGETS get_table_row
    RUNTIME DESTINATION bin
)
