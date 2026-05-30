.config 目录
    是固定目录与.exe启动时路径下的.config文件
    包含scmd自身的配置文件scmd.yaml
.logs 存储scmd运行日志的目录、可以通过scmd.yaml配置日志级别、日志文件路径等

.service 存储各个服务的配置文件和exe文件（只读）、服务运行环境就在这里、结构如下
    .init 存放所有正确安装、并且正确解析的服务的systemd的INI scmd_xxxx.service、其中xxxx为传入的服务名
    xxxx 为每一个已经安装的xxxx服务的目录、结构如下
        service.yaml 服务的配置文件
        .exe 服务的可执行文件
        ... 其它服务自身运行时需要的文件
        ./xx/xx/数据目录

.backup 存储服务升级过程中保留完成执行程序副本
    服务名/{.exe ...} 是.service下去除数据目录的其余文件

.data 存储scmd运行时必要操做持久化yaml文件（用于操做恢复）以及各个服务的数据目录（.service/xxxx/xx/xx/数据目录）用于迁移）

 