#!/usr/bin/env python3
"""SND1 音频素材的读写 + 全仓共用的采样率上限策略。

素材格式: "SND1" | u32 rate | u32 frames | int16 data[frames]  (单声道)

为什么要限制采样率
------------------
卡带最终进键盘的 SPIFFS 分区(917,504 字节, 实际可用 ~844KB), 而三个游戏的音频占了
其中绝大部分。引擎的 `AudioMixer` 会按 `clip->rate / 输出采样率` 的步长取样, 所以素材
存成 11025Hz 在 22050Hz 输出上**时长和音高都不变**(只少一点高频细节, 短促打击音基本
听不出来)。

⚠ 这个策略必须由打包链路**强制执行**, 不能靠"记得手动跑一遍脚本":
  * `pack_assets.pack_wav()` —— WAV -> SND1 时直接按 `SND_RATE` 输出(源头就是瘦的)
  * `build_game.py`          —— 任何进卡带的 .snd 再过一道 `limit()`, 兜住手工放进
                                `games-src/<game>/` 的 22050 素材
  `limit()` 是幂等的, 重复过不会越降越低。
要恢复全质量就把本文件的 MAX_RATE 和 `pack_assets.SND_RATE` 一起改回 22050。
"""
import struct

MAGIC = b"SND1"
HDR = 12                      # magic(4) + rate(4) + frames(4)

MAX_RATE = 11025              # 素材存盘采样率上限(0 = 不限制)


def parse(raw):
    """-> (rate, frames, samples) 或 None(不是合法 SND1)"""
    if len(raw) < HDR or raw[:4] != MAGIC:
        return None
    rate, frames = struct.unpack_from("<II", raw, 4)
    if len(raw) != HDR + frames * 2:
        return None
    return rate, frames, list(struct.unpack_from("<%dh" % frames, raw, HDR))


def encode(rate, samples):
    return (MAGIC + struct.pack("<II", rate, len(samples))
            + struct.pack("<%dh" % len(samples), *samples))


def limit(raw, max_rate=MAX_RATE):
    """把 SND1 降到 <= max_rate(整数倍抽取, 窗口内取平均抗混叠)。

    -> (bytes, note) ; 已经够低 / 不是合法 SND1 时原样返回。幂等。
    """
    got = parse(raw)
    if got is None:
        return raw, "非 SND1, 原样保留"
    rate, frames, samples = got
    if max_rate <= 0 or rate <= max_rate:
        return raw, f"{rate}Hz"
    steps = (rate + max_rate - 1) // max_rate          # 22050->11025 时 steps=2
    n = frames // steps
    out = [sum(samples[i * steps:(i + 1) * steps]) // steps for i in range(n)]
    return encode(rate // steps, out), f"{rate}Hz->{rate // steps}Hz"


def summary(blobs):
    """给一批 SND 字节做个一句话摘要(打包时打出来, 让采样率策略可见)。

    -> (文字, 超限个数)
    """
    rates = []
    for b in blobs:
        got = parse(b)
        if got:
            rates.append(got[0])
    if not rates:
        return "", 0
    over = sum(1 for r in rates if MAX_RATE and r > MAX_RATE)
    lo, hi = min(rates), max(rates)
    span = f"{lo}Hz" if lo == hi else f"{lo}~{hi}Hz"
    return f"音频 {len(rates)} 个 @{span}", over
