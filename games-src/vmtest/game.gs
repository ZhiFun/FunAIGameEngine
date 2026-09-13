# VM TEST —— 脚本 VM v2 的能力演示 + 自检
#
# 上面那行是自检结果: SELFTEST 必须是 0 —— 启动时把所有新指令验一遍, 失败一次 +100。
# 下面是个能玩的小关卡: 重力跳跃, 吃键帽加分, 被幽灵碰到掉一条命, 命用完 Game Over。
# 每项 VM 能力都对应画面上看得见的东西:
#   浮点        -> 重力/跳跃/水平缓动, LIVES 旁边 VY 读数连续变化, 底部示波条永远在动
#   数组        -> 8 块平台的坐标、8 个道具的位置, 还有 4 只幽灵各自的状态
#   位运算      -> GOT 用一个 32 位整数的 bit 记"哪个道具吃过了"
#   字符串数组  -> 走路 2 帧 + 跳跃帧, 帧号是运行时算的
#   水平翻转    -> 朝左走时把"朝右"的图翻转着画
#
# 打包: py -3 tools\build_game.py vmtest
# 试跑: py -3 tools\gs_run.py games-bin\vmtest.gbn --frames 90 --pad right,right,a --png out.png
# 引擎真实渲染: py -3 tools\sim_shot.py out.png vmtest --zoom 2

# ---- 物理(全是浮点) ----
var px = 30.0
var py = 120.0
var vx = 0.0
var vy = 0.0
var grav = 1000.0
var jumpv = -300.0
var movespd = 95.0
var vymax = 220.0
var ong = 1                      # 站在地上
var face = 0                     # 0=朝右 1=朝左
var phase = 0.0                  # 走路动画相位
var bar = 0.0                    # 底部示波条相位
var prevfeet = 0.0
var feet = 0.0

# ---- 局面状态 ----
var lives = 3
var score = 0
var ncoins = 0
var dead = 0                     # 正在播死亡动画
var deadT = 0.0
var over = 0                     # Game Over
var inv = 0.0                    # 复活后的无敌时间(秒)
var want_reset = 0               # 请求重新开局(on start 与 Game Over 共用同一段复位代码)
var cnt = 0                      # 自检失败次数, 必须是 0

# ---- 平台: x / y / 宽 ----
var plx[8] = 0
var ply[8] = 0
var plw[8] = 0

# ---- 道具 ----
var itx[8] = 0
var ity[8] = 0
var got[1] = 0                   # 位图: 第 i 位 = 第 i 个道具吃过了

# ---- 4 只幽灵: x / 方向 / 速度 / 巡逻左界 / 右界 / y ----
var gx[4] = 0.0
var gd[4] = 0.0
var gsp[4] = 0.0
var glo[4] = 0
var ghi[4] = 0
var gy[4] = 0

# ---- 帧表 ----
strs walk = { "muncher_r0", "muncher_r1", "muncher_u0" }
strs orb = { "pellet0", "pellet1" }
strs gh = { "ghost_g1_0", "ghost_g1_1" }

var i = 0
var j = 0
var si = 0
var anim = 0
var blink = 0
var hit = 0

# ================== 函数 ==================
# 每个函数一块固定内存槽(返回值 + 参数 + 局部), 所以不支持递归。
# `a = 0.0` 表示 float 参数(同时是默认值); `-> 0.0` 表示返回 float。
fn gadd(a, b) {
    var t = a + b
    return t
}

fn gscale(v = 0.0, k = 1.0) -> 0.0 {
    return v * k
}

fn gdist2(ax, ay, bx, by) {
    var dx = ax - bx
    var dy = ay - by
    return dx * dx + dy * dy
}

fn gclamp(v, lo, hi) {
    if v < lo { return lo }
    if v > hi { return hi }
    return v
}

fn greset() {                       # 无返回值, 当语句用
    got[0] = 0
}

on start {
    # ================== 自检 ==================
    cnt = 0

    # --- 浮点: 混合提升、比较、转换、数学 ---
    if grav * 0.001 < 0.99 { cnt = cnt + 100 }
    if grav * 0.001 > 1.01 { cnt = cnt + 100 }
    if itof(3) * 0.5 < 1.49 { cnt = cnt + 100 }
    if itof(3) * 0.5 > 1.51 { cnt = cnt + 100 }
    if itof(7) / 2.0 < 3.49 { cnt = cnt + 100 }
    if ftoi(-2.7) != -2 { cnt = cnt + 100 }
    if ftoi(9.99) != 9 { cnt = cnt + 100 }
    if fabs(-4.5) < 4.49 { cnt = cnt + 100 }
    if fabs(-4.5) > 4.51 { cnt = cnt + 100 }
    if fmax(1.5, 2.5) < 2.49 { cnt = cnt + 100 }
    if fmin(1.5, 2.5) > 1.51 { cnt = cnt + 100 }
    if fsqrt(9.0) < 2.99 { cnt = cnt + 100 }
    if fsqrt(9.0) > 3.01 { cnt = cnt + 100 }
    if !(3.5 > 3.0) { cnt = cnt + 100 }
    if 0.1 + 0.2 < 0.29 { cnt = cnt + 100 }

    # --- 位运算: 8 个 bit 塞进一个整数, 再逐个取回来 ---
    got[0] = 0
    got[0] = got[0] | (1 << 3)
    got[0] = got[0] | (1 << 5)
    if ((got[0] >> 3) & 1) != 1 { cnt = cnt + 100 }
    if ((got[0] >> 5) & 1) != 1 { cnt = cnt + 100 }
    if ((got[0] >> 4) & 1) != 0 { cnt = cnt + 100 }
    if got[0] != 40 { cnt = cnt + 100 }
    got[0] = got[0] & (255 ^ (1 << 5))             # 用"与上补码"清掉第 5 位
    if got[0] != 8 { cnt = cnt + 100 }
    got[0] = got[0] ^ 8                            # 异或回去清零
    if got[0] != 0 { cnt = cnt + 100 }
    if 0x0F | 0xF0 != 0xFF { cnt = cnt + 100 }
    if 0x0F & 0x03 != 0x03 { cnt = cnt + 100 }

    # --- 函数: 参数/局部/返回、int 与 float、默认参数、嵌套、提前 return ---
    if gadd(3, 4) != 7 { cnt = cnt + 100 }
    if gadd(gadd(1, 2), gadd(3, 4)) != 10 { cnt = cnt + 100 }     # 嵌套调用
    if gadd(-5, 2) != -3 { cnt = cnt + 100 }
    if gscale(2.5, 2.0) < 4.99 { cnt = cnt + 100 }                 # float 返回
    if gscale(2.5, 2.0) > 5.01 { cnt = cnt + 100 }
    if gscale(2.5) < 2.49 { cnt = cnt + 100 }                      # 少传的参数用默认值
    if gscale(2.5) > 2.51 { cnt = cnt + 100 }
    if gscale() < -0.01 { cnt = cnt + 100 }
    if gscale() > 0.01 { cnt = cnt + 100 }
    if gscale(gadd(1, 1)) < 1.99 { cnt = cnt + 100 }               # int 实参提升到 float
    if gdist2(0, 0, 3, 4) != 25 { cnt = cnt + 100 }
    if gdist2(10, 10, 10, 12) != 4 { cnt = cnt + 100 }
    if gclamp(50, 0, 20) != 20 { cnt = cnt + 100 }                 # 提前 return
    if gclamp(-5, 0, 20) != 0 { cnt = cnt + 100 }
    if gclamp(7, 0, 20) != 7 { cnt = cnt + 100 }
    got[0] = 255
    greset()                                                       # void 函数
    if got[0] != 0 { cnt = cnt + 100 }

    # --- 数组写入/越界 ---
    i = 0
    while i < 8 {
        plx[i] = 0
        ply[i] = 0
        plw[i] = 0
        i = i + 1
    }
    plx[0] = 0
    ply[0] = 132
    plw[0] = 428                    # 地面
    plx[1] = 24
    ply[1] = 106
    plw[1] = 84
    plx[2] = 180
    ply[2] = 106
    plw[2] = 68
    plx[3] = 340
    ply[3] = 106
    plw[3] = 76
    plx[4] = 96
    ply[4] = 82
    plw[4] = 72
    plx[5] = 272
    ply[5] = 82
    plw[5] = 90
    plx[6] = 168
    ply[6] = 58
    plw[6] = 92
    plx[7] = 380
    ply[7] = 58
    plw[7] = 40

    if plw[0] != 428 { cnt = cnt + 100 }
    if ply[6] != 58 { cnt = cnt + 100 }
    if plx[7] != 380 { cnt = cnt + 100 }
    plw[900] = 7                    # 远超内存池 -> 写被丢弃
    if plw[900] != 0 { cnt = cnt + 100 }
    if plx[-1] != 0 { cnt = cnt + 100 }
    plw[3] = plw[3] + 4             # 复合赋值
    if plw[3] != 80 { cnt = cnt + 100 }
    plw[3] = plw[3] - 4
    phase = 1.25
    phase += 0.25                   # 浮点标量复合赋值
    if phase < 1.49 { cnt = cnt + 100 }
    if phase > 1.51 { cnt = cnt + 100 }

    # --- 道具布点 ---
    itx[0] = 36
    ity[0] = 92
    itx[1] = 72
    ity[1] = 92
    itx[2] = 110
    ity[2] = 68
    itx[3] = 196
    ity[3] = 92
    itx[4] = 232
    ity[4] = 92
    itx[5] = 300
    ity[5] = 68
    itx[6] = 356
    ity[6] = 92
    itx[7] = 386
    ity[7] = 44

    # --- 4 只幽灵: 各自不同平台上的巡逻段(glo/ghi 要落在平台宽度内) ---
    gx[0] = 240.0
    gd[0] = 1.0
    gsp[0] = 46.0
    glo[0] = 216
    ghi[0] = 396
    gy[0] = 120                   # 地面
    gx[1] = 300.0
    gd[1] = -1.0
    gsp[1] = 58.0
    glo[1] = 272
    ghi[1] = 350
    gy[1] = 70                    # 站在 y=82 的平台上
    gx[2] = 40.0
    gd[2] = 1.0
    gsp[2] = 40.0
    glo[2] = 24
    ghi[2] = 96
    gy[2] = 94                    # 站在 y=106 的平台上
    gx[3] = 200.0
    gd[3] = -1.0
    gsp[3] = 52.0
    glo[3] = 168
    ghi[3] = 248
    gy[3] = 46                    # 站在 y=58 的平台上
    # 数组读写校验(幽灵表)
    if gsp[1] != 58.0 { cnt = cnt + 100 }
    if ghi[2] != 96 { cnt = cnt + 100 }
    if gy[3] != 46 { cnt = cnt + 100 }
    gy[3] += 2
    if gy[3] != 48 { cnt = cnt + 100 }
    gy[3] = gy[3] - 2

    # --- 素材真的在包里 ---
    if imgw("muncher_r0") != 12 { cnt = cnt + 100 }
    if imgh("muncher_r0") != 12 { cnt = cnt + 100 }
    if imgw("pellet0") != 10 { cnt = cnt + 100 }

    # 复位交给 update 做 —— 这样 Game Over 重开能复用同一段代码
    want_reset = 1
}

on update {
    # ---- 重新开局(首次进入 / Game Over 后按 A) ----
    if want_reset == 1 {
        want_reset = 0
        px = 30.0
        py = 120.0
        vx = 0.0
        vy = 0.0
        ong = 1
        face = 0
        phase = 0.0
        bar = 0.0
        lives = 3
        score = 0
        ncoins = 0
        dead = 0
        deadT = 0.0
        over = 0
        inv = 1.2
        got[0] = 0
        gx[0] = 240.0
        gd[0] = 1.0
        gsp[0] = 46.0
        bgm("bgm")
    }

    if dead == 1 {
        # ---- 死亡动画: 冻结操作, 只走计时器 ----
        deadT = deadT + dtf()
        if deadT > 1.3 {
            dead = 0
            if lives > 0 {
                px = 30.0                  # 复活: 回起点 + 短无敌
                py = 120.0
                vx = 0.0
                vy = 0.0
                ong = 1
                inv = 1.6
                gx[0] = 240.0
                gd[0] = 1.0
            } else {
                over = 1
            }
        }
    } else if over == 1 {
        # ---- Game Over: 等 A 重开 ----
        if pressed(a) { want_reset = 1 }
    } else {
        # ---- 水平: 浮点缓动, 不是硬切 ----
        j = 0
        if held(right) { j = 1 }
        if held(left) { j = -1 }
        vx = vx + (itof(j) * movespd - vx) * 9.0 * dtf()
        if vx < 0.06 && vx > -0.06 { vx = 0.0 }
        px = px + vx * dtf()
        if px < 0.0 { px = 0.0 }
        if px > 416.0 { px = 416.0 }

        # ---- 垂直: 重力 + 跳跃 ----
        if ong == 1 && pressed(a) {
            vy = jumpv
            ong = 0
            sfx("coin")
        }
        vy = vy + grav * dtf()
        if vy > vymax { vy = vymax }
        if vy < jumpv { vy = jumpv }

        prevfeet = py + 12.0
        py = py + vy * dtf()
        feet = py + 12.0

        # ---- 落台: 用"上一帧脚底 -> 这一帧脚底"扫过的区间, 再快也不会穿板 ----
        ong = 0
        if vy > 0.0 {
            i = 0
            while i < 8 {
                if feet >= itof(ply[i]) && prevfeet <= itof(ply[i]) {
                    if px + 11.0 > itof(plx[i]) && px < itof(plx[i] + plw[i]) {
                        py = itof(ply[i]) - 12.0
                        vy = 0.0
                        ong = 1
                    }
                }
                i = i + 1
            }
        }
        if py > 130.0 { py = 130.0; vy = 0.0; ong = 1 }

        # ---- 动画相位: 只有真的在移动才推进 ----
        phase = phase + fabs(vx) * dtf() * 0.09
        if phase > 2.0 { phase = phase - 2.0 }
        if vx < -5.0 { face = 1 }
        if vx > 5.0 { face = 0 }

        # ---- 吃道具: 第 i 位是 1 就说明吃过了 ----
        i = 0
        while i < 8 {
            if ((got[0] >> i) & 1) == 0 {
                if px + 11.0 > itof(itx[i]) && px < itof(itx[i] + 10) {
                    if py + 11.0 > itof(ity[i]) && py < itof(ity[i] + 10) {
                        got[0] = got[0] | (1 << i)
                        ncoins = ncoins + 1
                        score = score + 100
                        sfx("coin")
                    }
                }
            }
            i = i + 1
        }
        if ncoins >= 8 {                    # 全吃完重摆一轮, 幽灵提速
            got[0] = 0
            ncoins = 0
            gsp[0] = gsp[0] + 12.0
            if gsp[0] > 120.0 { gsp[0] = 46.0 }
            sfx("coin")
        }

        # ---- 幽灵巡逻 ----
        i = 0
        while i < 4 {
            gx[i] = gx[i] + gd[i] * gsp[i] * dtf()
            if gx[i] < itof(glo[i]) { gd[i] = 1.0 }
            if gx[i] > itof(ghi[i]) { gd[i] = -1.0 }
            i = i + 1
        }

        # ---- 无敌计时 + 碰到幽灵就死 ----
        if inv > 0.0 { inv = inv - dtf() }
        if inv <= 0.0 {
            hit = 0
            i = 0
            while i < 4 {
                if px + 10.0 > gx[i] && px + 1.0 < gx[i] + 11.0 {
                    if py + 11.0 > itof(gy[i]) && py < itof(gy[i] + 12) {
                        hit = 1
                    }
                }
                i = i + 1
            }
            if hit == 1 {
                dead = 1
                deadT = 0.0
                lives = lives - 1
                vy = 0.0
                sfx("miss")
            }
        }
    }

    # ---- 底部示波条: 纯浮点相位驱动, 所以画面永远有东西在动 ----
    bar = bar + 0.8 * dtf()
    if bar > 2.0 { bar = bar - 2.0 }
    blink = frame() % 20

    if held(select) { exit() }
}

on render {
    clear(rgb(6, 8, 18))

    # ---- 平台 ----
    i = 0
    while i < 8 {
        frect(plx[i], ply[i], plw[i], 4, rgb(70, 96, 130))
        frect(plx[i], ply[i], plw[i], 1, rgb(120, 170, 210))
        i = i + 1
    }

    # ---- 道具(两帧闪烁) ----
    i = 0
    while i < 8 {
        if ((got[0] >> i) & 1) == 0 {
            if blink < 12 {
                img(orb[0], itx[i], ity[i])
            } else {
                img(orb[1], itx[i], ity[i])
            }
        }
        i = i + 1
    }

    # ---- 幽灵 ----
    anim = frame() % 2
    i = 0
    while i < 4 {
        img(gh[anim], ftoi(gx[i]), gy[i])
        i = i + 1
    }

    # ---- 主角: 帧号运行时算, 朝左把朝右的图翻转过来 ----
    si = 0
    if ong == 1 {
        if phase > 1.0 { si = 1 }
    } else {
        si = 2
    }
    # 死亡 / 无敌期间闪烁(每 4 帧亮一次)
    j = (frame() >> 2) & 1
    if dead == 1 {
        if j == 0 { img(walk[2], ftoi(px), ftoi(py), face) }
    } else if inv > 0.0 {
        if j == 0 { img(walk[si], ftoi(px), ftoi(py), face) }
    } else {
        img(walk[si], ftoi(px), ftoi(py), face)
    }

    # ---- 顶栏 ----
    text(4, 2, "SELFTEST", rgb(150, 200, 255), 1)
    textnum(60, 2, cnt, rgb(120, 255, 150), 1)
    text(96, 2, "SCORE", rgb(150, 200, 255), 1)
    textnum(146, 2, score, rgb(255, 230, 120), 1)
    text(196, 2, "LIVES", rgb(150, 200, 255), 1)
    textnum(246, 2, lives, rgb(255, 130, 130), 1)
    text(268, 2, "GOT", rgb(150, 200, 255), 1)
    textnum(298, 2, ncoins, rgb(255, 160, 240), 1)
    text(324, 2, "VY", rgb(150, 200, 255), 1)
    textnum(346, 2, ftoi(vy), rgb(255, 160, 120), 1)

    # ---- Game Over ----
    if over == 1 {
        text(160, 58, "GAME OVER", rgb(255, 90, 90), 2)
        text(150, 82, "PRESS  A", rgb(150, 200, 255), 1)
    }

    # ---- 底部: 浮点相位示波条 + 速度条 ----
    frect(4, 136, 130, 4, rgb(24, 34, 52))
    frect(4, 136, ftoi(bar * 65.0), 4, rgb(90, 200, 255))
    frect(150, 136, 130, 4, rgb(24, 34, 52))
    frect(215, 136, ftoi(vx * 0.6), 4, rgb(255, 170, 90))
    frect(213, 135, 2, 6, rgb(120, 130, 150))
}
