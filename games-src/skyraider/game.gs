# SKY RAIDER —— 横版飞行射击(脚本卡带版, 完整)
#
# 玩法: 一路打过去, 每关打够击杀数就出 Boss, 打掉 Boss 过关; 三关通关。
#   · 五种敌机: 直冲(drone) / 蛇形(weaver) / 炮台(turret, 会瞄准你还击) / 快突(mini) / Boss
#   · 火力等级 1~3(单发/双发/三发散射), 吃 power 涨级, 被打掉一级
#   · 导弹: 按 B 发射, 自动朝最近的敌机修正
#   · 激光: 击杀攒槽, 攒满按 C 放, 一条横贯全屏的粗光束
#   · 道具: 生命(心) / 导弹 / 火力, 打敌机有概率掉
#   · 三关, 每关敌机更快更密; Boss 前有警告提示, 过关有结算
#
# 素材尺寸(IMG1/565): 飞船 24x16, 自机弹 12x6, 敌弹 10x4, 敌机 20x14 / 22x18,
#   爆炸 18/30/48, 道具 16x16, Boss 48x36 / 76x56, 星球 72x72,
#   简报框 180x58, 警告板 72x18
#
# 打包: py -3 tools\build_game.py skyraider
# 试跑: py -3 tools\gs_run.py games-bin\skyraider.gbn --frames 1200 --pad right,a --png out.png

# ---- 画面 ----
var kScrW = 428
var kScrH = 142
var kHudH = 14

# ---- 阶段 ----
var PH_IN = 0                 # 关卡开场简报
var PH_FIGHT = 1              # 打小怪, 攒击杀数
var PH_WARN = 2               # Boss 警告
var PH_BOSS = 3               # Boss 战
var PH_CLEAR = 4              # 过关结算
var PH_OVER = 5               # 游戏结束

# ---- 敌机种类 ----
var E_DRONE = 0
var E_WEAVER = 1
var E_TURRET = 2
var E_MINI = 3

# ---- 玩家 ----
var px = 0.0
var py = 0.0
var spdP = 116.0
var hitT = 0.0
var fireT = 0.0
var wlevel = 1                 # 火力等级 1~3
var missT = 0.0                # 导弹冷却
var laserG = 0                 # 激光槽(击杀攒)
var laserT = 0.0               # 激光持续时间
var laserTick = 0.0

# ---- 局面 ----
var phase = 0
var phaseT = 0.0
var stage = 1
var lives = 3
var score = 0
var kills = 0
var needK = 14
var elapsed = 0.0
var spawnT = 0.0
var overT = 0.0

# ---- 自机弹 ----
var kMaxPB = 20
var pbx[20] = 0.0
var pby[20] = 0.0
var pbv[20] = 0.0
var pbdir[20] = 0              # -1/0/1 散射方向
var pblive[20] = 0

# ---- 导弹 ----
var kMaxM = 4
var mx[4] = 0.0
var my[4] = 0.0
var mlive[4] = 0

# ---- 敌机 ----
var kMaxE = 12
var ex[12] = 0.0
var ey[12] = 0.0
var evx[12] = 0.0
var evy[12] = 0.0
var ehp[12] = 0
var ekind[12] = 0
var eph[12] = 0.0              # 蛇形相位
var efire[12] = 0.0            # 炮台开火计时
var elive[12] = 0

# ---- 敌弹 ----
var kMaxEB = 20
var ebx[20] = 0.0
var eby[20] = 0.0
var ebvx[20] = 0.0
var ebvy[20] = 0.0
var eblive[20] = 0

# ---- 爆炸 ----
var kMaxB = 10
var bx[10] = 0.0
var by[10] = 0.0
var bt[10] = 0.0

# ---- 道具 ----
var kMaxP = 4
var pkx[4] = 0.0
var pky[4] = 0.0
var pkv[4] = 0.0
var pkk[4] = 0                 # 0=生命 1=导弹 2=火力
var pklive[4] = 0

# ---- Boss ----
var bossA = 0
var bossX = 0.0
var bossY = 0.0
var bossHP = 0
var bossMax = 0
var bossDir = 1.0
var bossT = 0.0
var bossFireT = 0.0
var bossPhase = 0

# ---- 星空 ----
var kStars = 64
var sx[64] = 0.0
var sy[64] = 0.0
var ss[64] = 0.0

var i = 0
var j = 0
var k = 0
var f = 0

strs ship = { "ship_f0", "ship_f1", "ship_f2" }
strs boom = { "boom_s", "boom_m", "boom_l" }
strs ene = { "ene_drone", "ene_weaver", "ene_turret", "ene_drone" }
strs pick = { "pick_life", "pick_missile", "pick_power" }

# ================== 基础工具 ==================

fn iabs(v) -> 0 {
    if v < 0 { return 0 - v }
    return v
}

fn imin2(a, b) -> 0 {
    if a < b { return a }
    return b
}

fn hit_box(ax0 = 0.0, ay0 = 0.0, aw = 0, ah = 0, bx0 = 0.0, by0 = 0.0, bw = 0, bh = 0) -> 0 {
    if ax0 + itof(aw) < bx0 { return 0 }
    if bx0 + itof(bw) < ax0 { return 0 }
    if ay0 + itof(ah) < by0 { return 0 }
    if by0 + itof(bh) < ay0 { return 0 }
    return 1
}

fn boom_at(x = 0.0, y = 0.0) {
    var n = 0
    while n < kMaxB {
        if bt[n] <= 0.0 {
            bx[n] = x
            by[n] = y
            bt[n] = 0.30
            return
        }
        n = n + 1
    }
}

fn big_boom(x = 0.0, y = 0.0) {          # 一次铺三朵, 看起来更响
    boom_at(x - 18.0, y - 12.0)
    boom_at(x + 14.0, y + 8.0)
    boom_at(x - 4.0, y + 20.0)
}

fn spawn_pickup(x = 0.0, y = 0.0, kind = 0) {
    var n = 0
    while n < kMaxP {
        if pklive[n] == 0 {
            pklive[n] = 1
            pkx[n] = x
            pky[n] = y
            pkv[n] = 34.0
            pkk[n] = kind
            return
        }
        n = n + 1
    }
}

# ================== 关卡难度 ==================

fn stage_rate() -> 0 {                    # 越大越难
    return stage - 1
}

# ================== 敌机 ==================

fn spawn_enemy(kind, x0 = 0.0, y0 = 0.0) {
    var n = 0
    while n < kMaxE {
        if elive[n] == 0 {
            elive[n] = 1
            ekind[n] = kind
            ex[n] = x0
            ey[n] = y0
            evy[n] = 24.0
            if rnd(2) == 0 { evy[n] = -24.0 }
            eph[n] = itof(rnd(64)) * 0.1
            efire[n] = 0.6 + itof(rnd(10)) * 0.1
            if kind == E_DRONE {
                evx[n] = 0.0 - (58.0 + itof(stage_rate()) * 7.0)
                ehp[n] = 1
            } else if kind == E_WEAVER {
                evx[n] = 0.0 - (52.0 + itof(stage_rate()) * 7.0)
                ehp[n] = 1
            } else if kind == E_TURRET {
                evx[n] = 0.0 - (34.0 + itof(stage_rate()) * 5.0)
                ehp[n] = 3
            } else {
                evx[n] = 0.0 - (104.0 + itof(stage_rate()) * 10.0)
                ehp[n] = 1
            }
            return
        }
        n = n + 1
    }
}

fn spawn_wave() {
    # 随关卡混入不同种类: 第 1 关只有直冲和蛇形, 第 2 关加炮台, 第 3 关加快速突击
    var r = rnd(100)
    var y = 24.0 + itof(rnd(96))
    if r < 40 {
        spawn_enemy(E_DRONE, itof(kScrW) + 10.0, y)
    } else if r < 68 {
        spawn_enemy(E_WEAVER, itof(kScrW) + 10.0, y)
    } else if r < 86 {
        if stage >= 2 { spawn_enemy(E_TURRET, itof(kScrW) + 10.0, 30.0 + itof(rnd(80)))
        } else { spawn_enemy(E_DRONE, itof(kScrW) + 10.0, y) }
    } else {
        if stage >= 3 { spawn_enemy(E_MINI, itof(kScrW) + 10.0, y) }
        else { spawn_enemy(E_WEAVER, itof(kScrW) + 10.0, y) }
    }
}

fn enemy_shoot(n) {
    var m = 0
    while m < kMaxEB {
        if eblive[m] == 0 {
            eblive[m] = 1
            ebx[m] = ex[n] - 6.0
            eby[m] = ey[n]
            # 朝玩家方向瞄(只在左半边时才瞄, 否则直接往左打)
            if ebx[m] > 120.0 {
                ebvx[m] = 0.0 - 112.0
                ebvy[m] = (py + 8.0 - eby[m]) * 0.9
                if ebvy[m] > 70.0 { ebvy[m] = 70.0 }
                if ebvy[m] < -70.0 { ebvy[m] = -70.0 }
            } else {
                ebvx[m] = 0.0 - 132.0
                ebvy[m] = 0.0
            }
            return
        }
        m = m + 1
    }
}

fn kill_enemy(n) {
    var kx = ex[n]
    var ky = ey[n]
    var kd = ekind[n]
    elive[n] = 0
    if kd == E_TURRET { big_boom(kx, ky) } else { boom_at(kx - 9.0, ky - 9.0) }
    kills = kills + 1
    if kd == E_TURRET { score = score + 300 } else { score = score + 100 }
    laserG = laserG + 1
    if laserG > 12 { laserG = 12 }
    sfx("sfx_explode")
    # 掉落: 炮台掉得多, 小怪偶尔掉
    k = 22
    if kd == E_TURRET { k = 60 }
    k = rnd(k)
    if k < 7 { spawn_pickup(kx, ky, 0) }
    else if k < 16 { spawn_pickup(kx, ky, 1) }
    else if k < 30 { spawn_pickup(kx, ky, 2) }
}

# ================== 道具 ==================

fn take_pickup(n) {
    pklive[n] = 0
    sfx("sfx_pick")
    if pkk[n] == 0 {
        lives = lives + 1
        sfx("sfx_life")
    } else if pkk[n] == 1 {
        missT = 0.0                       # 立刻给一发导弹的冷却
        if wlevel < 3 { wlevel = wlevel + 1 }
    } else {
        if wlevel < 3 { wlevel = wlevel + 1 }
        score = score + 200
    }
}

# ================== 玩家 ==================

fn player_hit() {
    if hitT > 0.0 { return }
    if wlevel > 1 { wlevel = wlevel - 1 }
    lives = lives - 1
    hitT = 2.0
    big_boom(px, py)
    if lives <= 0 {
        phase = PH_OVER
        phaseT = 0.0
        sfx("sfx_over")
    } else {
        sfx("sfx_player_hit")
    }
}

fn fire_shot(dy = 0) {
    var n = 0
    while n < kMaxPB {
        if pblive[n] == 0 {
            pblive[n] = 1
            pbx[n] = px + 22.0
            pby[n] = py + 6.0
            pbv[n] = itof(dy) * 78.0
            pbdir[n] = dy
            return
        }
        n = n + 1
    }
}

fn fire_volley() {
    fire_shot(0)
    if wlevel >= 2 {
        fire_shot(-1)
        fire_shot(1)
    }
    if wlevel >= 3 {
        fire_shot(-2)
        fire_shot(2)
    }
}

fn fire_missile() {
    var n = 0
    while n < kMaxM {
        if mlive[n] == 0 {
            mlive[n] = 1
            mx[n] = px + 16.0
            my[n] = py + 4.0
            return
        }
        n = n + 1
    }
}

# ================== Boss ==================

fn boss_spawn() {
    bossA = 1
    bossX = itof(kScrW) + 40.0
    bossY = 66.0
    bossMax = 40 + stage * 26
    bossHP = bossMax
    bossDir = 1.0
    bossT = 0.0
    bossFireT = 1.4
    bossPhase = 0
}

fn boss_fire() {
    # 三连散射; 血量低于一半时变成五连
    var n = 0
    var spread = 3
    if bossHP * 2 < bossMax { spread = 5 }
    var s = 0
    while s < spread {
        n = 0
        while n < kMaxEB {
            if eblive[n] == 0 {
                eblive[n] = 1
                ebx[n] = bossX - 10.0
                eby[n] = bossY
                ebvx[n] = 0.0 - 122.0
                ebvy[n] = itof(s) * 34.0 - itof(spread - 1) * 17.0
                if ebvy[n] > 80.0 { ebvy[n] = 80.0 }
                if ebvy[n] < -80.0 { ebvy[n] = -80.0 }
                n = kMaxEB                     # 这一发装好了, 下一个角度
            }
            n = n + 1
        }
        s = s + 1
    }
    sfx("sfx_boss_warn")
}

fn boss_die() {
    bossA = 0
    big_boom(bossX - 20.0, bossY - 14.0)
    big_boom(bossX + 12.0, bossY + 10.0)
    boom_at(bossX, bossY)
    score = score + 5000
    phase = PH_CLEAR
    phaseT = 0.0
    sfx("sfx_explode_big")
}

# ================== 流程 ==================

fn start_stage() {
    needK = 14 + stage * 8
    phase = PH_IN
    phaseT = 0.0
    spawnT = 1.6
    i = 0
    while i < kMaxE { elive[i] = 0; i = i + 1 }
    i = 0
    while i < kMaxEB { eblive[i] = 0; i = i + 1 }
    i = 0
    while i < kMaxP { pklive[i] = 0; i = i + 1 }
    bossA = 0
    bgm("bgm_stage")
    sfx("sfx_ui")
}

fn reset_game() {
    stage = 1
    lives = 3
    score = 0
    kills = 0
    wlevel = 1
    laserG = 0
    laserT = 0.0
    missT = 0.0
    hitT = 2.0
    elapsed = 0.0
    px = 40.0
    py = 63.0
    i = 0
    while i < kMaxPB { pblive[i] = 0; i = i + 1 }
    i = 0
    while i < kMaxM { mlive[i] = 0; i = i + 1 }
    i = 0
    while i < kMaxB { bt[i] = 0.0; i = i + 1 }
    i = 0
    while i < kStars {
        sx[i] = itof(rnd(kScrW))
        sy[i] = 14.0 + itof(rnd(kScrH - 14))
        ss[i] = 18.0 + itof(rnd(3)) * 34.0
        i = i + 1
    }
    start_stage()
}

on start {
    reset_game()
}

# ================== 每帧 ==================

on update {
    # 星空无条件滚, 死了也滚
    i = 0
    while i < kStars {
        sx[i] = sx[i] - ss[i] * dtf()
        if sx[i] < 0.0 {
            sx[i] = itof(kScrW)
            sy[i] = 14.0 + itof(rnd(kScrH - 14))
            ss[i] = 18.0 + itof(rnd(3)) * 34.0
        }
        i = i + 1
    }
    i = 0
    while i < kMaxB {
        if bt[i] > 0.0 { bt[i] = bt[i] - dtf() }
        i = i + 1
    }

    f = frame() % 3

    if phase == PH_OVER {
        phaseT = phaseT + dtf()
        if phaseT > 0.8 && pressed(a) { reset_game() }
        if held(select) { exit() }
        return
    }

    if hitT > 0.0 { hitT = hitT - dtf() }
    if laserT > 0.0 {
        laserT = laserT - dtf()
        if laserT <= 0.0 { laserG = 0 }
    }
    elapsed = elapsed + dtf()

    # ---- 玩家操作(简报/结算期间不控制, 但画面照画) ----
    if phase == PH_FIGHT || phase == PH_BOSS {
        j = 0
        k = 0
        if held(right) { j = 1 }
        if held(left) { j = -1 }
        if held(down) { k = 1 }
        if held(up) { k = -1 }
        px = px + itof(j) * spdP * dtf()
        py = py + itof(k) * spdP * dtf()
        if px < 4.0 { px = 4.0 }
        if px > 160.0 { px = 160.0 }
        if py < itof(kHudH) + 2.0 { py = itof(kHudH) + 2.0 }
        if py > itof(kScrH) - 18.0 { py = itof(kScrH) - 18.0 }

        fireT = fireT - dtf()
        if fireT <= 0.0 {
            fire_volley()
            fireT = 0.14
            sfx("sfx_shoot")
        }
        missT = missT - dtf()
        if missT <= 0.0 && pressed(b) {
            fire_missile()
            missT = 0.7
            sfx("sfx_missile")
        }
        # 激光: 槽满 + 按 C
        if laserG >= 12 && laserT <= 0.0 && pressed(c) {
            laserT = 1.3
            laserTick = 0.0
            sfx("sfx_laser")
        }
    }

    # ---- 自机弹 ----
    i = 0
    while i < kMaxPB {
        if pblive[i] == 1 {
            pbx[i] = pbx[i] + 320.0 * dtf()
            pby[i] = pby[i] + pbv[i] * dtf()
            if pbx[i] > itof(kScrW) { pblive[i] = 0 }
        }
        i = i + 1
    }

    # ---- 导弹: 朝它正前方最近的一架敌机修正 ----
    i = 0
    while i < kMaxM {
        if mlive[i] == 1 {
            mx[i] = mx[i] + 250.0 * dtf()
            # 找目标
            k = -1
            j = 0
            while j < kMaxE {
                if elive[j] == 1 {
                    if ex[j] > mx[i] {
                        if k < 0 || ex[j] < ex[k] { k = j }
                    }
                }
                j = j + 1
            }
            if bossA == 1 {
                if bossX > mx[i] { my[i] = my[i] + (bossY - my[i]) * 3.4 * dtf() }
            } else if k >= 0 {
                my[i] = my[i] + (ey[k] - my[i]) * 3.4 * dtf()
            }
            if mx[i] > itof(kScrW) { mlive[i] = 0 }
        }
        i = i + 1
    }

    # ---- 敌机 ----
    i = 0
    while i < kMaxE {
        if elive[i] == 1 {
            ex[i] = ex[i] + evx[i] * dtf()
            if ekind[i] == E_WEAVER {
                eph[i] = eph[i] + dtf() * 2.6
                ey[i] = ey[i] + (itof(iabs(ftoi(eph[i]) % 20 - 10)) - 5.0) * 26.0 * dtf()
            } else {
                ey[i] = ey[i] + evy[i] * dtf()
            }
            if ey[i] < itof(kHudH) + 10.0 { ey[i] = itof(kHudH) + 10.0; evy[i] = 0.0 - evy[i] }
            if ey[i] > itof(kScrH) - 10.0 { ey[i] = itof(kScrH) - 10.0; evy[i] = 0.0 - evy[i] }
            # 炮台还击
            if ekind[i] == E_TURRET {
                efire[i] = efire[i] - dtf()
                if efire[i] <= 0.0 && ex[i] < itof(kScrW) - 20.0 {
                    enemy_shoot(i)
                    efire[i] = 1.5 - itof(stage_rate()) * 0.15
                    if efire[i] < 0.7 { efire[i] = 0.7 }
                }
            }
            if ex[i] < -24.0 { elive[i] = 0 }
        }
        i = i + 1
    }

    # ---- 敌弹 ----
    i = 0
    while i < kMaxEB {
        if eblive[i] == 1 {
            ebx[i] = ebx[i] + ebvx[i] * dtf()
            eby[i] = eby[i] + ebvy[i] * dtf()
            if ebx[i] < -8.0 || eby[i] < 0.0 || eby[i] > itof(kScrH) { eblive[i] = 0 }
        }
        i = i + 1
    }

    # ---- 道具下落 ----
    i = 0
    while i < kMaxP {
        if pklive[i] == 1 {
            pkx[i] = pkx[i] - pkv[i] * dtf()
            if pkx[i] < -18.0 { pklive[i] = 0 }
        }
        i = i + 1
    }

    # ---- 激光持续伤害 ----
    if laserT > 0.0 {
        laserTick = laserTick - dtf()
        if laserTick <= 0.0 {
            laserTick = 0.07
            i = 0
            while i < kMaxE {
                if elive[i] == 1 {
                    if iabs(ey[i] - (py + 8.0)) < 12.0 && ex[i] > px {
                        ehp[i] = ehp[i] - 1
                        if ehp[i] <= 0 { kill_enemy(i) } else { sfx("sfx_hit") }
                    }
                }
                i = i + 1
            }
            if bossA == 1 {
                if iabs(bossY - (py + 8.0)) < 22.0 {
                    bossHP = bossHP - 1
                    if bossHP <= 0 { boss_die() }
                }
            }
        }
    }

    # ---- 弹打敌机 ----
    i = 0
    while i < kMaxPB {
        if pblive[i] == 1 {
            j = 0
            while j < kMaxE {
                if elive[j] == 1 {
                    if hit_box(pbx[i], pby[i], 12, 6, ex[j] - 10.0, ey[j] - 7.0, 20, 14) == 1 {
                        pblive[i] = 0
                        ehp[j] = ehp[j] - 1
                        if ehp[j] <= 0 { kill_enemy(j) } else { sfx("sfx_hit") }
                        j = kMaxE
                    }
                }
                j = j + 1
            }
            if bossA == 1 && pblive[i] == 1 {
                if hit_box(pbx[i], pby[i], 12, 6, bossX - 38.0, bossY - 28.0, 76, 56) == 1 {
                    pblive[i] = 0
                    bossHP = bossHP - 1
                    sfx("sfx_hit")
                    if bossHP <= 0 { boss_die() }
                }
            }
        }
        i = i + 1
    }

    # ---- 导弹打敌机 ----
    i = 0
    while i < kMaxM {
        if mlive[i] == 1 {
            j = 0
            while j < kMaxE {
                if elive[j] == 1 {
                    if hit_box(mx[i], my[i], 12, 6, ex[j] - 10.0, ey[j] - 7.0, 20, 14) == 1 {
                        mlive[i] = 0
                        kill_enemy(j)
                        j = kMaxE
                    }
                }
                j = j + 1
            }
            if bossA == 1 && mlive[i] == 1 {
                if hit_box(mx[i], my[i], 12, 6, bossX - 38.0, bossY - 28.0, 76, 56) == 1 {
                    mlive[i] = 0
                    bossHP = bossHP - 4
                    big_boom(mx[i], my[i])
                    sfx("sfx_explode")
                    if bossHP <= 0 { boss_die() }
                }
            }
        }
        i = i + 1
    }

    # ---- 撞机 ----
    if phase == PH_FIGHT || phase == PH_BOSS {
        i = 0
        while i < kMaxE {
            if elive[i] == 1 {
                if hit_box(px, py, 22, 14, ex[i] - 10.0, ey[i] - 7.0, 20, 14) == 1 {
                    elive[i] = 0
                    boom_at(ex[i] - 9.0, ey[i] - 9.0)
                    player_hit()
                }
            }
            i = i + 1
        }
        i = 0
        while i < kMaxEB {
            if eblive[i] == 1 {
                if hit_box(px, py, 22, 14, ebx[i], eby[i], 10, 4) == 1 {
                    eblive[i] = 0
                    player_hit()
                }
            }
            i = i + 1
        }
        i = 0
        while i < kMaxP {
            if pklive[i] == 1 {
                if hit_box(px, py, 22, 14, pkx[i], pky[i], 16, 16) == 1 { take_pickup(i) }
            }
            i = i + 1
        }
        if bossA == 1 {
            if hit_box(px, py, 22, 14, bossX - 38.0, bossY - 28.0, 76, 56) == 1 { player_hit() }
        }
    }

    # ================== 阶段机 ==================
    phaseT = phaseT + dtf()

    if phase == PH_IN {
        if phaseT >= 2.2 { phase = PH_FIGHT; phaseT = 0.0 }
    } else if phase == PH_FIGHT {
        spawnT = spawnT - dtf()
        if spawnT <= 0.0 {
            spawn_wave()
            spawnT = 0.95 - itof(stage_rate()) * 0.16
            if spawnT < 0.34 { spawnT = 0.34 }
        }
        if kills >= needK { phase = PH_WARN; phaseT = 0.0; sfx("sfx_boss_warn") }
    } else if phase == PH_WARN {
        if phaseT >= 2.6 { boss_spawn(); phase = PH_BOSS; phaseT = 0.0; bgm("bgm_boss") }
    } else if phase == PH_BOSS {
        if bossA == 1 {
            bossT = bossT + dtf()
            # 先飞进来
            if bossX > itof(kScrW) - 70.0 { bossX = bossX - 60.0 * dtf() }
            else {
                bossY = bossY + bossDir * 34.0 * dtf()
                if bossY < 40.0 { bossY = 40.0; bossDir = 1.0 }
                if bossY > itof(kScrH) - 34.0 { bossY = itof(kScrH) - 34.0; bossDir = -1.0 }
                bossFireT = bossFireT - dtf()
                if bossFireT <= 0.0 {
                    boss_fire()
                    bossFireT = 1.15 - itof(stage_rate()) * 0.12
                    if bossFireT < 0.6 { bossFireT = 0.6 }
                }
            }
        }
    } else if phase == PH_CLEAR {
        if phaseT >= 2.8 {
            stage = stage + 1
            if stage > 3 {
                stage = 3
                phase = PH_OVER
                phaseT = 0.0
            } else {
                start_stage()
            }
        }
    }

    if held(select) { exit() }
}

# ================== 绘制 ==================

fn draw_world() {
    # 星空
    i = 0
    while i < kStars {
        if ss[i] > 84.0 { frect(ftoi(sx[i]), ftoi(sy[i]), 2, 2, rgb(220, 232, 255)) }
        else if ss[i] > 50.0 { px(ftoi(sx[i]), ftoi(sy[i]), rgb(150, 170, 210)) }
        else { px(ftoi(sx[i]), ftoi(sy[i]), rgb(80, 96, 130)) }
        i = i + 1
    }
    # 敌机
    i = 0
    while i < kMaxE {
        if elive[i] == 1 {
            if ekind[i] == E_MINI { img("ene_drone", ftoi(ex[i]) - 10, ftoi(ey[i]) - 7) }
            else { img(ene[ekind[i]], ftoi(ex[i]) - 10, ftoi(ey[i]) - 7) }
        }
        i = i + 1
    }
    # Boss
    if bossA == 1 {
        if bossHP * 2 < bossMax { img("boss_large", ftoi(bossX) - 38, ftoi(bossY) - 28) }
        else { img("boss_small", ftoi(bossX) - 24, ftoi(bossY) - 18) }
    }
    # 道具
    i = 0
    while i < kMaxP {
        if pklive[i] == 1 { img(pick[pkk[i]], ftoi(pkx[i]), ftoi(pky[i])) }
        i = i + 1
    }
    # 自机弹
    i = 0
    while i < kMaxPB {
        if pblive[i] == 1 { img("bul_b", ftoi(pbx[i]), ftoi(pby[i])) }
        i = i + 1
    }
    # 导弹
    i = 0
    while i < kMaxM {
        if mlive[i] == 1 { img("bul_c", ftoi(mx[i]), ftoi(my[i])) }
        i = i + 1
    }
    # 敌弹
    i = 0
    while i < kMaxEB {
        if eblive[i] == 1 { img("bul_a", ftoi(ebx[i]), ftoi(eby[i])) }
        i = i + 1
    }
    # 玩家(受伤闪烁)
    if phase != PH_OVER {
        if hitT <= 0.0 || ((frame() >> 2) & 1) == 0 {
            img(ship[f], ftoi(px), ftoi(py))
        }
    }
    # 激光
    if laserT > 0.0 {
        frect(ftoi(px) + 24, ftoi(py) + 2, 400, 12, rgb(120, 240, 255))
        frect(ftoi(px) + 24, ftoi(py) + 5, 400, 6, rgb(255, 255, 255))
    }
    # 爆炸
    i = 0
    while i < kMaxB {
        if bt[i] > 0.0 {
            if bt[i] > 0.20 { img(boom[0], ftoi(bx[i]), ftoi(by[i])) }
            else if bt[i] > 0.10 { img(boom[1], ftoi(bx[i]) - 6, ftoi(by[i]) - 6) }
            else { img(boom[2], ftoi(bx[i]) - 15, ftoi(by[i]) - 15) }
        }
        i = i + 1
    }
}

fn draw_hud() {
    frect(0, 0, kScrW, kHudH, rgb(10, 14, 28))
    frect(0, kHudH, kScrW, 1, rgb(52, 128, 232))
    text(4, 4, "SCORE", rgb(120, 136, 168), 1)
    textnum(46, 4, score, rgb(214, 226, 244), 1)
    text(120, 4, "ST", rgb(120, 136, 168), 1)
    textnum(140, 4, stage, rgb(150, 200, 255), 1)
    i = 0
    while i < lives && i < 4 {
        img("ship_f0", 170 + i * 22, 1)
        i = i + 1
    }
    # 激光槽
    frect(268, 5, 60, 5, rgb(30, 40, 60))
    if laserG >= 12 {
        frect(268, 5, 60, 5, rgb(120, 240, 255))
        text(332, 4, "LASER C", rgb(120, 240, 255), 1)
    } else {
        frect(268, 5, ftoi(itof(laserG) * 5.0), 5, rgb(60, 140, 200))
    }
    textnum(336, 4, wlevel, rgb(255, 200, 120), 1)
    # Boss 血条
    if bossA == 1 {
        frect(60, kHudH + 2, 308, 4, rgb(60, 20, 30))
        frect(60, kHudH + 2, ftoi(itof(bossHP) * 308.0 / itof(bossMax)), 4, rgb(232, 80, 80))
    }
}

fn draw_banner(which) {
    img("ui_mission_frame", 124, 42)
    if which == 0 {
        text(160, 56, "STAGE", rgb(150, 200, 255), 1)
        textnum(206, 56, stage, rgb(255, 230, 120), 1)
        text(140, 74, "GET READY", rgb(214, 226, 244), 1)
    } else if which == 1 {
        text(150, 60, "WARNING", rgb(255, 90, 90), 2)
        text(158, 76, "BOSS  NEAR", rgb(255, 160, 120), 1)
    } else {
        text(140, 56, "STAGE CLEAR", rgb(120, 255, 150), 1)
        textnum(256, 56, score, rgb(255, 230, 120), 1)
    }
}

on render {
    clear(rgb(4, 6, 18))
    draw_world()
    draw_hud()

    if phase == PH_IN { draw_banner(0) }
    else if phase == PH_WARN { draw_banner(1) }
    else if phase == PH_CLEAR { draw_banner(2) }
    else if phase == PH_OVER {
        frect(0, 46, kScrW, 44, rgb(10, 14, 28))
        frect(0, 46, kScrW, 1, rgb(232, 80, 80))
        frect(0, 89, kScrW, 1, rgb(232, 80, 80))
        text(158, 54, "GAME OVER", rgb(255, 90, 90), 2)
        text(120, 74, "PRESS A", rgb(150, 200, 255), 1)
        text(200, 74, "SCORE", rgb(120, 136, 168), 1)
        textnum(242, 74, score, rgb(255, 230, 120), 1)
    }
}
