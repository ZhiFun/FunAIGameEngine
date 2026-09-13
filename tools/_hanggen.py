"""生成一份带"步数看门狗"的 gs_run.py 副本(_hangvm.py), 用来定位字节码死循环。

死循环时 VM 是个 `while True`, faulthandler 只能指到那一行, 没有 PC。
副本里数指令条数, 超了就把当前 pc 抛出来 —— 再配 gs_compiler --listing 就能看到是哪一段。
"""
from pathlib import Path

src = Path("tools/gs_run.py").read_text(encoding="utf-8")
old = "        while True:\n"
assert src.count(old) == 1, src.count(old)
new = (
    "        _steps = 0\n"
    "        while True:\n"
    "            _steps += 1\n"
    "            if _steps > 2000000:\n"
    "                raise RuntimeError('HANG pc=%d steps=%d' % (pc, _steps))\n"
)
Path("tools/_hangvm.py").write_text(src.replace(old, new), encoding="utf-8")
print("wrote tools/_hangvm.py")
