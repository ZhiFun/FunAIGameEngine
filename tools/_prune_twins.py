"""删掉 games-src/<game>/ 里"已有同名 .png 的 .img"以及"已有同名 .wav 的 .snd"。

  py -3 tools\\_prune_twins.py games-src\\skyraider games-src\\keychase

为什么要删: games-src/<game>/ 的约定是"源(.png/.wav) + 已打包(.snd)"混放。
把 assets-packed 的 .img 也拷进来会变成同名两份 —— 卡带会因此翻倍
(build_game.py 现在会把重复的丢掉, 但仓库里留着冗余文件没意义)。
"""
import sys
from pathlib import Path

PRUNE = [(".png", ".img"), (".wav", ".snd")]

for d in sys.argv[1:]:
    p = Path(d)
    if not p.is_dir():
        print(f"{d}: 不存在")
        continue
    removed = 0
    for keep_ext, drop_ext in PRUNE:
        for f in sorted(p.glob(f"*{drop_ext}")):
            twin = f.with_suffix(keep_ext)
            if twin.is_file():
                print(f"   删 {f.name}(已有 {twin.name})")
                f.unlink()
                removed += 1
    print(f"{d}: 删了 {removed} 个冗余文件")
