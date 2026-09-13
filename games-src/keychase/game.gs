# KEY CHASE —— 迷宫吃豆(脚本卡带版)
#
# 和原生 C++ 版同一套玩法: 吃光豆过关, 躲四个追兵, 大力丸能反过来吃它们。
#   迷宫 30x9, 第 4 行两端是隧道(走出去从另一边回来); 中间是鬼屋, 门只有追兵能穿。
#   四个追兵脾气不同(直追 / 抄前路 / 反折夹击 / 近则回角), 巡逻-追击交替;
#   大力丸期间乱跑变慢, 被吃后只剩眼睛高速跑回鬼屋。
#   豆 10 分, 大力丸 50, 连吃追兵 200/400/800/1600, 水果 100/300/500。
#   3 条命, 10000 分加命, 第 70/170 颗豆出水果。
#
# 迷宫写成下面的字符画('#'墙 '.'豆 'o'大力丸 '-'门 ' '空地),
# 启动时编译成位图(wallb/doorb/dots/powb), 之后全走位运算 —— 每帧都查迷宫, 不能慢。
#
# 打包: py -3 tools\build_game.py keychase
# 试跑: py -3 tools\gs_run.py games-bin\keychase.gbn --frames 300 --pad left,a --png out.png

# ---- 几何 ----
var kTile = 14
var kMazeX = 4
var kMazeY = 16
var kTunnelRow = 4
var kCols = 30
var kRows = 9

# ---- 颜色 ----
var cBg = 0
var cWall = 0
var cEdge = 0
var cDoor = 0
var cText = 0
var cDim = 0
var cAccent = 0

# ---- 方向: 0=右 1=左 2=上 3=下 4=无 ----
var DIR_R = 0
var DIR_L = 1
var DIR_U = 2
var DIR_D = 3
var DIR_N = 4

# ---- 阶段 ----
var PH_READY = 0
var PH_PLAY = 1
var PH_DYING = 2
var PH_CLEAR = 3
var PH_OVER = 4
# ---- 追兵状态 ----
var GH_HOME = 0
var GH_LEAVE = 1
var GH_NORMAL = 2
var GH_SCARED = 3
var GH_EATEN = 4

# ---- 速度(px/s) ----
var spdPlayer = 62.0
var spdGhost = 57.0
var spdScared = 34.0
var spdEaten = 126.0
var spdTunnel = 38.0

# ---- 计时 ----
var tReady = 1.7
var tDying = 1.9
var tClear = 2.3
var tFruit = 9.0

# ---- 局面 ----
var phase = 0
var phaseT = 0.0
var lives = 3
var score = 0
var level = 1
var dotsLeft = 0
var dotsEaten = 0
var extraGiven = 0
var fruitShown = 0
var fruitT = 0.0
var frightT = 0.0
var chain = 0
var mode = 0
var modeT = 0.0
var eatPause = 0.0
var chompAlt = 0
var blink = 0

# ---- 迷宫位图: 每行一个整数, bit c = 第 c 格 ----
var wallb[9] = 0
var doorb[9] = 0
var dots[9] = 0
var powb[9] = 0

# ---- 主角/追兵统一按 actor 处理: 下标 0 = 主角, 1..4 = 追兵 ----
var ax[5] = 0.0
var ay[5] = 0.0
var adir[5] = 0
var awant[5] = 0
var aspd[5] = 0.0

# ---- 追兵独有 ----
var gstate[4] = 0
var grel[4] = 0               # 吃够多少豆放出来
var grelw[4] = 0.0            # 或者最多等这么久
var gtimer[4] = 0.0
var ghomec[4] = 0
var gsc_c[4] = 0              # 巡逻时各自去的角
var gsc_r[4] = 0

# ---- 水果 3 个位置/分值 ----
var fruitSc[3] = 0

# ---- 循环用的临时量 ----
var i = 0
var j = 0
var k = 0

strs mrow = {
    "##############################",
    "#o............##............o#",
    "####.####.###.######.####.####",
    "#...........#-####...........#",
    "  ..........#    #..........  ",
    "#...........######...........#",
    "######.####.######.#####.#####",
    "#o............ .............o#",
    "##############################",
}
strs munch = { "muncher_r0", "muncher_r1",
               "muncher_l0", "muncher_l1",
               "muncher_u0", "muncher_u1",
               "muncher_d0", "muncher_d1" }
strs gani = { "ghost_g1_0", "ghost_g1_1",
              "ghost_g2_0", "ghost_g2_1",
              "ghost_g3_0", "ghost_g3_1",
              "ghost_g4_0", "ghost_g4_1" }
strs gscared = { "ghost_scared0", "ghost_scared1" }
strs geyes = { "eyes_r", "eyes_l", "eyes_u", "eyes_d" }
strs fruit = { "fruit0", "fruit1", "fruit2" }
strs pellet = { "pellet0", "pellet1" }

# ================== 小工具 ==================

fn iabs(v) -> 0 {
    if v < 0 { return 0 - v }
    return v
}

fn imin2(a, b) -> 0 {
    if a < b { return a }
    return b
}

# 列取模(隧道): 必须是"正的"取模, 否则 col=-1 会去读数组的 -1
fn wrapc(c) -> 0 {
    if c < 0 { return c + kCols }
    if c >= kCols { return c - kCols }
    return c
}

fn ddx(d) -> 0 {
    if d == 0 { return 1 }
    if d == 1 { return -1 }
    return 0
}

fn ddy(d) -> 0 {
    if d == 2 { return -1 }
    if d == 3 { return 1 }
    return 0
}

fn revd(d) -> 0 {
    if d == 0 { return 1 }
    if d == 1 { return 0 }
    if d == 2 { return 3 }
    return 2
}

fn gcx(c) -> 0 { return kMazeX + c * kTile + kTile / 2 }
fn gcy(r) -> 0 { return kMazeY + r * kTile + kTile / 2 }

# 像素 -> 格子。负数要向下取整(C 的整除是向零截断, 隧道那格会差 1)
fn colof(x = 0.0) -> 0 {
    var v = ftoi(x) - (kMazeX + kTile / 2)
    var c = v / kTile
    if v < 0 && v % kTile != 0 { c = c - 1 }
    return wrapc(c)
}

fn rowof(y = 0.0) -> 0 {
    var v = ftoi(y) - (kMazeY + kTile / 2)
    var r = v / kTile
    if v < 0 && v % kTile != 0 { r = r - 1 }
    if r < 0 { return 0 }
    if r >= kRows { return kRows - 1 }
    return r
}

fn is_wall(c, r) -> 0 {
    if r < 0 { return 1 }
    if r >= kRows { return 1 }
    if ((wallb[r] >> wrapc(c)) & 1) == 1 { return 1 }
    return 0
}

fn is_door(c, r) -> 0 {
    if r < 0 { return 0 }
    if r >= kRows { return 0 }
    if ((doorb[r] >> wrapc(c)) & 1) == 1 { return 1 }
    return 0
}

fn has_dot(c, r) -> 0 {
    if r < 0 { return 0 }
    if r >= kRows { return 0 }
    if ((dots[r] >> wrapc(c)) & 1) == 1 { return 1 }
    return 0
}

fn has_pow(c, r) -> 0 {
    if r < 0 { return 0 }
    if r >= kRows { return 0 }
    if ((powb[r] >> wrapc(c)) & 1) == 1 { return 1 }
    return 0
}

# 主角: 墙和鬼屋门都过不去
fn blocked_player(c, r) -> 0 {
    if is_wall(c, r) == 1 { return 1 }
    if is_door(c, r) == 1 { return 1 }
    return 0
}

# 追兵: 墙过不去; 鬼屋门只有"正在出门"和"眼睛回家"能穿
# 注意: i 是 actor 下标(1..4), 追兵自己的数组是 0-based, 所以一律 i-1
fn blocked_ghost(i, c, r) -> 0 {
    if is_wall(c, r) == 1 { return 1 }
    if is_door(c, r) == 1 {
        if gstate[i - 1] == GH_LEAVE { return 0 }
        if gstate[i - 1] == GH_EATEN { return 0 }
        return 1
    }
    return 0
}

# ================== 追兵的目标格 ==================

fn gtarget_c(i) -> 0 {
    var pc = colof(ax[0])
    var st = gstate[i - 1]
    if st == GH_LEAVE { return 13 }            # 先出门
    if st == GH_EATEN { return 14 }            # 眼睛回家
    if mode == 0 { return gsc_c[i - 1] }       # 巡逻
    if st == GH_SCARED { return pc + rnd(7) - 3 }   # 害怕就乱跑
    if i == 1 { return pc }                    # 直追
    if i == 2 { return pc + ddx(adir[0]) * 4 } # 抄你前路 4 格
    if i == 3 { return pc * 2 - colof(ax[2]) } # 把 2 号的位置反折夹击
    if iabs(pc - colof(ax[4])) + iabs(rowof(ay[0]) - rowof(ay[4])) > 8 { return pc }
    return gsc_c[i - 1]                        # 离得远就回角
}

fn gtarget_r(i) -> 0 {
    var pr = rowof(ay[0])
    var st = gstate[i - 1]
    if st == GH_LEAVE { return 2 }
    if st == GH_EATEN { return 4 }
    if mode == 0 { return gsc_r[i - 1] }
    if st == GH_SCARED { return pr + rnd(7) - 3 }
    if i == 1 { return pr }
    if i == 2 { return pr + ddy(adir[0]) * 4 }
    if i == 3 { return pr * 2 - rowof(ay[2]) }
    if iabs(colof(ax[0]) - colof(ax[4])) + iabs(pr - rowof(ay[4])) > 8 { return pr }
    return gsc_r[i - 1]
}

# ================== 移动 ==================

# 在格心做决策: 主角用玩家给的转向, 追兵用目标格贪心选方向(不能掉头)
fn decide_actor(i) {
    var tc = colof(ax[i])
    var tr = rowof(ay[i])
    if i == 0 {
        if awant[i] != DIR_N {
            if blocked_player(wrapc(tc + ddx(awant[i])), tr + ddy(awant[i])) == 0 {
                adir[i] = awant[i]
                awant[i] = DIR_N
            }
        }
        return
    }
    var tgtc = gtarget_c(i)
    var tgtr = gtarget_r(i)
    var best = DIR_N
    var bestd = 999999
    var d = 0
    while d < 4 {
        if d != revd(adir[i]) {
            var nc = wrapc(tc + ddx(d))
            var nr = tr + ddy(d)
            if blocked_ghost(i, nc, nr) == 0 {
                var nc2 = nc - tgtc
                var nr2 = nr - tgtr
                var dist = nc2 * nc2 + nr2 * nr2
                if dist < bestd {
                    bestd = dist
                    best = d
                }
            }
        }
        d = d + 1
    }
    if best == DIR_N { adir[i] = revd(adir[i]) }
    else { adir[i] = best }
}

# 沿当前方向走; 到格心就重新决策, 但**永不越过下一个格心**
fn step_actor(i) {
    var d = adir[i]
    var tc = colof(ax[i])
    var tr = rowof(ay[i])
    var ccx = itof(gcx(tc))
    var ccy = itof(gcy(tr))
    var along = 0.0
    if d == 0 { along = ax[i] - ccx }
    if d == 1 { along = ccx - ax[i] }
    if d == 2 { along = ccy - ay[i] }
    if d == 3 { along = ay[i] - ccy }

    var step = aspd[i] * dtf()
    if step > 3.0 { step = 3.0 }                      # 掉帧别让角色瞬移

    if fabs(along) < 0.05 {
        # 在格心上: 贴正 + 重新决策(撞墙停下之后格号不变, 所以必须每帧都决策)
        ax[i] = ccx
        ay[i] = ccy
        decide_actor(i)
        d = adir[i]
        if i == 0 {
            if blocked_player(wrapc(tc + ddx(d)), tr + ddy(d)) == 1 { return }
        } else {
            if blocked_ghost(i, wrapc(tc + ddx(d)), tr + ddy(d)) == 1 { return }
        }
        along = 0.0
    }
    # 这一格还能走多远就不越过格心
    var room = 0.0
    if along >= 0.0 { room = itof(kTile) - along } else { room = 0.0 - along }
    if step > room { step = room }
    ax[i] = ax[i] + itof(ddx(d)) * step
    ay[i] = ay[i] + itof(ddy(d)) * step
}

# ================== 局面 ==================

fn place_actors() {
    ax[0] = itof(gcx(14))
    ay[0] = itof(gcy(7))
    adir[0] = DIR_L
    awant[0] = DIR_N
    aspd[0] = spdPlayer
    var i = 1
    while i <= 4 {
        ax[i] = itof(gcx(ghomec[i - 1]))
        ay[i] = itof(gcy(4))
        adir[i] = DIR_U
        awant[i] = DIR_N
        gstate[i - 1] = GH_HOME
        gtimer[i - 1] = 0.0
        i = i + 1
    }
    gstate[0] = GH_LEAVE                 # 1 号一开始就在外面
}

fn set_ghost_speeds() {
    var i = 0
    while i < 4 {
        var s = spdGhost + itof(level - 1) * 3.0
        if gstate[i] == GH_SCARED { s = spdScared }
        if gstate[i] == GH_EATEN { s = spdEaten }
        if rowof(ay[i + 1]) == kTunnelRow {
            if colof(ax[i + 1]) <= 1 || colof(ax[i + 1]) >= kCols - 2 { s = spdTunnel }
        }
        aspd[i + 1] = s
        i = i + 1
    }
}

fn build_maze() {
    # 迷宫字符画 -> 位图(只在开局做一次)
    var r = 0
    while r < kRows {
        wallb[r] = 0
        doorb[r] = 0
        dots[r] = 0
        powb[r] = 0
        var c = 0
        while c < kCols {
            var ch = chr(mrow[r], c)
            if ch == 35 { wallb[r] = wallb[r] | (1 << c) }
            if ch == 45 { doorb[r] = doorb[r] | (1 << c) }
            if ch == 46 { dots[r] = dots[r] | (1 << c) }
            if ch == 111 {
                dots[r] = dots[r] | (1 << c)
                powb[r] = powb[r] | (1 << c)
            }
            c = c + 1
        }
        r = r + 1
    }
    dotsLeft = 0
    r = 0
    while r < kRows {
        c = 0
        while c < kCols {
            if ((dots[r] >> c) & 1) == 1 { dotsLeft = dotsLeft + 1 }
            c = c + 1
        }
        r = r + 1
    }
}

fn start_level(full) {
    build_maze()
    dotsEaten = 0
    fruitShown = 0
    fruitT = 0.0
    frightT = 0.0
    chain = 0
    mode = 0
    modeT = 0.0
    eatPause = 0.0
    place_actors()
    phase = PH_READY
    phaseT = 0.0
    if full { sfx("sfx_level"); bgm("bgm_siren") }
    else { sfx("sfx_ready") }
}

fn lose_life() {
    lives = lives - 1
    if lives <= 0 {
        phase = PH_OVER
        phaseT = 0.0
        sfx("sfx_death")
        return
    }
    place_actors()
    frightT = 0.0
    mode = 0
    modeT = 0.0
    phase = PH_READY
    phaseT = 0.0
    sfx("sfx_death")
}

# ================== 开局 ==================

on start {
    cBg = rgb(8, 10, 20)
    cWall = rgb(22, 38, 86)
    cEdge = rgb(52, 128, 232)
    cDoor = rgb(240, 170, 210)
    cText = rgb(214, 226, 244)
    cDim = rgb(120, 136, 168)
    cAccent = rgb(96, 220, 244)

    # 鬼屋位置 / 巡逻角 / 水果分
    ghomec[0] = 13
    ghomec[1] = 14
    ghomec[2] = 15
    ghomec[3] = 16
    gsc_c[0] = 1
    gsc_r[0] = 1
    gsc_c[1] = 28
    gsc_r[1] = 1
    gsc_c[2] = 28
    gsc_r[2] = 7
    gsc_c[3] = 1
    gsc_r[3] = 7
    grel[0] = 0
    grel[1] = 6
    grel[2] = 20
    grel[3] = 46
    grelw[0] = 0.0
    grelw[1] = 1.2
    grelw[2] = 3.0
    grelw[3] = 5.0
    fruitSc[0] = 100
    fruitSc[1] = 300
    fruitSc[2] = 500

    lives = 3
    score = 0
    level = 1
    extraGiven = 0
    start_level(true)
}

# ================== 每帧 ==================

on update {
    blink = frame() % 20

    # ---- 大力丸时间 ----
    if frightT > 0.0 {
        frightT = frightT - dtf()
        if frightT <= 0.0 {
            frightT = 0.0
            chain = 0
            bgm("bgm_siren")
            i = 0
            while i < 4 {
                if gstate[i] == GH_SCARED { gstate[i] = GH_NORMAL }
                i = i + 1
            }
        }
    }

    # ---- 巡逻/追击交替: 7s 巡逻 <-> 20s 追击 ----
    if phase == PH_PLAY && frightT <= 0.0 {
        modeT = modeT + dtf()
        if mode == 0 && modeT > 7.0 { mode = 1; modeT = 0.0
            i = 0
            while i < 4 { if gstate[i] == GH_NORMAL { adir[i + 1] = revd(adir[i + 1]) } i = i + 1 }
        }
        if mode == 1 && modeT > 20.0 { mode = 0; modeT = 0.0
            i = 0
            while i < 4 { if gstate[i] == GH_NORMAL { adir[i + 1] = revd(adir[i + 1]) } i = i + 1 }
        }
    }

    # ---- 各阶段 ----
    if phase == PH_READY {
        phaseT = phaseT + dtf()
        if phaseT >= tReady { phase = PH_PLAY; phaseT = 0.0 }
    } else if phase == PH_DYING {
        phaseT = phaseT + dtf()
        if phaseT >= tDying { lose_life() }
    } else if phase == PH_CLEAR {
        phaseT = phaseT + dtf()
        if phaseT >= tClear {
            level = level + 1
            start_level(true)
        }
    } else if phase == PH_OVER {
        phaseT = phaseT + dtf()
        if phaseT > 0.6 && pressed(a) {
            lives = 3
            score = 0
            level = 1
            extraGiven = 0
            start_level(true)
        }
    } else if phase == PH_PLAY {
        # ---- 主角 ----
        if held(left) { awant[0] = DIR_L }
        if held(right) { awant[0] = DIR_R }
        if held(up) { awant[0] = DIR_U }
        if held(down) { awant[0] = DIR_D }
        # 在格心上直接允许转向(手感更跟手)
        if awant[0] != DIR_N {
            if fabs(ax[0] - itof(gcx(colof(ax[0])))) < 1.2 &&
               fabs(ay[0] - itof(gcy(rowof(ay[0])))) < 1.2 {
                if blocked_player(wrapc(colof(ax[0]) + ddx(awant[0])),
                                  rowof(ay[0]) + ddy(awant[0])) == 0 {
                    adir[0] = awant[0]
                    awant[0] = DIR_N
                }
            }
        }
        step_actor(0)

        # 吃豆 / 大力丸
        j = colof(ax[0])
        k = rowof(ay[0])
        if has_dot(j, k) == 1 {
            # 这一位已知是 1, 所以直接异或就能清掉
            dots[k] = dots[k] ^ (1 << j)
            dotsLeft = dotsLeft - 1
            dotsEaten = dotsEaten + 1
            if has_pow(j, k) == 1 {
                powb[k] = powb[k] ^ (1 << j)
                score = score + 50
                frightT = 5.5 - itof(level - 1) * 0.5
                if frightT < 2.0 { frightT = 2.0 }
                chain = 0
                i = 0
                while i < 4 {
                    if gstate[i] == GH_NORMAL { gstate[i] = GH_SCARED
                        adir[i + 1] = revd(adir[i + 1]) }
                    i = i + 1
                }
                sfx("sfx_pellet")
                bgm("bgm_siren_hot")
            } else {
                score = score + 10
                chompAlt = 1 - chompAlt
                if chompAlt == 0 { sfx("sfx_chomp0") } else { sfx("sfx_chomp1") }
            }
            # 第 70 / 170 颗出水果
            if dotsEaten == 70 || dotsEaten == 170 {
                if fruitShown == 0 { fruitT = tFruit; fruitShown = 1 }
                else if fruitShown == 1 { fruitT = tFruit; fruitShown = 2 }
            }
            # 加命
            if extraGiven == 0 && score >= 10000 { extraGiven = 1; lives = lives + 1; sfx("sfx_life") }
        }

        # 水果
        if fruitT > 0.0 {
            fruitT = fruitT - dtf()
            if j == 15 && k == 7 {
                j = level
                if j > 3 { j = 3 }
                score = score + fruitSc[j - 1]
                fruitT = 0.0
                sfx("sfx_fruit")
            }
        }

        # ---- 追兵 ----
        i = 0
        while i < 4 {
            # 放出: 吃够豆 或者 等够时间
            if gstate[i] == GH_HOME {
                gtimer[i] = gtimer[i] + dtf()
                if dotsEaten >= grel[i] || gtimer[i] >= grelw[i] { gstate[i] = GH_LEAVE }
            }
            if gstate[i] == GH_LEAVE && rowof(ay[i + 1]) <= 2 { gstate[i] = GH_NORMAL }
            if gstate[i] == GH_EATEN {
                j = colof(ax[i + 1])
                k = rowof(ay[i + 1])
                if k >= 4 && j >= 13 && j <= 16 { gstate[i] = GH_LEAVE }
            }
            i = i + 1
        }
        set_ghost_speeds()
        if eatPause > 0.0 {
            eatPause = eatPause - dtf()
        } else {
            i = 1
            while i <= 4 {
                step_actor(i)
                i = i + 1
            }
        }

        # ---- 碰撞 ----
        i = 0
        while i < 4 {
            if gstate[i] == GH_SCARED || gstate[i] == GH_NORMAL {
                var dx = ax[i + 1] - ax[0]
                var dy = ay[i + 1] - ay[0]
                if fabs(dx) < 9.0 && fabs(dy) < 9.0 {
                    if gstate[i] == GH_SCARED {
                        j = 200
                        if chain > 3 { chain = 3 }
                        k = 0
                        while k < chain { j = j * 2; k = k + 1 }
                        score = score + j
                        chain = chain + 1
                        gstate[i] = GH_EATEN
                        aspd[i + 1] = spdEaten
                        eatPause = 0.45
                        sfx("sfx_eatghost")
                        if chain >= 4 { chain = 3 }
                    } else {
                        phase = PH_DYING
                        phaseT = 0.0
                        sfx("sfx_death")
                    }
                }
            }
            i = i + 1
        }

        # ---- 过关 ----
        if dotsLeft <= 0 {
            phase = PH_CLEAR
            phaseT = 0.0
        }
    }

    if held(select) { exit() }
}

# ================== 绘制 ==================

fn draw_maze() {
    var r = 0
    while r < kRows {
        var c = 0
        while c < kCols {
            var x = kMazeX + c * kTile
            var y = kMazeY + r * kTile
            if is_wall(c, r) == 1 {
                frect(x, y, kTile, kTile, cWall)
                if is_wall(c, r - 1) == 0 { frect(x, y, kTile, 2, cEdge) }
                if is_wall(c, r + 1) == 0 { frect(x, y + kTile - 2, kTile, 2, cEdge) }
            } else if is_door(c, r) == 1 {
                frect(x, y + 6, kTile, 2, cDoor)
            }
            c = c + 1
        }
        r = r + 1
    }
}

fn draw_dots() {
    var r = 0
    while r < kRows {
        var c = 0
        while c < kCols {
            if ((dots[r] >> c) & 1) == 1 {
                if ((powb[r] >> c) & 1) == 1 {
                    # 大力丸: 两帧闪烁, 只在没被吃掉时闪
                    if blink < 12 { img(pellet[0], gcx(c) - 5, gcy(r) - 5) }
                    else { img(pellet[1], gcx(c) - 5, gcy(r) - 5) }
                } else {
                    img("dot", gcx(c) - 3, gcy(r) - 3)
                }
            }
            c = c + 1
        }
        r = r + 1
    }
}

fn draw_actors() {
    # 主角: 帧号 = 方向*2 + 动画帧, 朝左时把"朝右"的图翻过来
    var f = frame() % 2
    if phase == PH_DYING {
        f = 0
    }
    var si = 0
    if adir[0] == DIR_R { si = 0 + f }
    else if adir[0] == DIR_L { si = 2 + f }
    else if adir[0] == DIR_U { si = 4 + f }
    else { si = 6 + f }
    if phase == PH_DYING {
        if ((frame() >> 2) & 1) == 0 { img(munch[si], ftoi(ax[0]) - 6, ftoi(ay[0]) - 6) }
    } else {
        img(munch[si], ftoi(ax[0]) - 6, ftoi(ay[0]) - 6)
    }

    # 追兵
    var i = 1
    while i <= 4 {
        var st = gstate[i - 1]
        var gx = ftoi(ax[i]) - 6
        var gy = ftoi(ay[i]) - 6
        if st == GH_EATEN {
            img(geyes[adir[i]], gx, gy)
        } else if st == GH_SCARED {
            # 大力丸快结束时闪一下, 提示要变回来了
            if frightT > 1.5 || blink < 10 { img(gscared[f], gx, gy) }
        } else {
            img(gani[(i - 1) * 2 + f], gx, gy)
        }
        i = i + 1
    }
}

fn draw_hud() {
    text(4, 4, "SCORE", cDim, 1)
    textnum(46, 4, score, cText, 1)
    text(140, 4, "LEVEL", cDim, 1)
    textnum(188, 4, level, cText, 1)
    text(240, 4, "LIVES", cDim, 1)
    i = 0
    while i < lives && i < 6 {
        img("muncher_r0", 290 + i * 14, 3)
        i = i + 1
    }
}

# 居中横幅: which 0=READY 1=GAME OVER 2=LEVEL CLEAR
fn draw_banner(which) {
    frect(0, kMazeY + 44, 428, 24, cBg)
    frect(0, kMazeY + 44, 428, 1, cEdge)
    frect(0, kMazeY + 67, 428, 1, cEdge)
    if which == 0 { text(178, kMazeY + 51, "READY!", cAccent, 2) }
    if which == 1 { text(160, kMazeY + 51, "GAME OVER", cAccent, 2) }
    if which == 2 { text(148, kMazeY + 51, "LEVEL CLEAR", cAccent, 2) }
}

on render {
    clear(cBg)
    draw_maze()
    draw_dots()
    draw_hud()
    if phase == PH_READY { draw_banner(0) }
    else if phase == PH_OVER { draw_banner(1) }
    else if phase == PH_CLEAR { draw_banner(2) }
    if phase == PH_DYING {
        draw_actors()
        return
    }
    if phase != PH_OVER { draw_actors() }
    # 水果
    if fruitT > 0.0 {
        j = level
        if j > 3 { j = 3 }
        img(fruit[j - 1], gcx(15) - 6, gcy(7) - 6)
    }
}
