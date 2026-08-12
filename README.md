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

聊天消息在服务端会被附加三个有序性字段：

| 字段 | 含义 |
|------|------|
| `convid` | 会话 ID。单聊为 `p{min}_{max}`（与方向无关），群聊为 `g{groupid}` |
| `seq` | 该会话内的单调递增序号，由服务端 Redis `INCR` 分配 |
| `msgts` | 服务端接收时间戳（毫秒），仅用于展示，不参与判序 |

## 消息有序性设计

集群环境下同一会话的消息有三条投递路径（本节点直发 / Redis 跨节点转发 / 离线补投），网络延迟不同会导致**乱序到达**。本项目用「服务端会话级发号 + 客户端会话内重排」解决。

### 为什么序号要按会话分桶，而不是全局一个

全局单调序号会让所有会话共用一个号段，接收端无法判断"我这个会话是否缺消息"——因为空洞可能属于别的会话。按会话取号后，会话内序号严格连续，客户端才能做去重与补洞判定。

不同会话之间不需要比较先后（A 的私聊和某群消息谁先谁后没有意义），因此**排序只在会话内部进行**，一个会话卡住不会阻塞其他会话展示。

### 为什么用 Redis INCR 发号

本项目是 Nginx 分发的多节点集群，同一会话的两条消息**可能落在不同节点**。若各节点本地自增，序号会冲突。

`INCR` 是 Redis 单线程内的原子操作，所有节点对同一个 key (`chat:seq:{convid}`) 取号：

- **唯一性**：原子自增，并发调用不会拿到相同号，无需分布式锁
- **跨节点一致**：与消息落在哪个节点无关，因此**不需要做会话分片**（一致性哈希把会话绑定到固定节点）
- **持久性**：计数由 Redis 的 RDB/AOF 保证；消息正文另有 MySQL 离线表兜底
- **降级**：Redis 不可用时切换到本地发号（加 `1<<40` 高位偏移与正常号段隔离），保证服务不中断并打印告警

实现见 `redis::incrSeq()`（`src/server/redis/redis.cpp`）。取号走**独立的 command 连接 + 互斥锁**——订阅连接处于订阅态被 observer 线程独占 `redisGetReply`，publish 连接被多个 IO 线程并发使用，混用会导致响应包错位。

### 取号时机：入口取号，而非各分支分别取

```cpp
// chatservice.cpp onechat/groupchat
const string convId = conv::p2p(fromid, toid);
const uint64_t seq  = _redis.incrSeq(convId);   // 先取号
js[MSG_FIELD_SEQ]   = seq;
const string payload = js.dump();                // 固化后三条路径发同一份
```

如果在"本节点直发 / Redis 转发 / 落库"各分支内分别取号，同一会话连续两条消息走不同路径时就会号段错乱。统一在入口取号，保证序号分配顺序严格等于服务端接收顺序。

Redis 订阅回调 `handleredissubscribemessage()` 只做透传，**绝不重新取号**——否则同一条消息在不同节点会有不同序号。

### 客户端会话内重排

`include/conversation_orderer.hpp`，每个会话维护 `expectSeq`：

| 情况 | 处理 |
|------|------|
| `seq == expect` | 立即上屏，`expect++`，并排空缓存中后续连续消息 |
| `seq > expect` | 中间有洞，先缓存不上屏，启动等待计时 |
| `seq < expect` 或已上屏过 | 重复消息（重连补投/重传），丢弃 |

**空洞不能无限等待**：Redis Pub/Sub 不保证送达，缺的消息可能永远不来。因此设等待超时（默认 800ms），超时后按序号升序强制放行——聊天场景下"迟到但有序"优于"永久卡住"，可用性优先。

首次见到某会话时以当前消息序号作为基线（而非假设从 1 开始），避免重连场景被误判为大量空洞。

### 离线消息的顺序

离线消息的**入库顺序不等于会话内 seq 顺序**（并发写入、跨节点降级落库都会打乱）。两层保障：

1. `OfflineMessage` 表冗余 `convid`/`seq` 列 + `idx_user_conv_seq` 索引，SQL 直接 `order by convid, seq`
2. 应用层 `sortOfflineMessages()` 兜底，兼容改造前入库的无序号历史数据（排到末尾）

### 验证

```bash
# 单元测试：会话ID方向无关性、乱序重排、去重、多会话隔离、超时放行等 14 项
./bin/orderer_test

# 端到端：假服务器故意按 1→3→2→2→4 乱序推送，验证真实客户端展示为 1,2,3,4
python3 test/order_test.py --client ./bin/chatclient

# 真实服务端：校验 seq 唯一、连续、convid 与方向无关
python3 test/order_test.py --live --host 127.0.0.1 --port 6000
```

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

### 7. 集群环境下消息乱序

**现象**：同一会话的连续消息，一条走本节点直发、一条走 Redis 跨节点转发，两条路径网络延迟不同，接收端看到的顺序与发送顺序不一致。压测中连发消息时可稳定复现。

**根因**：消息本身不携带任何顺序信息，接收端只能按到达顺序展示。多节点部署下不存在天然的全局顺序。

**修复**：服务端按会话用 Redis `INCR` 分配单调递增 `seq`（入口取号，三条投递路径共用同一份 payload），客户端按会话维护 `expectSeq` 做重排、去重与补洞，空洞超时后强制放行。详见「消息有序性设计」章节。

### 8. 群聊持锁广播导致锁竞争

**现象**：`groupchat` 在持有 `_connMutex` 的情况下遍历群成员并逐个 `send`，群成员多时锁持有时间长，阻塞其他 IO 线程的登录/登出/单聊。

**修复**：锁内只拷贝目标连接的 `TcpConnectionPtr`（`shared_ptr` 拷贝可保证 send 期间对象不被析构），真正的网络写入与数据库查询全部移到锁外。`handleredissubscribemessage` 同样处理。

## 已知局限

诚实记录，也是后续优化方向：

1. **无应用层 ACK 与重传**。`conn->send()` 返回只代表数据进了内核发送缓冲区，不代表对端收到。当前靠 `seq` 让客户端**感知**空洞，但没有主动向服务端请求重传的通道——空洞超时后是"跳过"而非"补齐"。要做到端到端可靠需要补 ACK + 重传 + 服务端消息历史存储。
2. **Redis Pub/Sub 不保证送达**。它不持久化，publish 瞬间无订阅者则消息丢弃。当前靠"先查在线状态 + publish 失败降级落库"兜底，但仍存在竞态窗口。生产环境应换用 Kafka / RabbitMQ 等带持久化和 ACK 的消息队列。
3. **序号强依赖 Redis**。Redis 不可用时降级为本地发号，此时跨节点的序号不再全局唯一（仅保证单节点内递增），客户端可能出现重排失效。生产环境应部署 Redis 主从 + Sentinel。
4. **在线状态存在 MySQL 而非 Redis**。状态字段读写频繁，放关系库不合适，应迁到 Redis 并配合 TTL。
5. **单点依赖**。Nginx 与 Redis 均为单实例，未做主备或哨兵。
6. **无会话分片**。当前依赖 Redis 全局发号规避了跨节点序号问题，因此不需要分片。但如果要去掉 Redis 依赖（改用本地发号提升性能），就必须引入一致性哈希把会话绑定到固定节点，并处理节点扩缩容时的会话迁移。

## 目录结构

```
├── include/
│   ├── public.hpp              消息类型与有序性字段定义
│   ├── conversation.hpp        会话ID规则（server/client 共用）
│   ├── conversation_orderer.hpp 客户端会话内消息重排器
│   └── server/
│       ├── chatserver.hpp      网络层
│       ├── chatservice.hpp     业务层
│       ├── db/                 连接池 + 密码哈希
│       ├── model/              数据模型
│       └── redis/              Redis 封装 + 会话发号器
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
    ├── orderer_test.cpp        重排器单元测试（14项）
    ├── order_test.py           消息有序性端到端验证
    ├── feature_test.py         功能测试
    └── load_test.py            压力测试
```
