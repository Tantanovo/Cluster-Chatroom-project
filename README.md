# Cluster-ChatServer

基于 muduo 网络库的高并发集群聊天服务器。Reactor 模式多线程 TCP 服务，支持单聊、群聊、离线消息，通过 Nginx TCP 负载均衡与 Redis 发布订阅实现多节点横向扩展。

## 功能

- 用户注册、登录、注销，重复登录拦截
- 好友添加、好友列表拉取
- 单聊：在线实时推送 / 离线落库、登录时补投
- 群组创建、加入、群聊广播
- 跨服务器通信：用户分布在不同节点时仍可互相收发
- 密码加盐哈希存储（SHA-256），杜绝明文入库与 SQL 注入

## 架构

```
                        ┌──────────────┐
   client ──┐           │    Nginx     │  stream 模块 · 四层负载均衡
   client ──┼── :8000 ─▶│  least_conn  │
   client ──┘           └──────┬───────┘
                               │
                ┌──────────────┴──────────────┐
                ▼                             ▼
        ┌───────────────┐             ┌───────────────┐
        │ ChatServer #1 │             │ ChatServer #2 │
        │   :6000       │             │   :6002       │
        │ 4 IO threads  │             │ 4 IO threads  │
        └───┬───────┬───┘             └───┬───────┬───┘
            │       │                     │       │
            │       └────────┬────────────┘       │
            │                ▼                    │
            │        ┌───────────────┐            │
            │        │     Redis     │            │
            │        │  Pub/Sub      │  跨节点消息路由
            │        │  channel=uid  │            │
            │        └───────────────┘            │
            │                                     │
            └──────────────┬──────────────────────┘
                           ▼
                   ┌───────────────┐
                   │    MySQL      │  连接池 · 预编译语句 · 事务
                   └───────────────┘
```

### 分层设计

| 层 | 职责 | 关键实现 |
|----|------|---------|
| 网络层 | 连接管理、收发、协议编解码 | muduo `TcpServer`，one loop per thread，4 个 IO 线程；`[4字节长度头][JSON body]` 分帧，异常隔离 |
| 业务层 | 消息分发与业务处理 | `unordered_map<msgid, handler>` 回调表，网络与业务零耦合 |
| 数据层 | 持久化 | MySQL 连接池（生产者消费者模型），预编译语句，多表写入走事务 |
| 集群层 | 跨节点通信 | Redis Pub/Sub，userid 作为 channel |

## 通信协议

```
 0        4                    4+len
 ┌────────┬───────────────────────┐
 │ length │      JSON body        │
 │ 4字节   │                       │
 │ 网络序  │                       │
 └────────┴───────────────────────┘
```

早期版本直接收发裸 JSON，压测中发现客户端连续发送时服务端解析失败并导致进程退出（见「已解决的问题」第 1 条），因此引入长度头分帧。

消息类型（`include/public.hpp`）：

| msgid | 含义 |
|-------|------|
| 1 / 2 | 登录 / 登录应答 |
| 3 | 注销 |
| 4 / 5 | 注册 / 注册应答 |
| 6 | 单聊 |
| 7 | 添加好友 |
| 8 | 创建群组 |
| 9 | 加入群组 |
| 10 | 群聊 |

## 环境依赖

- Ubuntu 20.04+ / g++ 9.3+ (C++11)
- muduo
- MySQL 8.0 + libmysqlclient
- Redis + hiredis
- Nginx（需带 `--with-stream`）
- CMake 3.16+
- OpenSSL（密码哈希，libssl-dev）

## 构建与运行

### 1. 初始化数据库

```bash
mysql -u root -p < sql/chat.sql
```

### 2. 配置数据库凭据

凭据不写进源码。任选一种：

```bash
# 方式一：环境变量
export CHAT_DB_USER=your_user
export CHAT_DB_PASSWD=your_password
export CHAT_DB_NAME=chat

# 方式二：配置文件（已在 .gitignore 中）
cp conf/mysql.cnf.example conf/mysql.cnf && vim conf/mysql.cnf
```

### 3. 编译

```bash
mkdir -p build && cd build && cmake .. && make -j4
```

### 4. 启动（单机单节点）

```bash
redis-server &

./bin/chatserver 127.0.0.1 6000 &
./bin/chatclient 127.0.0.1 6000
```

### 5. 启动（集群多节点 + Nginx 负载均衡）

```bash
redis-server &

# 起两个业务节点（端口由命令行参数指定）
./bin/chatserver 127.0.0.1 6000 &
./bin/chatserver 127.0.0.1 6002 &

# Nginx 四层负载均衡
sudo nginx -c $(pwd)/conf/nginx.conf

# 客户端统一连负载均衡端口
./bin/chatclient 127.0.0.1 8000
```

用四层 `stream` 而非七层 `http`：本项目是自定义 TCP 协议而非 HTTP，且服务端需主动向客户端推送消息，必须保持长连接。

## 测试

```bash
# 功能测试：注册/登录/单聊/离线消息/群聊逐项校验
python3 test/feature_test.py

# 压力测试
python3 test/load_test.py --host 127.0.0.1 --port 8000 --conns 2000 --msgs 20
```

`load_test.py` 覆盖三个维度：并发注册/登录吞吐与延迟（p50/p90/p99）、单聊端到端 RTT、长连接并发保持能力。改造前后各跑一次，把数字填进简历，比任何形容词都有说服力。

## 已解决的问题

### 1. 粘包导致进程整体退出

**现象**：压测到几百并发时服务端稳定在几十秒内退出，无 core 提示。

**定位**：gdb attach 后 `bt` 看到调用栈终止在 `nlohmann::json::parse` 抛出的 `parse_error`，异常从 muduo 的 `MessageCallback` 逃出，无人捕获，`std::terminate` 终止进程。

**根因**：`onMessage` 用 `retrieveAllAsString()` 一次取走全部可读字节。TCP 是字节流，客户端连续发送时两条 JSON 会粘在同一次读事件里，形成 `{...}{...}`，不是合法 JSON。

**修复**：协议加 4 字节网络序长度头；`onMessage` 改为 `while` 循环，先 `peek` 长度头，缓冲区不足一帧则保留数据等待下次可读（半包处理）；解析包 try-catch，畸形帧只关闭该连接。

### 2. SIGPIPE 处理函数误杀整个服务器

**现象**：任意客户端异常断开后，整个服务器进程退出。

**根因**：`main` 里写了 `signal(SIGPIPE, resetHandler)`，而 `resetHandler` 内部调用 `exit(0)`。muduo 在 `EventLoop` 构造时已将 SIGPIPE 设为 `SIG_IGN`，这行代码把它覆盖成了「收到即退出」。

**修复**：`SIGPIPE` 恢复 `SIG_IGN`，优雅退出逻辑改挂 `SIGINT` / `SIGTERM`，且用 `loop.quit()` 而非 `exit()`。

### 3. 客户端强杀导致消息黑洞

**现象**：`kill -9` 客户端后，该用户在 `User` 表中的 `state` 永久停留 `online`。此后发给他的消息走「在线分支」尝试推送或 Redis 转发，但目标连接已不存在，消息既不推送也不落离线库，**永久丢失**。

**修复**：客户端异常断开时（连接回调），把对应用户重置为 `offline` 并取消 Redis 订阅，后续消息走离线库兜底。

### 4. 每次查询新建数据库连接

**现象**：登录路径上 `queryByName` / `updatestate` / 离线消息查删 / 好友查询 / 群组查询各自 `MySQL mysql; mysql.connect();`，一次登录建立 4~6 条 MySQL 连接并立即销毁。

**修复**：实现连接池（`mutex` + `condition_variable` 生产者消费者模型），`getConnection` 返回带自定义删除器的 `shared_ptr`，析构时归还而非关闭；独立线程回收空闲超时连接；取用前 `mysql_ping` 探活。

### 5. SQL 注入与密码明文存储

**现象**：所有 SQL 由 `sprintf` 拼接，登录查询 `where name='%s'` 直接拼用户输入，传入 `' or '1'='1` 即可绕过登录。密码明文入库。

**修复**：全部改用 `mysql_stmt_prepare` 预编译语句 + 参数绑定；密码改存 `SHA-256(salt + password)`，salt 每用户随机生成；数据库凭据从源码移至环境变量 / 配置文件并加入 `.gitignore`。

### 6. 群组创建缺少事务

**现象**：创建群组需要往 `AllGroup` 和 `GroupUser` 两张表各插一条，原实现是两条独立语句。第二条失败会留下无成员、无创建者的孤儿群组。

**修复**：两条写入包裹在同一事务中，失败整体 `rollback`。

## 已知局限

诚实记录，也是后续优化方向：

1. **Redis Pub/Sub 不保证送达**。它不持久化，publish 瞬间无订阅者则消息丢弃。当前靠「先查在线状态、对端找不到连接则落库」兜底，但仍存在竞态窗口。生产环境应换用 Kafka / RabbitMQ 等带持久化和 ACK 的消息队列。
2. **无应用层 ACK 与消息序号**。`conn->send()` 返回只代表数据进了内核发送缓冲区，不代表对端收到。要做到端到端可靠需要 seq + ACK + 重传 + 幂等去重。
3. **在线状态存在 MySQL 而非 Redis**。状态字段读写频繁，放关系库不合适，应迁到 Redis 并配合 TTL。
4. **单点依赖**。Nginx 与 Redis 均为单实例，未做主备或哨兵。
5. **群聊在锁内转发**。`groupchat` 持锁遍历成员并 `send`，群成员多时锁持有时间偏长。应在锁内仅拷贝 `TcpConnectionPtr` 列表，出锁后再发送。

## 目录结构

```
├── include/
│   ├── public.hpp              消息类型定义
│   └── server/
│       ├── chatserver.hpp      网络层
│       ├── chatservice.hpp     业务层
│       ├── db/                 连接池 + 密码哈希
│       ├── model/              数据模型
│       └── redis/              Redis 封装
├── src/
│   ├── client/                 客户端
│   └── server/
│       ├── db/                 连接池实现
│       ├── muduo/              数据模型实现
│       └── redis/              Redis 实现
├── conf/
│   ├── nginx.conf              集群负载均衡配置
│   └── mysql.cnf.example       数据库配置示例
├── sql/chat.sql                建库建表脚本
└── test/
    ├── feature_test.py         功能测试
    └── load_test.py            压力测试
```
