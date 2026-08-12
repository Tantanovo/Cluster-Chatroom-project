#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""集群聊天室压测脚本。

按 JSON over TCP 协议对服务器做压力测试，覆盖三个维度：
  1) 并发连接 + 注册/登录 吞吐与延迟
  2) 一对一在线消息收发 吞吐与延迟(端到端 RTT)
  3) 长连接并发保持能力

用法:
  python3 load_test.py --host 127.0.0.1 --port 6000 \
      --conns 200 --msgs 20
"""
import socket
import json
import time
import struct
import argparse
import threading
import statistics
from concurrent.futures import ThreadPoolExecutor, as_completed

# 消息类型(与 public.hpp 一致)
LOGIN, LOGIN_ACK, LOGINOUT, REG, REG_ACK = 1, 2, 3, 4, 5
ONE_CHAT, ADD_FRIEND, CREATE_GROUP, ADD_GROUP, GROUP_CHAT = 6, 7, 8, 9, 10


def now():
    return time.perf_counter()


# 协议：每个请求/响应为 [4字节网络序长度头][JSON body]
_HEADER = struct.Struct("!I")


def send(s, d):
    body = json.dumps(d).encode()
    s.sendall(_HEADER.pack(len(body)) + body)


# 每连接缓冲区，按长度头切帧，处理粘包/拆包
_RECV_BUF = {}


def recv_one(s):
    """读取一条完整 json 响应。按长度头分帧，缓冲处理粘包/拆包。"""
    fd = s.fileno()
    buf = _RECV_BUF.get(fd, b"")
    while True:
        if len(buf) >= 4:
            body_len = _HEADER.unpack(buf[:4])[0]
            if body_len == 0 or body_len > 64 * 1024:
                _RECV_BUF[fd] = b""
                return None
            if len(buf) >= 4 + body_len:
                body = buf[4:4 + body_len]
                _RECV_BUF[fd] = buf[4 + body_len:]
                try:
                    return json.loads(body.decode())
                except json.JSONDecodeError:
                    continue
        data = s.recv(8192)
        if not data:
            _RECV_BUF[fd] = buf
            return None
        buf += data


def close_sock(s):
    _RECV_BUF.pop(s.fileno(), None)
    try:
        s.close()
    except Exception:
        pass


def percentile(vals, p):
    if not vals:
        return 0.0
    vals = sorted(vals)
    k = int(round((len(vals) - 1) * p / 100.0))
    return vals[k]


def summarize(name, latencies_ms, total_time_s, ok, fail):
    n = len(latencies_ms)
    print(f"\n--- {name} ---")
    print(f"  成功/失败:     {ok} / {fail}")
    print(f"  总耗时:        {total_time_s*1000:.1f} ms")
    if n:
        print(f"  吞吐(QPS):     {ok/total_time_s:.0f} 次/秒")
        print(f"  延迟 avg:      {statistics.mean(latencies_ms):.2f} ms")
        print(f"  延迟 p50:      {percentile(latencies_ms,50):.2f} ms")
        print(f"  延迟 p90:      {percentile(latencies_ms,90):.2f} ms")
        print(f"  延迟 p99:      {percentile(latencies_ms,99):.2f} ms")
        print(f"  延迟 max:      {max(latencies_ms):.2f} ms")


# ----------------------------------------------------------------------------
# 场景 1: 并发 注册 + 登录
# ----------------------------------------------------------------------------
def scenario_reg_login(host, port, conns, suffix):
    reg_lat, login_lat = [], []
    reg_ok = reg_fail = login_ok = login_fail = 0
    lock = threading.Lock()
    ids = {}

    def worker(i):
        nonlocal reg_ok, reg_fail, login_ok, login_fail
        name = f"lt_{suffix}_{i}"
        pwd = "p"
        try:
            s = socket.create_connection((host, port), timeout=10)
            s.settimeout(10)
            # 注册
            t0 = now()
            send(s, {"msgid": REG, "name": name, "password": pwd})
            r = recv_one(s)
            dt = (now() - t0) * 1000
            if r and r.get("errno") == 0:
                with lock:
                    reg_ok += 1
                    reg_lat.append(dt)
                    ids[name] = r.get("id")
            else:
                with lock:
                    reg_fail += 1
            # 登录
            t0 = now()
            send(s, {"msgid": LOGIN, "name": name, "password": pwd})
            r = recv_one(s)
            dt = (now() - t0) * 1000
            if r and r.get("errno") == 0:
                with lock:
                    login_ok += 1
                    login_lat.append(dt)
                    ids[name] = r.get("id")
            else:
                with lock:
                    login_fail += 1
            # 登出，释放在线状态
            uid = ids.get(name)
            if uid:
                send(s, {"msgid": LOGINOUT, "id": uid})
            close_sock(s)
        except Exception:
            with lock:
                reg_fail += 1

    t_start = now()
    with ThreadPoolExecutor(max_workers=min(conns, 500)) as ex:
        futs = [ex.submit(worker, i) for i in range(conns)]
        for _ in as_completed(futs):
            pass
    total = now() - t_start

    summarize(f"场景1 并发注册 ({conns}连接)", reg_lat, total, reg_ok, reg_fail)
    summarize(f"场景1 并发登录 ({conns}连接)", login_lat, total, login_ok, login_fail)
    return ids


# ----------------------------------------------------------------------------
# 场景 2: 一对一在线消息 端到端 RTT
#   建立两个登录用户 sender / receiver，sender 发消息，receiver 收，测 RTT
# ----------------------------------------------------------------------------
def scenario_one_chat(host, port, pairs, msgs, suffix):
    lat = []
    ok = fail = 0
    lock = threading.Lock()

    def make_user(idx):
        name = f"chat_{suffix}_{idx}"
        s = socket.create_connection((host, port), timeout=10)
        s.settimeout(10)
        send(s, {"msgid": REG, "name": name, "password": "p"})
        r = recv_one(s)
        uid = r.get("id") if r else None
        send(s, {"msgid": LOGIN, "name": name, "password": "p"})
        r = recv_one(s)
        uid = r.get("id") if r and r.get("errno") == 0 else uid
        return s, uid, name

    def worker(idx):
        nonlocal ok, fail
        try:
            sa, ida, na = make_user(idx * 2)
            sb, idb, nb = make_user(idx * 2 + 1)
            # A 加 B 好友（可选，单聊不强制）
            for _ in range(msgs):
                t0 = now()
                send(sa, {"msgid": ONE_CHAT, "id": ida, "name": na,
                          "toid": idb, "msg": "x", "time": "now"})
                r = recv_one(sb)
                dt = (now() - t0) * 1000
                if r and r.get("msgid") == ONE_CHAT:
                    with lock:
                        ok += 1
                        lat.append(dt)
                else:
                    with lock:
                        fail += 1
            if ida:
                send(sa, {"msgid": LOGINOUT, "id": ida})
            if idb:
                send(sb, {"msgid": LOGINOUT, "id": idb})
            close_sock(sa)
            close_sock(sb)
        except Exception:
            with lock:
                fail += 1

    t_start = now()
    with ThreadPoolExecutor(max_workers=min(pairs, 300)) as ex:
        futs = [ex.submit(worker, i) for i in range(pairs)]
        for _ in as_completed(futs):
            pass
    total = now() - t_start
    summarize(f"场景2 单聊在线RTT ({pairs}对 x {msgs}条)", lat, total, ok, fail)


# ----------------------------------------------------------------------------
# 场景 3: 并发长连接保持（瞬时在线数）
# ----------------------------------------------------------------------------
def scenario_concurrent_conns(host, port, conns, hold_s, suffix):
    sockets = []
    ok = fail = 0
    lock = threading.Lock()
    connect_lat = []

    def worker(i):
        nonlocal ok, fail
        name = f"hold_{suffix}_{i}"
        try:
            t0 = now()
            s = socket.create_connection((host, port), timeout=10)
            s.settimeout(10)
            send(s, {"msgid": REG, "name": name, "password": "p"})
            recv_one(s)
            send(s, {"msgid": LOGIN, "name": name, "password": "p"})
            r = recv_one(s)
            dt = (now() - t0) * 1000
            if r and r.get("errno") == 0:
                with lock:
                    ok += 1
                    connect_lat.append(dt)
                    sockets.append((s, r.get("id")))
            else:
                with lock:
                    fail += 1
                close_sock(s)
        except Exception:
            with lock:
                fail += 1

    t_start = now()
    with ThreadPoolExecutor(max_workers=min(conns, 500)) as ex:
        futs = [ex.submit(worker, i) for i in range(conns)]
        for _ in as_completed(futs):
            pass
    total = now() - t_start
    print(f"\n--- 场景3 并发长连接保持 ---")
    print(f"  目标连接数:    {conns}")
    print(f"  成功在线:      {ok}")
    print(f"  失败:          {fail}")
    print(f"  建链+登录耗时: {total*1000:.1f} ms")
    if connect_lat:
        print(f"  单连接建立+登录 avg: {statistics.mean(connect_lat):.2f} ms")
        print(f"  p99:                 {percentile(connect_lat,99):.2f} ms")
    print(f"  保持 {hold_s}s ...")
    time.sleep(hold_s)
    # 全部登出关闭
    for s, uid in sockets:
        try:
            if uid:
                send(s, {"msgid": LOGINOUT, "id": uid})
            close_sock(s)
        except Exception:
            pass
    print(f"  已释放 {len(sockets)} 个连接")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=6000)
    ap.add_argument("--conns", type=int, default=200, help="场景1/3 并发连接数")
    ap.add_argument("--pairs", type=int, default=50, help="场景2 单聊对数")
    ap.add_argument("--msgs", type=int, default=20, help="场景2 每对消息数")
    ap.add_argument("--hold", type=int, default=3, help="场景3 保持秒数")
    ap.add_argument("--only", default="", help="只跑某场景: 1/2/3")
    args = ap.parse_args()

    suffix = str(int(time.time() * 1000) % 1000000)
    print("=" * 60)
    print(f"压测目标 {args.host}:{args.port}  suffix={suffix}")
    print("=" * 60)

    if args.only in ("", "1"):
        scenario_reg_login(args.host, args.port, args.conns, suffix + "a")
    if args.only in ("", "2"):
        scenario_one_chat(args.host, args.port, args.pairs, args.msgs, suffix + "b")
    if args.only in ("", "3"):
        scenario_concurrent_conns(args.host, args.port, args.conns, args.hold, suffix + "c")

    print("\n" + "=" * 60)
    print("压测结束")


if __name__ == "__main__":
    main()
