- scmctl命令需求：

    - scmd、scmctl信息

        - \-\-version \-v  查看scm/scmctl版本

    - 服务控制

        - install \-\-name xxx 根据默认xxx\.yaml或数据库的最新版本安装xxx服务

        - install \-\-name xxx \-\-tar\_dir xxx/xx/ xx\.tar  将指定目录下的服务包安装到项目指定的目录下

        - start \-\-name xxx \-\-version xxx 启动指定版本服务或数据库记录的最新版本

        - stop \-\-name xxx 停止xxx服务

        - restart \-\-name  xx 重启xx

        - restart \-a 重启全部

        - upgrade \-\-name xx \-\-tar\_dir xx/xxxx 使用指定的目录软件包升级该服务 

    - 服务状态查看

        - list 查看所有服务基础信息：服务名、版本号、运行状态

        - info \-\-name xxx 查看某个服务的运行状态\+数据文件\+资源占用\+服务依赖\+日志文件路径、以及存在的 历史版本

    - 操做查看

        - log  \-type warn \-n 100查看最近100的操做警告

        - log \-\-type error \-n xx 查看失败