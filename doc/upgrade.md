# 软件包
```text
qifeng_ca:
    server:
        - bin
        - lib
        - config
        - data
        - log
    model:
        - llm
        - aas
        - xxx
    frontend:
        - static
        - config.d/default.conf\ca_https.conf

```


## 安装服务结构:
```text
service:
    - qifeng_ca
        - server
        - model
    - nginx_frontend /frontend+conf.d
backend:
    - xx_service
        替换的目录
    - mysql(db) {存放数据库升级或者迁移的备份数据(sql)}
    - nginx_frontend 
```

## 升级流程:
```text
upgrade_tar:
    AA
    BB
    CC
    [up_detail.yaml] 升级详情
1、检查up_detail.yaml 不存在即全量升级
2、根据up_detail.yaml 替换对应目录
3、中间状态：旧文件都移动到backend的对应目录下
    3.1 升级成功、清除旧文件
    3.2 升级失败、回滚旧文件
4、前端和数据库如是
```

## 升级详情:
```text
up_detail.yaml:
    replace: 替换
        AA
        BB
        CC
    add: 添加(避免冲突)
        AA
        BB
        CC
    clear: 清除旧文件
        AA
        BB
        CC
```


