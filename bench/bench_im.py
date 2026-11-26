#!/usr/bin/env python3
"""
轻讯（QingXun）分布式 IM 端到端压测脚本
仅依赖 Python 3 标准库（asyncio），覆盖 4 个核心链路：
  1. GateServer HTTP 登录链路（HTTP -> MySQL -> gRPC StatusServer -> Redis）
  2. ChatServer TCP 长连接心跳（纯网络框架 + 逻辑队列吞吐）
  3. ChatServer 同服私聊消息（1017 -> MySQL 落库 -> 1018 回执 + 1019 在线推送）
  4. ChatServer 跨服私聊消息（ChatServer1 -> MySQL -> gRPC -> ChatServer2 -> 1019 推送）

测试账号使用独立 uid 段（990001+），结束后自动清理 MySQL / Redis 测试数据，不污染业务库。
"""

import argparse
import asyncio
import json
import statistics
import struct
import subprocess
import sys
import time
import uuid
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

# 与 ChatServer/const.h 一致的消息 ID
MSG_CHAT_LOGIN = 1005
MSG_CHAT_LOGIN_RSP = 1006
ID_TEXT_CHAT_MSG_REQ = 1017
ID_TEXT_CHAT_MSG_RSP = 1018
ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019
ID_HEART_BEAT_REQ = 1023
ID_HEARTBEAT_RSP = 1024

BENCH_UID_BASE = 990001
BENCH_PASS = "bench123456"


def xor_string(s: str) -> str:
    code = len(s) % 255
    return "".join(chr(ord(c) ^ code) for c in s)


def pack_msg(msg_id: int, payload: dict) -> bytes:
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    return struct.pack("!HH", msg_id, len(body)) + body


async def read_msg(reader: asyncio.StreamReader, timeout: float = 10.0) -> Tuple[int, dict]:
    header = await asyncio.wait_for(reader.readexactly(4), timeout=timeout)
    msg_id, length = struct.unpack("!HH", header)
    body = await asyncio.wait_for(reader.readexactly(length), timeout=timeout)
    return msg_id, json.loads(body.decode("utf-8"))


def percentile(sorted_vals: List[float], p: float) -> float:
    if not sorted_vals:
        return 0.0
    idx = int((len(sorted_vals) - 1) * p)
    return sorted_vals[idx]


def summarize(name: str, latencies_ms: List[float], elapsed: float, total: int,
              errors: int, extra: str = "") -> dict:
    ok = len(latencies_ms)
    qps = ok / elapsed if elapsed > 0 else 0.0
    s = sorted(latencies_ms)
    avg = statistics.mean(s) if s else 0.0
    p50 = percentile(s, 0.50)
    p95 = percentile(s, 0.95)
    p99 = percentile(s, 0.99)
    mx = s[-1] if s else 0.0
    print(f"\n[{name}]")
    print(f"  Total: {total} | Success: {ok} ({ok * 100.0 / max(total, 1):.2f}%) | Errors: {errors}")
    print(f"  Elapsed: {elapsed:.3f}s | QPS: {qps:.1f} req/s")
    print(f"  Latency (ms): avg={avg:.2f}  p50={p50:.2f}  p95={p95:.2f}  p99={p99:.2f}  max={mx:.2f}")
    if extra:
        print(f"  {extra}")
    return {
        "name": name,
        "total": total,
        "ok": ok,
        "errors": errors,
        "elapsed_s": round(elapsed, 3),
        "qps": round(qps, 1),
        "avg_ms": round(avg, 2),
        "p50_ms": round(p50, 2),
        "p95_ms": round(p95, 2),
        "p99_ms": round(p99, 2),
        "max_ms": round(mx, 2),
    }


def run_mysql(sql: str, mysql_user: str, mysql_pass: str, mysql_db: str) -> str:
    cmd = [
        "mysql", f"-u{mysql_user}", f"-p{mysql_pass}",
        "-h127.0.0.1", "-P3306", "-N", "-B", mysql_db, "-e", sql,
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"mysql failed: {res.stderr.strip()}")
    return res.stdout


def run_redis(args: List[str], redis_port: int, redis_pass: str) -> str:
    cmd = ["redis-cli", "-h", "127.0.0.1", "-p", str(redis_port), "-a", redis_pass, "--no-auth-warning"] + args
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"redis-cli failed: {res.stderr.strip()}")
    return res.stdout.strip()


def snapshot_server_rss() -> Dict[str, float]:
    out = subprocess.run(
        ["ps", "-eo", "comm,rss"], capture_output=True, text=True
    ).stdout
    targets = {"ApiGateway", "RouteServer", "MsgServer", "FileServer"}
    rss_mb: Dict[str, float] = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] in targets:
            rss_mb[parts[0]] = round(rss_mb.get(parts[0], 0.0) + int(parts[1]) / 1024.0, 1)
    return rss_mb


def setup_bench_data(num_users: int, mysql_user: str, mysql_pass: str, mysql_db: str,
                     redis_port: int, redis_pass: str) -> Tuple[List[int], Dict[Tuple[int, int], int], Dict[str, str]]:
    uids = [BENCH_UID_BASE + i for i in range(num_users)]
    # 备份 logincount，避免影响原有计数
    orig_counts = {
        "im-node-1": run_redis(["HGET", "logincount", "im-node-1"], redis_port, redis_pass),
        "im-node-2": run_redis(["HGET", "logincount", "im-node-2"], redis_port, redis_pass),
    }

    # 先清理上次可能残留的 bench 数据
    cleanup_bench_data(uids, mysql_user, mysql_pass, mysql_db, redis_port, redis_pass, None)

    values = []
    for uid in uids:
        values.append(
            f"({uid}, 'bench_{uid}', 'bench_{uid}@bench.local', '{BENCH_PASS}', "
            f"'bench_{uid}', '', 0, ':/res/head_1.jpg')"
        )
    sql = (
        "INSERT INTO `user` (`uid`, `name`, `email`, `pwd`, `nick`, `desc`, `sex`, `icon`) VALUES "
        + ",".join(values) + ";"
    )
    run_mysql(sql, mysql_user, mysql_pass, mysql_db)

    # 预先为相邻成对用户创建私聊 thread
    pair_threads: Dict[Tuple[int, int], int] = {}
    for i in range(0, num_users - 1, 2):
        u1, u2 = uids[i], uids[i + 1]
        out = run_mysql(
            f"INSERT INTO chat_thread (type) VALUES ('private'); "
            f"SET @tid = LAST_INSERT_ID(); "
            f"INSERT INTO private_chat (thread_id, user1_id, user2_id) VALUES (@tid, {u1}, {u2}); "
            f"SELECT @tid;",
            mysql_user, mysql_pass, mysql_db,
        )
        tid = int(out.strip().splitlines()[-1])
        pair_threads[(u1, u2)] = tid

    # 预先写入固定 token 到 Redis，方便 TCP 压测直接登录指定 ChatServer 节点
    pipe_lines = []
    for uid in uids:
        pipe_lines.append(f"SET utoken_{uid} bench_token_{uid}")
    proc = subprocess.run(
        ["redis-cli", "-h", "127.0.0.1", "-p", str(redis_port), "-a", redis_pass, "--no-auth-warning"],
        input="\n".join(pipe_lines) + "\n",
        text=True, capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"redis pipeline failed: {proc.stderr}")

    return uids, pair_threads, orig_counts


def cleanup_bench_data(uids: List[int], mysql_user: str, mysql_pass: str, mysql_db: str,
                       redis_port: int, redis_pass: str, orig_counts: Optional[Dict[str, str]]):
    if not uids:
        return
    u_min, u_max = min(uids), max(uids)
    sql = f"""
    DELETE FROM chat_message WHERE sender_id BETWEEN {u_min} AND {u_max} OR recv_id BETWEEN {u_min} AND {u_max};
    DELETE FROM chat_thread WHERE id IN (
        SELECT thread_id FROM private_chat WHERE user1_id BETWEEN {u_min} AND {u_max}
    );
    DELETE FROM private_chat WHERE user1_id BETWEEN {u_min} AND {u_max};
    DELETE FROM friend WHERE self_id BETWEEN {u_min} AND {u_max} OR friend_id BETWEEN {u_min} AND {u_max};
    DELETE FROM user WHERE uid BETWEEN {u_min} AND {u_max};
    """
    run_mysql(sql, mysql_user, mysql_pass, mysql_db)

    keys = []
    for uid in uids:
        keys.extend([
            f"utoken_{uid}", f"uip_{uid}", f"usession_{uid}",
            f"ubaseinfo_{uid}", f"lock:lock_{uid}",
        ])
    if keys:
        # 分批 DEL，避免命令行过长
        for i in range(0, len(keys), 200):
            run_redis(["DEL"] + keys[i:i + 200], redis_port, redis_pass)

    if orig_counts is not None:
        for srv, val in orig_counts.items():
            if val != "":
                run_redis(["HSET", "logincount", srv, val], redis_port, redis_pass)


# ----------------------------------------------------------------------
# 1. GateServer HTTP 登录压测
# ----------------------------------------------------------------------
async def http_post_json(host: str, port: int, path: str, payload: dict, timeout: float = 10.0) -> Tuple[int, dict]:
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    req = (
        f"POST {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        f"Content-Type: application/json\r\n"
        f"Content-Length: {len(body)}\r\n"
        f"Connection: close\r\n\r\n"
    ).encode("utf-8") + body

    reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port), timeout=timeout)
    try:
        writer.write(req)
        await writer.drain()
        raw = await asyncio.wait_for(reader.read(), timeout=timeout)
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass

    head, _, resp_body = raw.partition(b"\r\n\r\n")
    status_line = head.split(b"\r\n", 1)[0].decode("utf-8", errors="ignore")
    status_code = int(status_line.split()[1]) if len(status_line.split()) >= 2 else 0
    return status_code, json.loads(resp_body.decode("utf-8"))


async def bench_http_login(uids: List[int], concurrency: int, total_reqs: int) -> dict:
    enc_pwd = xor_string(BENCH_PASS)
    latencies: List[float] = []
    errors = 0
    idx = 0
    lock = asyncio.Lock()

    async def worker():
        nonlocal idx, errors
        while True:
            async with lock:
                if idx >= total_reqs:
                    return
                cur = idx
                idx += 1
            uid = uids[cur % len(uids)]
            payload = {"email": f"bench_{uid}@bench.local", "passwd": enc_pwd}
            t0 = time.perf_counter()
            try:
                status, data = await http_post_json("127.0.0.1", 8080, "/user_login", payload)
                dt = (time.perf_counter() - t0) * 1000.0
                if status == 200 and data.get("error") == 0 and data.get("token"):
                    latencies.append(dt)
                else:
                    errors += 1
            except Exception:
                errors += 1

    t_start = time.perf_counter()
    await asyncio.gather(*(worker() for _ in range(concurrency)))
    elapsed = time.perf_counter() - t_start
    return summarize(
        f"1. GateServer HTTP 登录 (concurrency={concurrency})",
        latencies, elapsed, total_reqs, errors,
    )


# ----------------------------------------------------------------------
# 2. ChatServer TCP 长连接心跳压测（纯 Reactor + LogicQueue 吞吐）
# ----------------------------------------------------------------------
async def tcp_login(host: str, port: int, uid: int, token: str) -> Tuple[asyncio.StreamReader, asyncio.StreamWriter]:
    reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port), timeout=10.0)
    writer.write(pack_msg(MSG_CHAT_LOGIN, {"uid": uid, "token": token}))
    await writer.drain()
    while True:
        mid, data = await read_msg(reader, timeout=10.0)
        if mid == MSG_CHAT_LOGIN_RSP:
            if data.get("error") != 0:
                writer.close()
                raise RuntimeError(f"TCP login failed for uid={uid}: {data}")
            return reader, writer


async def bench_tcp_heartbeat(uids: List[int], num_conns: int, reqs_per_conn: int) -> dict:
    # 建连并登录
    conns = []
    for i in range(num_conns):
        uid = uids[i]
        reader, writer = await tcp_login("127.0.0.1", 8090, uid, f"bench_token_{uid}")
        conns.append((uid, reader, writer))

    latencies: List[float] = []
    errors = 0

    async def client_loop(uid: int, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        nonlocal errors
        pkt = pack_msg(ID_HEART_BEAT_REQ, {"fromuid": uid})
        for _ in range(reqs_per_conn):
            t0 = time.perf_counter()
            try:
                writer.write(pkt)
                await writer.drain()
                mid, data = await read_msg(reader, timeout=10.0)
                dt = (time.perf_counter() - t0) * 1000.0
                if mid == ID_HEARTBEAT_RSP and data.get("error") == 0:
                    latencies.append(dt)
                else:
                    errors += 1
            except Exception:
                errors += 1
                break

    t_start = time.perf_counter()
    try:
        await asyncio.gather(*(client_loop(u, r, w) for u, r, w in conns))
    finally:
        for _, _, w in conns:
            w.close()
        await asyncio.sleep(0.2)

    elapsed = time.perf_counter() - t_start
    return summarize(
        f"2. ChatServer TCP 长连接心跳 (conns={num_conns}, {reqs_per_conn}/conn)",
        latencies, elapsed, num_conns * reqs_per_conn, errors,
    )


# ----------------------------------------------------------------------
# 3 & 4. ChatServer 私聊端到端消息压测（支持同服 / 跨服 gRPC）
# ----------------------------------------------------------------------
async def bench_chat_messaging(
    label: str,
    pairs: List[Tuple[int, int, int]],
    sender_port: int,
    receiver_port: int,
    msgs_per_pair: int,
) -> dict:
    # 所有发送方和接收方分别登录到指定 ChatServer 节点
    sender_conns: List[Tuple[int, int, int, asyncio.StreamReader, asyncio.StreamWriter]] = []
    receiver_conns: List[Tuple[int, asyncio.StreamReader, asyncio.StreamWriter]] = []

    for s_uid, r_uid, tid in pairs:
        sr, sw = await tcp_login("127.0.0.1", sender_port, s_uid, f"bench_token_{s_uid}")
        rr, rw = await tcp_login("127.0.0.1", receiver_port, r_uid, f"bench_token_{r_uid}")
        sender_conns.append((s_uid, r_uid, tid, sr, sw))
        receiver_conns.append((r_uid, rr, rw))

    send_times: Dict[str, float] = {}
    ack_latencies: List[float] = []
    delivery_latencies: List[float] = []
    ack_errors = 0
    recv_count = 0
    total_msgs = len(pairs) * msgs_per_pair
    recv_done_events = [asyncio.Event() for _ in pairs]

    async def receiver_loop(idx: int, r_uid: int, reader: asyncio.StreamReader):
        nonlocal recv_count
        got = 0
        try:
            while got < msgs_per_pair:
                mid, data = await read_msg(reader, timeout=15.0)
                if mid == ID_NOTIFY_TEXT_CHAT_MSG_REQ:
                    for item in data.get("chat_datas", []):
                        uid_key = item.get("unique_id", "")
                        if uid_key in send_times:
                            delivery_latencies.append((time.perf_counter() - send_times[uid_key]) * 1000.0)
                        got += 1
                        recv_count += 1
        except Exception:
            pass
        finally:
            recv_done_events[idx].set()

    async def sender_loop(s_uid: int, r_uid: int, tid: int,
                          reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        nonlocal ack_errors
        for seq in range(msgs_per_pair):
            uniq = f"b-{s_uid}-{seq}-{uuid.uuid4().hex[:8]}"
            payload = {
                "fromuid": s_uid,
                "touid": r_uid,
                "form_type": 0,
                "thread_id": tid,
                "text_array": [{"content": f"bench msg {seq}", "unique_id": uniq}],
            }
            t0 = time.perf_counter()
            send_times[uniq] = t0
            try:
                writer.write(pack_msg(ID_TEXT_CHAT_MSG_REQ, payload))
                await writer.drain()
                while True:
                    mid, data = await read_msg(reader, timeout=15.0)
                    if mid == ID_TEXT_CHAT_MSG_RSP:
                        dt = (time.perf_counter() - t0) * 1000.0
                        if data.get("error") == 0:
                            ack_latencies.append(dt)
                        else:
                            ack_errors += 1
                        break
            except Exception:
                ack_errors += 1
                break

    recv_tasks = [
        asyncio.create_task(receiver_loop(i, r_uid, rr))
        for i, (r_uid, rr, _) in enumerate(receiver_conns)
    ]

    t_start = time.perf_counter()
    try:
        await asyncio.gather(*(sender_loop(s, r, t, sr, sw) for s, r, t, sr, sw in sender_conns))
        # 等待所有接收端把在途推送收完（最多等 5 秒）
        await asyncio.wait_for(
            asyncio.gather(*(ev.wait() for ev in recv_done_events)),
            timeout=5.0,
        )
    except asyncio.TimeoutError:
        pass
    finally:
        for t in recv_tasks:
            t.cancel()
        for _, _, _, _, sw in sender_conns:
            sw.close()
        for _, _, rw in receiver_conns:
            rw.close()
        await asyncio.sleep(0.2)

    elapsed = time.perf_counter() - t_start
    ds = sorted(delivery_latencies)
    extra = (
        f"E2E Push Delivered: {recv_count}/{total_msgs} ({recv_count * 100.0 / max(total_msgs, 1):.2f}%) | "
        f"E2E Latency (ms): p50={percentile(ds, 0.50):.2f}  p95={percentile(ds, 0.95):.2f}  p99={percentile(ds, 0.99):.2f}"
    )
    res = summarize(label, ack_latencies, elapsed, total_msgs, ack_errors, extra=extra)
    res["delivered"] = recv_count
    res["e2e_p50_ms"] = round(percentile(ds, 0.50), 2)
    res["e2e_p95_ms"] = round(percentile(ds, 0.95), 2)
    res["e2e_p99_ms"] = round(percentile(ds, 0.99), 2)
    return res


async def main():
    parser = argparse.ArgumentParser(description="轻讯 IM 分布式压测脚本")
    parser.add_argument("--mysql-user", default="xulei")
    parser.add_argument("--mysql-pass", default="123456")
    parser.add_argument("--mysql-db", default="test")
    parser.add_argument("--redis-port", type=int, default=6380)
    parser.add_argument("--redis-pass", default="123456")
    parser.add_argument("--http-conns", type=int, default=30)
    parser.add_argument("--http-reqs", type=int, default=300)
    parser.add_argument("--hb-conns", type=int, default=100)
    parser.add_argument("--hb-reqs", type=int, default=50)
    parser.add_argument("--chat-pairs", type=int, default=20)
    parser.add_argument("--chat-msgs", type=int, default=30)
    parser.add_argument("--cross-pairs", type=int, default=10)
    parser.add_argument("--cross-msgs", type=int, default=30)
    parser.add_argument("--json-out", default="")
    args = parser.parse_args()

    num_users = max(args.hb_conns, args.chat_pairs * 2, args.cross_pairs * 2, 40)
    if num_users % 2 == 1:
        num_users += 1

    print("=" * 68)
    print(" 轻讯（QingXun）分布式 IM 压测开始")
    print("=" * 68)
    print(f"准备 {num_users} 个临时压测账号（uid {BENCH_UID_BASE}..{BENCH_UID_BASE + num_users - 1}）...")
    uids, pair_threads, orig_counts = setup_bench_data(
        num_users, args.mysql_user, args.mysql_pass, args.mysql_db,
        args.redis_port, args.redis_pass,
    )

    rss_before = snapshot_server_rss()
    print(f"压测前各服务内存 RSS (MB): {rss_before}")

    results = []
    try:
        # 1. HTTP 登录压测（GateServer + MySQL + StatusServer gRPC + Redis）
        r1 = await bench_http_login(uids, args.http_conns, args.http_reqs)
        results.append(r1)

        # HTTP 登录会覆盖 utoken_<uid>，重新写入固定 token 供 TCP 压测使用
        pipe_lines = [f"SET utoken_{u} bench_token_{u}" for u in uids]
        subprocess.run(
            ["redis-cli", "-h", "127.0.0.1", "-p", str(args.redis_port),
             "-a", args.redis_pass, "--no-auth-warning"],
            input="\n".join(pipe_lines) + "\n",
            text=True, capture_output=True, check=True,
        )

        # 2. ChatServer TCP 心跳压测
        r2 = await bench_tcp_heartbeat(uids, args.hb_conns, args.hb_reqs)
        results.append(r2)

        # 3. ChatServer 同服私聊消息（入库 + 1018 回执 + 1019 同服推送）
        same_pairs = [
            (uids[2 * i], uids[2 * i + 1], pair_threads[(uids[2 * i], uids[2 * i + 1])])
            for i in range(args.chat_pairs)
        ]
        r3 = await bench_chat_messaging(
            f"3. ChatServer 同服私聊入库+推送 (pairs={args.chat_pairs}, {args.chat_msgs} msgs/pair)",
            same_pairs, 8090, 8090, args.chat_msgs,
        )
        results.append(r3)

        # 4. ChatServer 跨服私聊消息（ChatServer1 -> gRPC -> ChatServer2 -> 1019 推送）
        cross_pairs = [
            (uids[2 * i], uids[2 * i + 1], pair_threads[(uids[2 * i], uids[2 * i + 1])])
            for i in range(args.cross_pairs)
        ]
        r4 = await bench_chat_messaging(
            f"4. ChatServer 跨服私聊 gRPC 转发 (pairs={args.cross_pairs}, {args.cross_msgs} msgs/pair)",
            cross_pairs, 8090, 8091, args.cross_msgs,
        )
        results.append(r4)

    finally:
        rss_after = snapshot_server_rss()
        print(f"\n压测后各服务内存 RSS (MB): {rss_after}")
        print("清理临时压测账号、消息与 Redis 键...")
        cleanup_bench_data(
            uids, args.mysql_user, args.mysql_pass, args.mysql_db,
            args.redis_port, args.redis_pass, orig_counts,
        )
        print("清理完成。")

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump({"rss_before": rss_before, "rss_after": rss_after, "results": results},
                      f, ensure_ascii=False, indent=2)
        print(f"JSON 报告已写入: {args.json_out}")


if __name__ == "__main__":
    asyncio.run(main())
