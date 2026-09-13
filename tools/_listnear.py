"""打印字节码反汇编里某个 PC 区间的内容。

  py -3 tools\\_listnear.py _list.txt 29C0 2A10
"""
import sys

path, lo, hi = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
for ln in open(path, encoding="utf-8", errors="replace"):
    t = ln.split()
    keep = False
    if t:
        try:
            keep = lo <= int(t[0], 16) <= hi
        except ValueError:
            keep = ln.lstrip().startswith("<fn ")
    if keep:
        print(ln.rstrip())
