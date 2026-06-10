# 数据库操作脚本

- 每个目录都是对一个数据库的操作脚本
- 比如（mariadb举例）：
  - 安装脚本：install.sh --version=10.6.33 --deb_path=/path/to/mariadb-10.6.33-1.el8.noarch.rpm
    - 检查当前系统是否安装了mariadb、没有安装则查看系统源是否有该版本、无则使用指定路径的包安装
  - 初始化脚本:init.sh
    - init.sh --admin_password=123456 --data=/path/to/data --port=3306 --ip=127.0.0.1...
  - 启动脚本:start.sh
  - create_user_and_execute.sh:创建用户并执行脚本
    - create_user_and_execute.sh --admin_user=root --admin_password=123456 --user=test --password=123456 --sql_dir=/path/to/sql
  - 停止脚本:stop.sh
  - 重启脚本:restart.sh
  - 卸载脚本：uninstall.sh

