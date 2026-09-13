"""看一个游戏目录的构成: 各种扩展名几个, 以及同名不同扩展名(.png/.img)的冲突。

  py -3 tools\\_dirsum.py games-src\\skyraider games-src\\keychase
"""
import sys
from collections import Counter, defaultdict
from pathlib import Path

for d in sys.argv[1:]:
    p = Path(d)
    if not p.is_dir():
        print(f"{d}: 不存在")
        continue
    ext = Counter()
    by_stem = defaultdict(set)
    for f in sorted(p.iterdir()):
        if not f.is_file():
            continue
        ext[f.suffix.lower()] += 1
        by_stem[f.stem].add(f.suffix.lower())
    print(f"=== {d} ===")
    print("   " + ", ".join(f"{k or '(无)'}x{v}" for k, v in sorted(ext.items())))
    dup = {k: v for k, v in by_stem.items() if len(v) > 1}
    if dup:
        print(f"   同名不同扩展名 {len(dup)} 组, 例: "
              + ", ".join(f"{k}{sorted(v)}" for k, v in sorted(dup.items())[:5]))
