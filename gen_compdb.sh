#!/bin/bash
# ============================================================
# 重新生成 compile_commands.json（给 clangd / IntelliSense 用）
#
# 作用：让 IDE 知道每个 .cpp 的编译参数（头文件路径、宏定义），
#       这样 Ctrl+Click 跳转函数/类定义才稳定。
#
# 什么时候跑：
#   - 新增/删除源文件后
#   - 新增第三方头文件目录后
#   - 发现 Ctrl+Click 又失效时
#
# 用法：./gen_compdb.sh
# ============================================================
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"

echo "[1/2] 生成服务端 CMake 编译数据库..."
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null

echo "[2/2] 生成客户端 qmake 编译数据库并合并..."
python3 - "$ROOT" <<'PY'
import json
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(sys.argv[1])
qt_dir = root / "testqt"

entries = json.loads((root / "build" / "compile_commands.json").read_text())

# qmake 工程没有 CMake 的 compile_commands 导出，
# 这里用 make -B -n 把每条 g++ 编译命令抓出来，转成同样的格式
result = subprocess.run(
    ["make", "-B", "-n"],
    cwd=qt_dir,
    capture_output=True,
    text=True,
)
if result.returncode != 0:
    print(result.stderr)
    sys.exit(result.returncode)

seen = set()
for line in result.stdout.splitlines():
    if not line.startswith("g++ ") or " -c " not in line or " -o " not in line:
        continue
    m = re.search(r"-o\s+(\S+)\.o\s+(\S+\.cpp)\s*$", line)
    if not m:
        continue
    src = (qt_dir / m.group(2)).resolve()
    if str(src) in seen:
        continue
    seen.add(str(src))
    entries.append({
        "directory": str(qt_dir),
        "command": line,
        "file": str(src),
    })

out = root / "compile_commands.json"
out.write_text(json.dumps(entries, indent=2, ensure_ascii=False))
print(f"完成：{out}（共 {len(entries)} 条编译命令）")
PY

echo "现在可以在 IDE 里执行：Ctrl+Shift+P -> Reload Window"
