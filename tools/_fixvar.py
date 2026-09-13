"""把函数/on 段里"第一次出现的裸赋值"自动补上 var。

这门脚本语言不会自动声明局部变量, 写 `x = 1` 会报"未定义的变量"。
手工补容易漏, 所以用这个一次性脚本扫一遍。
规则: 行首(有缩进)的 `name = ...` (排除 `==`, 排除数组下标 store),
      如果 name 既不是函数形参、也不是顶层 var/strs 全局, 且在这个块里还没声明过,
      就在前面插 `var `。
"""
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
lines = path.read_text(encoding="utf-8").split("\n")

# --- 顶层全局名 ---
globals_ = set()
for ln in lines:
    m = re.match(r"^(?:var|strs)\s+([A-Za-z_]\w*)", ln)
    if m:
        globals_.add(m.group(1))

BLOCK = re.compile(r"^(?:fn\s+([A-Za-z_]\w*)\s*\(([^)]*)\)|on\s+([A-Za-z_]\w*)).*\{\s*$")
ASSIGN = re.compile(r"^(\s+)([A-Za-z_]\w*)\s*=(?!=)\s*(.*)$")

out = []
block = None          # None = 不在函数里
params = set()
seen = set()
added = 0

for ln in lines:
    if block is None:
        m = BLOCK.match(ln)
        if m:
            block = m.group(1) or ("on " + m.group(3))
            raw = m.group(2) or ""
            params = {p.split("=")[0].strip() for p in raw.split(",") if p.strip()}
            seen = set()
        out.append(ln)
        continue

    if ln == "}":
        block = None
        out.append(ln)
        continue

    m = ASSIGN.match(ln)
    if m:
        indent, name, rest = m.groups()
        if name not in params and name not in globals_ and name not in seen:
            seen.add(name)
            added += 1
            out.append(f"{indent}var {name} = {rest}")
            continue
    out.append(ln)

path.write_text("\n".join(out), encoding="utf-8")
print(f"{path.name}: 补了 {added} 处 var")
