"""扫描 include 写法, 判断能不能把 engine/ 改成 Arduino 库布局(src/engine/*)。

  py -3 tools\\_incscan.py

要看两件事:
 1) 引擎自己的 .cpp/.h 用的带引号 include 是不是"同目录"的
    —— 是的话, 头和 .cpp 一起搬进 src/engine/ 后不用改一行代码
 2) 仓库里别处有没有靠 include/engine 这个额外搜索路径的裸 include
    (比如 #include "Engine.h"), 有的话搬完要跟着改
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QUOTED = re.compile(r'#\s*include\s+"([^"]+)"')

ENGINE_HDRS = {p.name for p in (ROOT / "engine" / "include" / "engine").glob("*.h")}
ENGINE_DIR = ROOT / "engine" / "include" / "engine"

print("=== 1) engine/ 自己的带引号 include ===")
outside = []
for p in sorted(list((ROOT / "engine" / "src").glob("*.cpp")) +
                list(ENGINE_DIR.glob("*.h"))):
    for inc in QUOTED.findall(p.read_text(encoding="utf-8", errors="replace")):
        if "/" in inc or "\\" in inc:
            continue
        same_dir = (p.parent / inc).exists()
        if not same_dir:
            outside.append((p.relative_to(ROOT).as_posix(), inc))

if outside:
    print("   以下 include 不是同目录的(靠额外的 -I): ")
    for f, inc in outside:
        print(f"     {f}  ->  {inc}")
else:
    print("   全部是同目录带引号 include —— 头和 .cpp 一起搬不用改代码")

print()
print("=== 2) engine/ 之外, 有没有裸 include 引擎头 ===")
bare = []
for sub in ("games", "platform", "runtime", "tools"):
    d = ROOT / sub
    if not d.is_dir():
        continue
    for p in d.rglob("*"):
        if p.suffix.lower() not in (".c", ".cc", ".cpp", ".h", ".hpp"):
            continue
        for inc in QUOTED.findall(p.read_text(encoding="utf-8", errors="replace")):
            if "/" in inc or "\\" in inc:
                continue
            if inc in ENGINE_HDRS:
                bare.append((p.relative_to(ROOT).as_posix(), inc))

if bare:
    print("   这些要跟着改成 engine/xxx.h:")
    for f, inc in sorted(set(bare)):
        print(f"     {f}  ->  {inc}")
else:
    print("   没有 —— 外部一律用 engine/xxx.h, 可以去掉 include/engine 这条搜索路径")

print()
print("=== 3) 外部当前用的引擎头前缀 ===")
uses = set()
for sub in ("games", "platform", "runtime"):
    d = ROOT / sub
    if not d.is_dir():
        continue
    for p in d.rglob("*"):
        if p.suffix.lower() not in (".c", ".cc", ".cpp", ".h", ".hpp"):
            continue
        for inc in QUOTED.findall(p.read_text(encoding="utf-8", errors="replace")):
            if inc.startswith("engine/"):
                uses.add(inc)
for u in sorted(uses):
    print("   " + u)
