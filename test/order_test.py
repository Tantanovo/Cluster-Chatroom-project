#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""消息有序性端到端验证。

分两种模式：

【mock 模式】(默认，无需编译服务端/无需MySQL/Redis)
  启动一个假服务器，故意乱序、重复、带空洞地推送消息给真实的 chatclient，
  检查客户端标准输出中的消息顺序是否被正确重排。
  用法: python3 order_test.py --client ./bin/chatclient

【live 模式】(需要真实服务端在跑)
  连接真实服务器，双向发送消息，校验服务端分配的 seq 在会话内严格递增、
  无重复、无跳号。
  用法: python3 order_test.py --live --host 127.0.0.1 --port 6000
"""
import socket
import json
import time
import struct
import argparse
import threading
import subprocess
import sys
import re

_HEADER = struct.Struct("!I")

LOGIN, LOGIN_ACK, LOGINOUT, REG, REG_ACK = 1, 2, 3, 4, 5
ONE_CHAT, ADD_FRIEND, CREATE_GROUP, ADD_GROUP, GROUP_CHAT = 6, 7, 8, 9, 10

PASS = FAIL = 0


def check(name, cond, extra=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name}  {extra}")


def pack(d):
    body = json.dumps(d).encode()
    return _HEADER.pack(len(body)) + body


def recv_frame(sock, buf):
    """从 buf 中取一帧，不足则继续 recv。返回 (obj, buf)。"""
    while True:
        if len(buf) >= 4:
            n = _HEADER.unpack(buf[:4])[0]
            if len(buf) >= 4 + n:
                body = buf[4:4 + n]
                return json.loads(body.decode()), buf[4 + n:]
        data = sock.recv(8192)
        if not data:
            return None, buf
        buf += data


# ============================================================================
# mock 模式：假服务器故意乱序推送，验证真实客户端的重排能力
# ============================================================================
def run_mock(client_path, port=16000):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(1)

    # 乱序 + 重复 + 空洞的推送脚本
    # 期望客户端最终按 seq 升序展示：m1 m2 m3 m4，且 m2 只出现一次
    pushes = [
        (1, "m1"),
        (3, "m3"),   # 提前到达，应被缓存
        (2, "m2"),   # 补洞，触发 m2、m3 一起放行
        (2, "m2"),   # 重复，应被去重丢弃
        (4, "m4"),
    ]

    proc = subprocess.Popen(
        [client_path, "127.0.0.1", str(port)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, bufsize=1)

    conn, _ = srv.accept()
    buf = b""

    try:
        # 客户端菜单：选 1 登录
        proc.stdin.write("1\nalice\npwd\n")
        proc.stdin.flush()

        # 收登录请求
        req, buf = recv_frame(conn, buf)
        assert req and req.get("msgid") == LOGIN, f"unexpected: {req}"

        # 回登录成功（不带离线消息）
        conn.sendall(pack({
            "msgid": LOGIN_ACK, "errno": 0, "id": 1, "name": "alice"
        }))
        time.sleep(0.4)

        # 乱序推送聊天消息
        for seq, text in pushes:
            conn.sendall(pack({
                "msgid": ONE_CHAT, "id": 2, "name": "bob",
                "toid": 1, "msg": text, "time": "t",
                "convid": "p1_2", "seq": seq
            }))
            time.sleep(0.12)

        # 等重排器把缓存排空（含超时放行窗口）
        time.sleep(1.5)
    finally:
        try:
            proc.stdin.close()
        except Exception:
            pass
        proc.terminate()
        try:
            out = proc.stdout.read()
        except Exception:
            out = ""
        conn.close()
        srv.close()

    print("---------- 客户端输出 ----------")
    print(out.strip()[-800:])
    print("--------------------------------")

    # 解析客户端打印出来的消息顺序（renderChatMessage 里带 [#seq]）
    seen = re.findall(r"\[#(\d+)\].*said:\s*(m\d+)", out)
    seqs = [int(s) for s, _ in seen]
    texts = [t for _, t in seen]

    print(f"客户端展示顺序: {texts}  (seq={seqs})")
    check("消息按 seq 升序展示（乱序已被重排）",
          seqs == sorted(seqs), f"got {seqs}")
    check("重复消息被去重（m2 只出现一次）",
          texts.count("m2") == 1, f"m2 x{texts.count('m2')}")
    check("四条消息全部送达", set(texts) == {"m1", "m2", "m3", "m4"},
          f"got {sorted(set(texts))}")
    check("首条为 m1", texts[:1] == ["m1"] if texts else False, f"got {texts[:1]}")


# ============================================================================
# live 模式：连真实服务端，校验 seq 的唯一性与连续性
# ============================================================================
def run_live(host, port, count):
    suffix = str(int(time.time()) % 100000)
    A, B = f"oa_{suffix}", f"ob_{suffix}"

    def conn_reg_login(name):
        s = socket.create_connection((host, port), timeout=10)
        s.settimeout(10)
        buf = b""
        s.sendall(pack({"msgid": REG, "name": name, "password": "p"}))
        r, buf = recv_frame(s, buf)
        s.sendall(pack({"msgid": LOGIN, "name": name, "password": "p"}))
        r, buf = recv_frame(s, buf)
        assert r and r.get("errno") == 0, f"login failed: {r}"
        return s, r["id"], buf

    sa, ida, bufa = conn_reg_login(A)
    sb, idb, bufb = conn_reg_login(B)
    print(f"  A id={ida}, B id={idb}")

    received = []
    stop = threading.Event()

    def reader():
        nonlocal bufb
        while not stop.is_set():
            try:
                obj, bufb = recv_frame(sb, bufb)
                if obj is None:
                    break
                if obj.get("msgid") == ONE_CHAT:
                    received.append(obj)
            except socket.timeout:
                continue
            except Exception:
                break

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    # A 连续快速发 count 条给 B
    for i in range(count):
        sa.sendall(pack({"msgid": ONE_CHAT, "id": ida, "name": A,
                         "toid": idb, "msg": f"n{i}", "time": "t"}))
    time.sleep(2.0)
    stop.set()

    print(f"  收到 {len(received)}/{count} 条")
    check("服务端为每条消息分配了 seq",
          all("seq" in m for m in received),
          f"missing seq in {sum(1 for m in received if 'seq' not in m)} msgs")

    check("服务端为每条消息分配了 convid",
          all("convid" in m for m in received))

    if received and all("convid" in m for m in received):
        convs = {m["convid"] for m in received}
        expect_conv = f"p{min(ida,idb)}_{max(ida,idb)}"
        check(f"convid 与方向无关（应为 {expect_conv}）",
              convs == {expect_conv}, f"got {convs}")

    seqs = [m["seq"] for m in received if "seq" in m]
    check("seq 无重复", len(seqs) == len(set(seqs)),
          f"{len(seqs)} msgs, {len(set(seqs))} unique")
    check("seq 严格递增连续（无跳号）",
          sorted(seqs) == list(range(min(seqs), min(seqs) + len(seqs)))
          if seqs else False,
          f"seqs={sorted(seqs)[:10]}...")

    for s in (sa, sb):
        try:
            s.close()
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser(description="消息有序性验证")
    ap.add_argument("--live", action="store_true", help="连接真实服务端测试")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=6000)
    ap.add_argument("--client", default="./bin/chatclient",
                    help="mock 模式下 chatclient 可执行文件路径")
    ap.add_argument("--count", type=int, default=30,
                    help="live 模式发送消息条数")
    args = ap.parse_args()

    print("=" * 60)
    if args.live:
        print("live 模式：校验服务端 seq 唯一性与连续性")
        print("=" * 60)
        run_live(args.host, args.port, args.count)
    else:
        print("mock 模式：校验客户端乱序重排与去重")
        print(f"client = {args.client}")
        print("=" * 60)
        run_mock(args.client)

    print("=" * 60)
    print(f"结果: PASS={PASS}  FAIL={FAIL}")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
