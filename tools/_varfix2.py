"""修掉 `var X = ...X...` 这种"重复声明 + 自我引用"的坏行。

背景: tools/_fixvar.py 如果对同一个文件跑了两遍, 第二遍认不出已经存在的 `var X = ...`,
于是在后面的 `X = X + 1` 上又插一次 `var`, 变成 `var X = X + 1` ——
这会给循环变量另开一个槽, 条件永远读到旧槽(0) => 死循环。

规则: 在同一个 fn/on 块里, 如果 `var X = ...` 的右值又用到 X, 且这个块前面已经有 `var X`,
就把这一行的 `var ` 去掉。
"""
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
lines = path.read_text(encoding="utf-8").split("\n")

DECL = re.compile(r"^(\s*)var\s+([A-Za-z_]\w*)\s*=\s*(.*)$")
BLOCK = re.compile(r"^(?:fn\s+([A-Za-z_]\w*)\s*\(([^)]*)\)|on\s+([A-Za-z_]\w*)).*\{\s*$")

declared = set()      # 当前块里已经出现过的 var 名
out = []
fixed = 0

for ln in lines:
    if BLOCK.match(ln):
        declared = set()
        out.append(ln)
        continue
    if ln == "}":
        declared = set()
        out.append(ln)
        continue

    m = DECL.match(ln)
    if m:
        indent, name, rest = m.groups()
        if name in declared and re.search(r"\b" + re.escape(name) + r"\b", rest):
            fixed += 1
            print(f"[修] {ln.strip()}  ->  {indent}{name} = {rest}")
            out.append(f"{indent}{name} = {rest}")
            continue
        declared.add(name)
    out.append(ln)

path.write_text("\n".join(out), encoding="utf-8")
print(f"{path.name}: 修了 {fixed} 处自我引用 var")
