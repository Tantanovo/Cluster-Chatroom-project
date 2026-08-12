#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""集群聊天室服务端功能测试：直接按 JSON 协议连服务器，逐项验证。"""
import socket
import json
import time
import struct
import sys

HOST, PORT = "127.0.0.1", 6000

# 消息类型（与 public.hpp 一致）
LOGIN, LOGIN_ACK, LOGINOUT, REG, REG_ACK = 1, 2, 3, 4, 5
ONE_CHAT, ADD_FRIEND, CREATE_GROUP, ADD_GROUP, GROUP_CHAT = 6, 7, 8, 9, 10

# 协议：每个请求/响应为 [4字节网络序长度头][JSON body]
_HEADER = struct.Struct("!I")

PASS, FAIL = 0, 0
def check(name, cond, extra=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name}  {extra}")

def conn():
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.settimeout(3)
    return s

def send(s, d):
    body = json.dumps(d).encode()
    s.sendall(_HEADER.pack(len(body)) + body)

def recv(s):
    """读取一条完整 json 响应。按长度头分帧。"""
    try:
        hdr = b""
        while len(hdr) < 4:
            chunk = s.recv(4 - len(hdr))
            if not chunk:
                return None
            hdr += chunk
        body_len = _HEADER.unpack(hdr)[0]
        if body_len == 0 or body_len > 64 * 1024:
            return None
        body = b""
        while len(body) < body_len:
            chunk = s.recv(body_len - len(body))
            if not chunk:
                return None
            body += chunk
        return json.loads(body.decode())
    except socket.timeout:
        return None
    except json.JSONDecodeError as e:
        print(f"    (json解析失败: {e})")
        return None

def reg(s, name, pwd):
    send(s, {"msgid": REG, "name": name, "password": pwd})
    return recv(s)

def login(s, name, pwd):
    send(s, {"msgid": LOGIN, "name": name, "password": pwd})
    return recv(s)

def main():
    suffix = str(int(time.time()) % 100000)
    A = "tA_" + suffix
    B = "tB_" + suffix
    pa, pb = "pwdA", "pwdB"

    print("=" * 50)
    print("1. 注册功能")
    s = conn()
    r = reg(s, A, pa)
    check("注册新用户A 成功(errno=0)", r and r.get("errno") == 0, str(r))
    idA = r.get("id") if r else None
    print(f"      -> A id={idA}")
    r2 = reg(s, A, pa)
    check("重复用户名注册被拦截(errno=1)", r2 and r2.get("errno") == 1, str(r2))
    r3 = reg(s, B, pb)
    idB = r3.get("id") if r3 else None
    check("注册新用户B 成功", r3 and r3.get("errno") == 0, str(r3))
    print(f"      -> B id={idB}")
    s.close()

    print("=" * 50)
    print("2. 登录功能")
    s = conn()
    r = login(s, A, "wrongpwd")
    check("错误密码登录失败(errno=1)", r and r.get("errno") == 1, str(r))
    # 错误密码后服务器不关连接，可继续在同一连接登录
    r = login(s, A, pa)
    check("正确用户名+密码登录成功(errno=0)", r and r.get("errno") == 0, str(r))
    check("登录返回正确的id", r and r.get("id") == idA, str(r))
    check("登录返回正确的name", r and r.get("name") == A, str(r))
    # 重复登录
    s2 = conn()
    r = login(s2, A, pa)
    check("重复登录被拦截(errno=2)", r and r.get("errno") == 2, str(r))
    s2.close()

    print("=" * 50)
    print("3. 添加好友 + 一对一聊天(在线)")
    # A 已登录(socket s)。让 B 也登录
    sb = conn()
    rb = login(sb, B, pb)
    check("B登录成功", rb and rb.get("errno") == 0, str(rb))
    # A 添加 B 为好友
    send(s, {"msgid": ADD_FRIEND, "id": idA, "friendid": idB})
    time.sleep(0.3)
    # A 给在线的 B 发消息
    send(s, {"msgid": ONE_CHAT, "id": idA, "name": A, "toid": idB,
             "msg": "hello-online", "time": "now"})
    pushed = recv(sb)
    check("B在线时收到A的实时消息",
          pushed and pushed.get("msgid") == ONE_CHAT and pushed.get("msg") == "hello-online",
          str(pushed))

    print("=" * 50)
    print("4. 一对一聊天(离线消息)")
    # B 下线
    send(sb, {"msgid": LOGINOUT, "id": idB})
    time.sleep(0.3)
    sb.close()
    time.sleep(0.3)
    # A 给离线的 B 发消息
    send(s, {"msgid": ONE_CHAT, "id": idA, "name": A, "toid": idB,
             "msg": "hello-offline", "time": "now"})
    time.sleep(0.3)
    # B 重新登录，应在登录响应里收到离线消息
    sb = conn()
    rb = login(sb, B, pb)
    offs = rb.get("offlinemsg", []) if rb else []
    got = any(json.loads(m).get("msg") == "hello-offline" for m in offs)
    check("B重新登录收到离线消息", got, str(offs))

    print("=" * 50)
    print("5. 群组功能")
    gname = "grp_" + suffix
    send(s, {"msgid": CREATE_GROUP, "id": idA, "groupname": gname,
             "groupdesc": "test group"})
    time.sleep(0.4)
    # 查询群id（通过A重新登录拿到groups）
    s3 = conn()
    # A 当前在线，重登会被拦截，所以用数据库无关方式：让B加入需要groupid
    # 改为：A 先登出再登录拿群id
    send(s, {"msgid": LOGINOUT, "id": idA})
    time.sleep(0.3)
    s.close()
    time.sleep(0.3)
    s = conn()
    ra = login(s, A, pa)
    groups = ra.get("groups", []) if ra else []
    gid = None
    for g in groups:
        gj = json.loads(g)
        if gj.get("groupname") == gname:
            gid = gj.get("id")
    check("创建群组成功(能在A的群列表查到)", gid is not None, str(groups))

    if gid is not None:
        # B 加入群
        send(sb, {"msgid": ADD_GROUP, "id": idB, "groupid": gid})
        time.sleep(0.4)
        # A 发群聊，B在线应收到
        send(s, {"msgid": GROUP_CHAT, "id": idA, "name": A, "groupid": gid,
                 "msg": "group-hi", "time": "now"})
        pushed = recv(sb)
        check("B收到群聊消息",
              pushed and pushed.get("msgid") == GROUP_CHAT and pushed.get("msg") == "group-hi",
              str(pushed))

    print("=" * 50)
    print("6. 注销")
    send(s, {"msgid": LOGINOUT, "id": idA})
    send(sb, {"msgid": LOGINOUT, "id": idB})
    time.sleep(0.3)
    s.close()
    sb.close()
    # 验证下线后能重新登录(说明状态已置offline)
    s = conn()
    r = login(s, A, pa)
    check("注销后可再次登录(状态已置offline)", r and r.get("errno") == 0, str(r))
    send(s, {"msgid": LOGINOUT, "id": idA})
    time.sleep(0.2)
    s.close()

    print("=" * 50)
    print(f"测试完成: {PASS} 通过, {FAIL} 失败")
    return 0 if FAIL == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
