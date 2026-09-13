"""编译脚本并打出完整 traceback(编译失败时定位用)。

  py -3 tools\\gs_trace.py games-src\\keychase\\game.gs
  py -3 tools\\gs_trace.py keychase            # 也可以只给游戏名

gs_compiler.py 只打一行错误消息, 定位**编译器自己**的 bug 时不够用, 这个会打完整栈。
"""
import sys
import traceback
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import gs_compiler as g

arg = sys.argv[1] if len(sys.argv) > 1 else "vmtest"
p = Path(arg)
if not p.is_file():
    p = ROOT / "games-src" / arg / "game.gs"
src = p.read_text(encoding="utf-8")
try:
    code, entries, strings, gen = g.compile_source(src)
    print(f"OK {p.name}: {len(code)}B, {len(gen)} 内存字, 函数={gen.fn_order}")
    print(f"   数组: {sorted(gen.arrays)}")
except Exception:
    traceback.print_exc()
