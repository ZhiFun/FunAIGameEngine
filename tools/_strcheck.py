"""检查脚本编译出的字符串表里到底有没有某些名字。

  py -3 tools\\_strcheck.py skyraider bg_planet ui_warning_plate sfx_clear

`build_game.py` 现在只打包"脚本引用过"的素材 —— 引用集合就是编译出的字符串表。
这个工具用来验证某个素材到底有没有被引用(漏了就会变成游戏里缺图)。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gs_compiler                        # noqa: E402

name = sys.argv[1]
src = (Path(__file__).resolve().parent.parent / "games-src" / name / "game.gs")
code, entries, strings, _globals = gs_compiler.compile_source(src.read_text(encoding="utf-8"))
table = set(strings)
print(f"{name}: {len(strings)} 个字符串, 引用集合 {len(table)} 个")
for want in sys.argv[2:]:
    print(f"   {want:<24} {'有' if want in table else '没有'}")
