"""在参考 VM 里跑 fighter 3 帧, 20 秒后如果还没结束就打印卡住的位置。

用来定位"脚本里有死循环"这类问题 —— gs_run 卡住时什么输出都没有,
加个 watchdog 才能看到它是停在哪个 opcode / 哪一行 Python。
"""
import faulthandler
import runpy
import sys

faulthandler.dump_traceback_later(20, exit=True)
sys.argv = ["gs_run.py", "games-src/fighter/game.gs", "--frames", "3", "--globals", "gPhase"]
runpy.run_path("tools/gs_run.py", run_name="__main__")
