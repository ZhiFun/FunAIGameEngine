# ============================================================================
# games-src/fighter/game.gs —— KEY FIGHTER(键盘格斗) 脚本版
#
# 原生 games/fighter/game.cpp 的 1:1 移植:
#   · 1P vs CPU, 三回合两胜, 60 秒倒数
#   · 每回合: ROUND n -> FIGHT! -> 对打 -> K.O./TIME UP -> 结算 -> 下一回合
#   · 站立/蹲下 拳脚, 跳跃攻击, 后退格挡, 击退/硬直/命中停顿
#   · 简化指令: ↓ 之后接前/后, 再按 A/B(不用搓招)
#   · 超必杀攒满 100 气槽才能放
#
# 与原版的差异(都是脚本 VM 的表达能力限制, 不影响玩法):
#   · 没有 sin/cos —— 震屏用帧奇偶交替, 眩晕星用 8 向查表
#   · 没有结构体 —— 双方数据用"下标 0=玩家 / 1=CPU"的平行数组
#   · 没有 sprintf —— 横幅用编号 + 字面量分支
#   · 字符串数组 `spr` 装全部 116 张精灵名, 下标 = 角色*29 + 动作偏移 + 帧号
# ============================================================================

# --- 几何/物理常量 -----------------------------------------------------------
var kScrW = 428
var kScrH = 142
var kHudH = 18
var kFloorY = 126
var kLeft = 26
var kRight = 402
var kSprW = 42
var kSprH = 60
var kHalfW = 21
var kGravity = 640.0
var kJumpVy = -220.0
var kMinD = 34.0
var kFrameSlot = 29

# --- 动作编号(与 pose 偏移表一一对应) ---------------------------------------
var A_IDLE = 0
var A_WALK = 1
var A_CROUCH = 2
var A_JUMP = 3
var A_PUNCH = 4
var A_KICK = 5
var A_CPUNCH = 6
var A_CKICK = 7
var A_JPUNCH = 8
var A_JKICK = 9
var A_THROW = 10
var A_SUPERKB = 11
var A_GUN = 12
var A_HURT = 13
var A_BLOCK = 14
var A_KO = 15
var A_RUSH = 16
var A_UPPER = 17
var A_SPINK = 18
var A_COMBO = 19

# --- 招式编号 -----------------------------------------------------------------
var AK_PUNCH = 0
var AK_KICK = 1
var AK_CPUNCH = 2
var AK_CKICK = 3
var AK_SUPER = 4
var AK_JUMP = 5
var AK_RUSH = 6
var AK_UPPER = 7
var AK_SPINK = 8
var AK_COMBO = 9

# --- 阶段 ---------------------------------------------------------------------
var PH_SELECT = 0
var PH_INTRO = 1
var PH_FIGHT = 2
var PH_KO = 3
var PH_ROUND_END = 4
var PH_MATCH_END = 5

# --- 横幅 ---------------------------------------------------------------------
var BN_NONE = 0
var BN_ROUND = 1
var BN_FIGHT = 2
var BN_KO = 3
var BN_TIMEUP = 4
var BN_YOUWIN = 5
var BN_YOULOSE = 6
var BN_MWIN = 7
var BN_MLOST = 8

# ============================================================================
# 精灵名表: 4 角色 × 29 帧 = 116 张
# 每角色内部顺序: idle2 walk4 crouch1 jump2 punch4 kick4 cpush2 ckick3 hurt2 super3 ko2
# 所以 下标 = 角色*29 + aoff(动作) + 帧号
# ⚠ 帧数必须和卡带里真实存在的图对得上, 否则 img() 拿不到图 -> 人直接不显示
# ============================================================================
strs spr = {
    "keyspr_idle0", "keyspr_idle1",
    "keyspr_walk0", "keyspr_walk1", "keyspr_walk2", "keyspr_walk3",
    "keyspr_crouch0",
    "keyspr_jump0", "keyspr_jump1",
    "keyspr_punch0", "keyspr_punch1", "keyspr_punch2", "keyspr_punch3",
    "keyspr_kick0", "keyspr_kick1", "keyspr_kick2", "keyspr_kick3",
    "keyspr_cpush0", "keyspr_cpush1",
    "keyspr_ckick0", "keyspr_ckick1", "keyspr_ckick2",
    "keyspr_hurt0", "keyspr_hurt1",
    "keyspr_super0", "keyspr_super1", "keyspr_super2",
    "keyspr_ko0", "keyspr_ko1",
    "boxer_idle0", "boxer_idle1",
    "boxer_walk0", "boxer_walk1", "boxer_walk2", "boxer_walk3",
    "boxer_crouch0",
    "boxer_jump0", "boxer_jump1",
    "boxer_punch0", "boxer_punch1", "boxer_punch2", "boxer_punch3",
    "boxer_kick0", "boxer_kick1", "boxer_kick2", "boxer_kick3",
    "boxer_cpush0", "boxer_cpush1",
    "boxer_ckick0", "boxer_ckick1", "boxer_ckick2",
    "boxer_hurt0", "boxer_hurt1",
    "boxer_super0", "boxer_super1", "boxer_super2",
    "boxer_ko0", "boxer_ko1",
    "ninja_idle0", "ninja_idle1",
    "ninja_walk0", "ninja_walk1", "ninja_walk2", "ninja_walk3",
    "ninja_crouch0",
    "ninja_jump0", "ninja_jump1",
    "ninja_punch0", "ninja_punch1", "ninja_punch2", "ninja_punch3",
    "ninja_kick0", "ninja_kick1", "ninja_kick2", "ninja_kick3",
    "ninja_cpush0", "ninja_cpush1",
    "ninja_ckick0", "ninja_ckick1", "ninja_ckick2",
    "ninja_hurt0", "ninja_hurt1",
    "ninja_super0", "ninja_super1", "ninja_super2",
    "ninja_ko0", "ninja_ko1",
    "brute_idle0", "brute_idle1",
    "brute_walk0", "brute_walk1", "brute_walk2", "brute_walk3",
    "brute_crouch0",
    "brute_jump0", "brute_jump1",
    "brute_punch0", "brute_punch1", "brute_punch2", "brute_punch3",
    "brute_kick0", "brute_kick1", "brute_kick2", "brute_kick3",
    "brute_cpush0", "brute_cpush1",
    "brute_ckick0", "brute_ckick1", "brute_ckick2",
    "brute_hurt0", "brute_hurt1",
    "brute_super0", "brute_super1", "brute_super2",
    "brute_ko0", "brute_ko1",
}

# --- 角色属性(4 种) ----------------------------------------------------------
var chp[4] = 0
var cspd[4] = 0.0
var cdmg[4] = 0.0
var cbar[4] = 0
var ckb[4] = 0

# --- 招式表(10 招) -----------------------------------------------------------
var atkT_startup[10] = 0.0
var atkT_active[10] = 0.0
var atkT_recover[10] = 0.0
var atk_dmg[10] = 0
var atk_ox[10] = 0
var atk_oy[10] = 0
var atk_ow[10] = 0
var atk_oh[10] = 0
var atk_kb[10] = 0

# --- 双方状态: 下标 0 = 玩家, 1 = CPU ---------------------------------------
var fDef[2] = 0
var fX[2] = 0.0
var fY[2] = 0.0
var fVy[2] = 0.0
var fAir[2] = 0
var fFace[2] = 1
var fHp[2] = 0
var fHpShow[2] = 0
var fMeter[2] = 0
var fAct[2] = 0
var fActT[2] = 0.0
var fHitDone[2] = 0
var fGuard[2] = 0
var fStun[2] = 0.0
var fBlockT[2] = 0.0
var fAnim[2] = 0.0
var fRounds[2] = 0
var fThrow[2] = 0
var fGunLeft[2] = 0
var fGunT[2] = 0.0
var fCombo[2] = 0
var fDownT[2] = 0.0
var fArmF[2] = 0.0
var fArmB[2] = 0.0
var fDecT[2] = 0.0
var fDec[2] = 0

# --- 飞行道具(键帽) ----------------------------------------------------------
var sAlive[20] = 0
var sX[20] = 0.0
var sY[20] = 0.0
var sVx[20] = 0.0
var sVy[20] = 0.0
var sDmg[20] = 0
var sKind[20] = 0
var sFromP[20] = 1

# --- 特效 ---------------------------------------------------------------------
var eAlive[24] = 0
var eX[24] = 0.0
var eY[24] = 0.0
var eT[24] = 0.0
var eLife[24] = 0.0
var eKind[24] = 0

# --- 眩晕星的 8 向偏移 -------------------------------------------------------
var dxs[8] = 0
var dys[8] = 0
var bob[8] = 0

# --- 全局流程 -----------------------------------------------------------------
var gPhase = 0
var gPhaseT = 0.0
var gTime = 0.0
var gSel = 0
var gRound = 0
var gTimer = 60
var gTimerAcc = 0.0
var gShake = 0.0
var gHitStop = 0.0
var gBanner = 0
var gBannerT = 0.0
var gDt = 0.0
var gOx = 0
var gOy = 0
var gSW = 428
var gSH = 142

# ============================================================================
# 小工具
# ============================================================================
fn iabs(v) {
    if v < 0 { return -v }
    return v
}

fn clampi(v, lo, hi) {
    if v < lo { return lo }
    if v > hi { return hi }
    return v
}

fn clampf(v = 0.0, lo = 0.0, hi = 0.0) {
    if v < lo { return lo }
    if v > hi { return hi }
    return v
}

fn f01() -> 0.0 {
    return itof(rnd(1000)) * 0.001
}

fn rndr(lo, hi) {
    return lo + rnd(hi - lo + 1)
}

# ---------------------------------------------------------------------------
# 动作 -> 在角色 29 帧块里的偏移 / 帧数
# ---------------------------------------------------------------------------
fn aoff(act) {
    if act == A_WALK { return 2 }
    if act == A_CROUCH { return 6 }
    if act == A_JUMP { return 7 }
    if act == A_JPUNCH { return 7 }
    if act == A_JKICK { return 7 }
    if act == A_UPPER { return 7 }
    if act == A_PUNCH { return 9 }
    if act == A_THROW { return 9 }
    if act == A_GUN { return 9 }
    if act == A_RUSH { return 9 }
    if act == A_COMBO { return 9 }
    if act == A_KICK { return 13 }
    if act == A_SPINK { return 13 }
    if act == A_CPUNCH { return 17 }
    if act == A_CKICK { return 19 }
    if act == A_HURT { return 22 }
    if act == A_BLOCK { return 22 }
    if act == A_SUPERKB { return 24 }
    if act == A_KO { return 27 }
    return 0
}

fn anum(act) {
    if act == A_WALK { return 4 }
    if act == A_CROUCH { return 1 }
    if act == A_JPUNCH { return 2 }
    if act == A_JKICK { return 2 }
    if act == A_UPPER { return 2 }
    if act == A_PUNCH { return 4 }
    if act == A_THROW { return 4 }
    if act == A_GUN { return 4 }
    if act == A_RUSH { return 4 }
    if act == A_COMBO { return 4 }
    if act == A_KICK { return 4 }
    if act == A_SPINK { return 4 }
    if act == A_CPUNCH { return 2 }
    if act == A_CKICK { return 3 }
    if act == A_HURT { return 2 }
    if act == A_BLOCK { return 2 }
    if act == A_SUPERKB { return 3 }
    if act == A_KO { return 2 }
    return 2
}

# 动作 -> 招式编号(-1 = 这一招没有伤害判定框)
fn atk_of(act) {
    if act == A_PUNCH { return AK_PUNCH }
    if act == A_KICK { return AK_KICK }
    if act == A_CPUNCH { return AK_CPUNCH }
    if act == A_CKICK { return AK_CKICK }
    if act == A_SUPERKB { return AK_SUPER }
    if act == A_JPUNCH { return AK_JUMP }
    if act == A_JKICK { return AK_JUMP }
    if act == A_RUSH { return AK_RUSH }
    if act == A_UPPER { return AK_UPPER }
    if act == A_SPINK { return AK_SPINK }
    if act == A_COMBO { return AK_COMBO }
    return -1
}

fn busy_act(act) {
    if act == A_PUNCH { return 1 }
    if act == A_KICK { return 1 }
    if act == A_CPUNCH { return 1 }
    if act == A_CKICK { return 1 }
    if act == A_JPUNCH { return 1 }
    if act == A_JKICK { return 1 }
    if act == A_THROW { return 1 }
    if act == A_SUPERKB { return 1 }
    if act == A_GUN { return 1 }
    if act == A_HURT { return 1 }
    if act == A_BLOCK { return 1 }
    if act == A_KO { return 1 }
    if act == A_RUSH { return 1 }
    if act == A_UPPER { return 1 }
    if act == A_SPINK { return 1 }
    if act == A_COMBO { return 1 }
    return 0
}

fn total_of(ai = 0.0) -> 0.0 {
    return atkT_startup[ai] + atkT_active[ai] + atkT_recover[ai]
}

# ---------------------------------------------------------------------------
# 当前该显示第几帧
# ---------------------------------------------------------------------------
fn frame_of(who) {
    var act = fAct[who]
    var n = anum(act)
    if n <= 1 { return 0 }
    if act == A_IDLE { return ftoi(fAnim[who] * 2.4) & 1 }
    if act == A_WALK { return ftoi(fAnim[who] * 9.0) & 3 }
    if act == A_JUMP {
        if fVy[who] > 0.0 { return 1 }
        return 0
    }
    if act == A_KO {
        if fActT[who] < 0.30 { return 0 }
        return 1
    }
    if act == A_HURT {
        if fActT[who] < 0.14 { return 0 }
        return 1
    }
    if act == A_BLOCK { return 0 }
    if act == A_THROW {
        if fActT[who] < 0.12 { return 0 }
        if fActT[who] < 0.24 { return 1 }
        return 2
    }
    if act == A_GUN {
        if fActT[who] < 0.12 { return 0 }
        return 1
    }
    var ai = atk_of(act)
    if ai < 0 { return 0 }
    var su = atkT_startup[ai]
    var ac = atkT_active[ai]
    if fActT[who] < su { return 0 }
    if fActT[who] < su + ac { return 1 }
    var rec = atkT_recover[ai]
    if rec <= 0.0 { rec = 1.0 }
    var rr = (fActT[who] - su - ac) / rec
    var fr = 2 + ftoi(rr * itof(n - 3))
    if fr < 1 { fr = 1 }
    if fr > n - 1 { fr = n - 1 }
    return fr
}

fn spr_index(who) {
    return fDef[who] * kFrameSlot + aoff(fAct[who]) + frame_of(who)
}

# ---------------------------------------------------------------------------
fn set_act(who, act) {
    if (fAct[who] == A_KO) && (act != A_KO) { return }
    fAct[who] = act
    fActT[who] = 0.0
    fHitDone[who] = 0
    if act == A_BLOCK { fBlockT[who] = 0.22 }
    if act == A_COMBO { fCombo[who] = 0 }
    if act == A_UPPER {
        fAir[who] = 1
        fVy[who] = -300.0
    }
}

fn add_fx(fx = 0.0, fy = 0.0, kind = 0, life = 0.26) {
    var i = 0
    while i < 24 {
        if eAlive[i] == 0 {
            eAlive[i] = 1
            eX[i] = fx
            eY[i] = fy
            eT[i] = 0.0
            eLife[i] = life
            eKind[i] = kind
            return
        }
        i = i + 1
    }
}

fn spawn_shot(who, kind) {
    var i = 0
    var slot = -1
    while i < 20 {
        if (sAlive[i] == 0) && (slot < 0) { slot = i }
        i = i + 1
    }
    if slot < 0 { return }
    sAlive[slot] = 1
    sKind[slot] = kind
    if who == 0 { sFromP[slot] = 1 } else { sFromP[slot] = 0 }
    sX[slot] = fX[who] + itof(fFace[who]) * 22.0
    sY[slot] = fY[who] - 44.0
    if kind == 0 {
        sVx[slot] = itof(fFace[who]) * 260.0
        sVy[slot] = 0.0
        sDmg[slot] = 8
        sfx("sfx_throw")
    } else if kind == 1 {
        sVx[slot] = itof(fFace[who]) * 150.0
        sVy[slot] = -180.0
        sDmg[slot] = 11
        sfx("sfx_throw")
    } else {
        sVx[slot] = itof(fFace[who]) * 320.0
        sVy[slot] = (f01() - 0.5) * 36.0
        sDmg[slot] = 5
        sfx("sfx_gun")
    }
}

# ---------------------------------------------------------------------------
# 命中裁定
# ---------------------------------------------------------------------------
fn hurt_h(who) {
    var act = fAct[who]
    if act == A_CROUCH { return 34 }
    if act == A_CPUNCH { return 34 }
    if act == A_CKICK { return 34 }
    return 56
}

fn apply_hit(atk, dfn, dmg, kb, sdir, heavy, stunT = 0.30) {
    var blocked = 0
    if (fGuard[dfn] == 1) && (fAir[dfn] == 0) {
        var aa = fAct[dfn]
        if aa == A_IDLE { blocked = 1 }
        if aa == A_WALK { blocked = 1 }
        if aa == A_CROUCH { blocked = 1 }
        if aa == A_BLOCK { blocked = 1 }
    }
    if blocked == 1 {
        var chip = dmg / 6 + 1
        fHp[dfn] = clampi(fHp[dfn] - chip, 0, chp[fDef[dfn]])
        set_act(dfn, A_BLOCK)
        fX[dfn] = fX[dfn] + itof(sdir) * 7.0
        fMeter[dfn] = clampi(fMeter[dfn] + 3, 0, 100)
        add_fx(fX[dfn] - itof(sdir) * 14.0, fY[dfn] - 42.0, 1, 0.20)
        sfx("sfx_block")
        if gShake < 2.0 { gShake = 2.0 }
        return
    }
    var real = ftoi(itof(dmg) * cdmg[fDef[atk]])
    fHp[dfn] = clampi(fHp[dfn] - real, 0, chp[fDef[dfn]])
    fX[dfn] = clampf(fX[dfn] + itof(sdir) * itof(kb / 26), 26.0, 402.0)
    fFace[dfn] = -sdir
    if fHp[dfn] <= 0 {
        set_act(dfn, A_KO)
        fVy[dfn] = -160.0
        fAir[dfn] = 1
        fStun[dfn] = 99.0
        fGuard[dfn] = 0
        add_fx(fX[dfn], fY[dfn] - 66.0, 2, 1.6)
    } else {
        set_act(dfn, A_HURT)
        fStun[dfn] = stunT
    }
    fMeter[atk] = clampi(fMeter[atk] + real / 2 + 3, 0, 100)
    fMeter[dfn] = clampi(fMeter[dfn] + real / 3 + 2, 0, 100)
    if heavy == 1 { gHitStop = 0.10 } else { gHitStop = 0.055 }
    if heavy == 1 { gShake = 9.0 } else { gShake = 3.0 }
    var lf = 0.24
    if heavy == 1 { lf = 0.34 }
    add_fx(fX[dfn] + itof(fFace[dfn]) * 10.0, fY[dfn] - 46.0, 0, lf)
    if heavy == 1 { sfx("sfx_slam") } else { sfx("sfx_hit") }
}

fn resolve_melee(a, b) {
    var ai = atk_of(fAct[a])
    if ai < 0 { return }
    if (fAct[a] == A_COMBO) && (fHitDone[a] == 1) && (fCombo[a] < 3) {
        var slot = ftoi((fActT[a] - atkT_startup[ai]) / 0.09)
        if slot > fCombo[a] {
            fCombo[a] = slot
            fHitDone[a] = 0
        }
    }
    if fHitDone[a] == 1 { return }
    var su = atkT_startup[ai]
    var ac = atkT_active[ai]
    if fActT[a] < su { return }
    if fActT[a] > su + ac { return }
    var aw = atk_ow[ai]
    var ah = atk_oh[ai]
    if fFace[a] > 0 {
        var ax = ftoi(fX[a]) + atk_ox[ai]
    } else {
        var ax = ftoi(fX[a]) - atk_ox[ai] - aw
    }
    var ay = ftoi(fY[a]) + atk_oy[ai]
    var bh = hurt_h(b)
    var bx = ftoi(fX[b]) - 13
    var by = ftoi(fY[b]) - bh
    if (ax < bx + 26) && (bx < ax + aw) && (ay < by + bh) && (by < ay + ah) {
        fHitDone[a] = 1
        var sdir = 1
        if fX[b] < fX[a] { sdir = -1 }
        var heavy = 0
        if (fAct[a] == A_SUPERKB) || (fAct[a] == A_SPINK) { heavy = 1 }
        var st = 0.30
        if heavy == 1 { st = 0.55 }
        apply_hit(a, b, atk_dmg[ai], atk_kb[ai], sdir, heavy, st)
    }
}

fn shots_hit(i, tg) {
    var bh = hurt_h(tg)
    var sx0 = ftoi(sX[i]) - 5
    var sy0 = ftoi(sY[i]) - 5
    var bx = ftoi(fX[tg]) - 13
    var by = ftoi(fY[tg]) - bh
    if (sx0 < bx + 26) && (bx < sx0 + 10) && (sy0 < by + bh) && (by < sy0 + 10) {
        sAlive[i] = 0
        var sh = 1
        if sFromP[i] == 1 { sh = 0 }
        var sdir = 1
        if sVx[i] < 0.0 { sdir = -1 }
        apply_hit(sh, tg, sDmg[i], 70, sdir, 0, 0.22)
        sfx("sfx_clack")
    }
}

fn update_shots() {
    var i = 0
    while i < 20 {
        if sAlive[i] == 1 {
            if sKind[i] == 1 { sVy[i] = sVy[i] + 430.0 * gDt }
            sX[i] = sX[i] + sVx[i] * gDt
            sY[i] = sY[i] + sVy[i] * gDt
            var gone = 0
            if sX[i] < -24.0 { gone = 1 }
            if sX[i] > 452.0 { gone = 1 }
            if sY[i] > 132.0 { gone = 1 }
            if gone == 1 {
                sAlive[i] = 0
                if sKind[i] == 1 { add_fx(sX[i], 124.0, 0, 0.18) }
            } else {
                var tg = 0
                if sFromP[i] == 1 { tg = 1 }
                shots_hit(i, tg)
            }
        }
        i = i + 1
    }
}

fn tick_bars() {
    var w = 0
    while w < 2 {
        if fHpShow[w] > fHp[w] {
            var stp = (itof(fHpShow[w] - fHp[w]) * 4.5 + 14.0) * gDt
            var ns = fHpShow[w] - ftoi(stp)
            if ns <= fHp[w] { ns = fHp[w] }
            fHpShow[w] = ns
        }
        w = w + 1
    }
}

fn push_apart() {
    var dx = fX[1] - fX[0]
    var adx = fabs(dx)
    if adx >= kMinD { return }
    var push = (kMinD - adx) * 0.5
    var sgn = 1.0
    if dx < 0.0 { sgn = -1.0 }
    fX[0] = clampf(fX[0] - sgn * push, 26.0, 402.0)
    fX[1] = clampf(fX[1] + sgn * push, 26.0, 402.0)
}

# ---------------------------------------------------------------------------
# 逐帧推进(双方共用)
# ---------------------------------------------------------------------------
fn step_fighter(who) {
    fAnim[who] = fAnim[who] + gDt
    fActT[who] = fActT[who] + gDt
    if fStun[who] > 0.0 { fStun[who] = fStun[who] - gDt }
    if fBlockT[who] > 0.0 { fBlockT[who] = fBlockT[who] - gDt }

    if fAir[who] == 1 {
        fVy[who] = fVy[who] + kGravity * gDt
        fY[who] = fY[who] + fVy[who] * gDt
        if fY[who] >= itof(kFloorY) {
            fY[who] = itof(kFloorY)
            fVy[who] = 0.0
            fAir[who] = 0
            if fAct[who] != A_KO { set_act(who, A_IDLE) }
        }
    }

    if fAct[who] == A_GUN {
        fGunT[who] = fGunT[who] - gDt
        if (fGunLeft[who] > 0) && (fGunT[who] <= 0.0) {
            fGunLeft[who] = fGunLeft[who] - 1
            fGunT[who] = 0.055
            spawn_shot(who, 2)
        } else if (fGunLeft[who] <= 0) && (fActT[who] > 0.50) {
            set_act(who, A_IDLE)
        }
    }

    if (fAct[who] == A_THROW) && (fHitDone[who] == 0) && (fActT[who] >= 0.12) {
        fHitDone[who] = 1
        spawn_shot(who, fThrow[who])
    }

    var aa = fAct[who]
    if aa == A_JPUNCH {
        if (fAir[who] == 0) || (fActT[who] > 0.55) { set_act(who, A_JUMP) }
    } else if aa == A_JKICK {
        if (fAir[who] == 0) || (fActT[who] > 0.55) { set_act(who, A_JUMP) }
    } else {
        var ai = atk_of(aa)
        if ai >= 0 {
            if fActT[who] > total_of(ai) {
                if (aa == A_CPUNCH) || (aa == A_CKICK) { set_act(who, A_CROUCH) }
                else { set_act(who, A_IDLE) }
            }
        }
    }

    if fAct[who] == A_HURT {
        if fStun[who] <= 0.0 { set_act(who, A_IDLE) }
    }
    if fAct[who] == A_BLOCK {
        if fBlockT[who] <= 0.0 { set_act(who, A_IDLE) }
    }
}

# ---------------------------------------------------------------------------
# 玩家操作
# ---------------------------------------------------------------------------
fn control_player() {
    var dt = gDt
    if fDownT[0] > 0.0 { fDownT[0] = fDownT[0] - dt }
    if fArmF[0] > 0.0 { fArmF[0] = fArmF[0] - dt }
    if fArmB[0] > 0.0 { fArmB[0] = fArmB[0] - dt }

    if pressed(down) == 1 { fDownT[0] = 0.42 }
    if fDownT[0] > 0.0 {
        var fbtn = left
        if fFace[0] > 0 { fbtn = right }
        if pressed(fbtn) == 1 { fArmF[0] = 0.45 }
        var bbtn = right
        if fFace[0] > 0 { bbtn = left }
        if pressed(bbtn) == 1 { fArmB[0] = 0.45 }
    }

    var bbtn = right
    if fFace[0] > 0 { bbtn = left }
    fGuard[0] = 0
    if held(bbtn) == 1 {
        if fAir[0] == 0 { fGuard[0] = 1 }
    }

    if busy_act(fAct[0]) == 1 { return }

    var pb = 0
    if pressed(a) == 1 { pb = 1 }
    if pressed(c) == 1 { pb = 1 }
    var kbtn = 0
    if pressed(b) == 1 { kbtn = 1 }
    if pressed(d) == 1 { kbtn = 1 }

    if pb == 1 {
        if fArmF[0] > 0.0 {
            fArmF[0] = 0.0
            if ckb[fDef[0]] == 1 {
                fThrow[0] = 0
                set_act(0, A_THROW)
            } else {
                set_act(0, A_RUSH)
            }
        } else if fArmB[0] > 0.0 {
            fArmB[0] = 0.0
            if ckb[fDef[0]] == 1 {
                fThrow[0] = 1
                set_act(0, A_THROW)
            } else {
                set_act(0, A_UPPER)
            }
        } else {
            if fAir[0] == 1 {
                set_act(0, A_JPUNCH)
            } else if held(down) == 1 {
                set_act(0, A_CPUNCH)
            } else {
                set_act(0, A_PUNCH)
            }
        }
    } else if kbtn == 1 {
        if (fArmF[0] > 0.0) && (fMeter[0] >= 100) {
            fArmF[0] = 0.0
            fMeter[0] = 0
            if ckb[fDef[0]] == 1 { set_act(0, A_SUPERKB) } else { set_act(0, A_SPINK) }
        } else if (fArmB[0] > 0.0) && (fMeter[0] >= 100) {
            fArmB[0] = 0.0
            fMeter[0] = 0
            if ckb[fDef[0]] == 1 {
                set_act(0, A_GUN)
                fGunLeft[0] = 14
                fGunT[0] = 0.0
            } else {
                set_act(0, A_COMBO)
            }
        } else {
            if fAir[0] == 1 {
                set_act(0, A_JKICK)
            } else if held(down) == 1 {
                set_act(0, A_CKICK)
            } else {
                set_act(0, A_KICK)
            }
        }
    }

    if busy_act(fAct[0]) == 1 { return }

    if (fAir[0] == 0) && (pressed(up) == 1) {
        set_act(0, A_JUMP)
        fAir[0] = 1
        fVy[0] = kJumpVy
        return
    }
    if (held(down) == 1) && (fAir[0] == 0) {
        set_act(0, A_CROUCH)
        return
    }
    var dir = 0
    if held(left) == 1 { dir = dir - 1 }
    if held(right) == 1 { dir = dir + 1 }
    if (dir != 0) && (fAir[0] == 0) {
        if fAct[0] != A_WALK { set_act(0, A_WALK) }
        fX[0] = clampf(fX[0] + itof(dir) * 72.0 * cspd[fDef[0]] * dt, 26.0, 402.0)
    } else if (dir != 0) && (fAir[0] == 1) {
        fX[0] = clampf(fX[0] + itof(dir) * 54.0 * cspd[fDef[0]] * dt, 26.0, 402.0)
    } else if (fAct[0] == A_WALK) || (fAct[0] == A_CROUCH) {
        set_act(0, A_IDLE)
    }
}

# ---------------------------------------------------------------------------
# CPU
# ---------------------------------------------------------------------------
fn ai_cpu() {
    var w = 1
    if fX[0] >= fX[w] { fFace[w] = 1 } else { fFace[w] = -1 }
    var dist = fabs(fX[0] - fX[w])

    var air_ok = 0
    if fAir[w] == 0 {
        if fAct[w] != A_HURT {
            if fAct[w] != A_KO {
                if fAct[w] != A_BLOCK { air_ok = 1 }
            }
        }
    }
    if air_ok == 1 {
        var p_atk = 0
        if atk_of(fAct[0]) >= 0 {
            if fHitDone[0] == 0 { p_atk = 1 }
        }
        fGuard[w] = 0
        if (p_atk == 1) && (dist < 70.0) {
            if f01() < 0.55 { fGuard[w] = 1 }
        }
    }

    if busy_act(fAct[w]) == 1 { return }

    fDecT[w] = fDecT[w] - gDt
    if fDecT[w] > 0.0 {
        var d = fDec[w]
        if d == 1 {
            if dist > 42.0 {
                set_act(w, A_WALK)
                fX[w] = clampf(fX[w] + itof(fFace[w]) * 70.0 * cspd[fDef[w]] * gDt, 26.0, 402.0)
            }
        } else if d == 2 {
            set_act(w, A_WALK)
            fX[w] = clampf(fX[w] - itof(fFace[w]) * 56.0 * cspd[fDef[w]] * gDt, 26.0, 402.0)
        } else if d == 3 {
            if dist < 60.0 { set_act(w, A_PUNCH) } else { set_act(w, A_KICK) }
        } else if d == 4 {
            if dist < 60.0 { set_act(w, A_KICK) } else { set_act(w, A_PUNCH) }
        } else if d == 5 {
            set_act(w, A_CKICK)
        } else if d == 6 {
            set_act(w, A_CROUCH)
        } else if d == 7 {
            if ckb[fDef[w]] == 1 {
                fThrow[w] = 0
                set_act(w, A_THROW)
            } else {
                set_act(w, A_RUSH)
            }
        } else if d == 8 {
            if ckb[fDef[w]] == 1 {
                fThrow[w] = 1
                set_act(w, A_THROW)
            } else {
                set_act(w, A_UPPER)
            }
        } else if d == 9 {
            if dist < 80.0 {
                fMeter[w] = 0
                if ckb[fDef[w]] == 1 { set_act(w, A_SUPERKB) } else { set_act(w, A_SPINK) }
            } else {
                set_act(w, A_PUNCH)
            }
        } else if d == 10 {
            if dist > 88.0 {
                fMeter[w] = 0
                if ckb[fDef[w]] == 1 {
                    set_act(w, A_GUN)
                    fGunLeft[w] = 14
                    fGunT[w] = 0.0
                } else {
                    set_act(w, A_COMBO)
                }
            } else if ckb[fDef[w]] == 1 {
                fThrow[w] = 0
                set_act(w, A_THROW)
            } else {
                set_act(w, A_RUSH)
            }
        }
        return
    }

    var lvl = 0.55 + 0.14 * itof(gRound)
    fDecT[w] = 0.15 + f01() * 0.20
    var r = f01()
    if fMeter[w] >= 100 {
        if dist < 78.0 { fDec[w] = 9 }
        else if dist > 92.0 { fDec[w] = 10 }
        else { fDec[w] = 1 }
    } else if dist > 150.0 {
        if r < 0.45 { fDec[w] = 1 }
        else if r < 0.78 { fDec[w] = 7 }
        else { fDec[w] = 8 }
    } else if dist > 74.0 {
        if r < 0.45 + 0.20 * lvl { fDec[w] = 1 }
        else if r < 0.82 { fDec[w] = 7 }
        else { fDec[w] = 2 }
    } else if dist > 50.0 {
        if r < 0.38 { fDec[w] = 3 }
        else if r < 0.72 { fDec[w] = 4 }
        else if r < 0.86 { fDec[w] = 5 }
        else { fDec[w] = 2 }
    } else {
        if r < 0.22 + 0.22 * lvl { fDec[w] = 3 }
        else if r < 0.70 { fDec[w] = 4 }
        else if r < 0.86 { fDec[w] = 5 }
        else { fDec[w] = 2 }
    }
}

# ---------------------------------------------------------------------------
# 流程
# ---------------------------------------------------------------------------
fn reset_round() {
    fX[0] = 136.0
    fX[1] = 292.0
    fFace[0] = 1
    fFace[1] = -1
    fY[0] = itof(kFloorY)
    fY[1] = itof(kFloorY)
    fHp[0] = chp[fDef[0]]
    fHp[1] = chp[fDef[1]]
    fHpShow[0] = fHp[0]
    fHpShow[1] = fHp[1]
    fAir[0] = 0
    fAir[1] = 0
    fVy[0] = 0.0
    fVy[1] = 0.0
    fStun[0] = 0.0
    fStun[1] = 0.0
    fGuard[0] = 0
    fGuard[1] = 0
    fGunLeft[0] = 0
    fGunLeft[1] = 0
    fAct[0] = A_IDLE
    fAct[1] = A_IDLE
    fActT[0] = 0.0
    fActT[1] = 0.0
    fHitDone[0] = 0
    fHitDone[1] = 0
    fCombo[0] = 0
    fCombo[1] = 0
    fDecT[0] = 0.0
    fDecT[1] = 0.0
    fDec[0] = 0
    fDec[1] = 0
    fDownT[0] = 0.0
    fArmF[0] = 0.0
    fArmB[0] = 0.0
    var i = 0
    while i < 20 {
        sAlive[i] = 0
        i = i + 1
    }
    gHitStop = 0.0
    gShake = 0.0
}

fn start_fight(pIdx, cIdx) {
    var pm = fMeter[0]
    var cm = fMeter[1]
    fDef[0] = pIdx
    fDef[1] = cIdx
    fMeter[0] = pm
    fMeter[1] = cm
    reset_round()
}

fn begin_round() {
    reset_round()
    gPhase = PH_INTRO
    gPhaseT = 0.0
    gTimer = 60
    gTimerAcc = 0.0
    gBanner = BN_ROUND
    gBannerT = 1.3
    sfx("sfx_round")
}

fn update_select() {
    if (pressed(left) == 1) && (gSel > 0) {
        gSel = gSel - 1
        sfx("sfx_round")
    }
    if (pressed(right) == 1) && (gSel < 3) {
        gSel = gSel + 1
        sfx("sfx_round")
    }
    var pick = 0
    if pressed(a) == 1 { pick = 1 }
    if pressed(b) == 1 { pick = 1 }
    if pressed(up) == 1 { pick = 1 }
    if pick == 1 {
        var rv = (gSel + 1 + rndr(0, 2)) & 3
        fRounds[0] = 0
        fRounds[1] = 0
        fMeter[0] = 0
        fMeter[1] = 0
        gRound = 0
        start_fight(gSel, rv)
        begin_round()
    }
}

fn update_intro() {
    gPhaseT = gPhaseT + gDt
    fAnim[0] = fAnim[0] + gDt
    fAnim[1] = fAnim[1] + gDt
    if (gPhaseT > 1.30) && (gBanner != BN_FIGHT) {
        gBanner = BN_FIGHT
        gBannerT = 0.95
    }
    if gPhaseT > 2.15 {
        gPhase = PH_FIGHT
        gPhaseT = 0.0
    }
}

fn update_fight() {
    if gHitStop > 0.0 {
        gHitStop = gHitStop - gDt
        if gHitStop > 0.0 {
            tick_bars()
            return
        }
    }
    gTimerAcc = gTimerAcc + gDt
    if gTimerAcc >= 1.0 {
        gTimerAcc = gTimerAcc - 1.0
        if gTimer > 0 { gTimer = gTimer - 1 }
    }
    if gTimer <= 0 {
        gPhase = PH_KO
        gPhaseT = 0.0
        gBanner = BN_TIMEUP
        gBannerT = 1.5
        return
    }

    control_player()
    ai_cpu()
    step_fighter(0)
    step_fighter(1)
    push_apart()
    resolve_melee(0, 1)
    resolve_melee(1, 0)
    update_shots()
    tick_bars()

    if (fHp[0] <= 0) || (fHp[1] <= 0) {
        gPhase = PH_KO
        gPhaseT = 0.0
        gShake = 7.0
        gBanner = BN_KO
        gBannerT = 1.5
        sfx("sfx_ko")
    }
}

fn update_ko() {
    gPhaseT = gPhaseT + gDt
    fAnim[0] = fAnim[0] + gDt
    fAnim[1] = fAnim[1] + gDt
    var w = 0
    while w < 2 {
        if fAct[w] == A_KO {
            fVy[w] = fVy[w] + kGravity * 0.55 * gDt
            fY[w] = fY[w] + fVy[w] * gDt
            if fY[w] >= itof(kFloorY) {
                fY[w] = itof(kFloorY)
                fVy[w] = 0.0
                fAir[w] = 0
            }
            fX[w] = clampf(fX[w] - itof(fFace[w]) * 16.0 * gDt, 26.0, 402.0)
        }
        w = w + 1
    }
    update_shots()
    tick_bars()
    if gPhaseT > 1.9 {
        var pWon = 0
        if fHp[1] <= 0 { pWon = 1 }
        else if (fHp[0] > 0) && (fHp[0] > fHp[1]) { pWon = 1 }
        if pWon == 1 { fRounds[0] = fRounds[0] + 1 } else { fRounds[1] = fRounds[1] + 1 }
        gPhase = PH_ROUND_END
        gPhaseT = 0.0
        if pWon == 1 { gBanner = BN_YOUWIN } else { gBanner = BN_YOULOSE }
        gBannerT = 1.6
    }
}

fn update_round_end() {
    gPhaseT = gPhaseT + gDt
    if gPhaseT < 1.7 { return }
    if (fRounds[0] >= 2) || (fRounds[1] >= 2) {
        gPhase = PH_MATCH_END
        gPhaseT = 0.0
        if fRounds[0] >= 2 { gBanner = BN_MWIN } else { gBanner = BN_MLOST }
        gBannerT = 3.0
        sfx("sfx_ko")
        return
    }
    gRound = gRound + 1
    begin_round()
}

fn update_match_end() {
    gPhaseT = gPhaseT + gDt
    fAnim[0] = fAnim[0] + gDt
    fAnim[1] = fAnim[1] + gDt
    if gPhaseT > 1.4 {
        var again = 0
        if pressed(a) == 1 { again = 1 }
        if pressed(b) == 1 { again = 1 }
        if pressed(up) == 1 { again = 1 }
        if again == 1 {
            fRounds[0] = 0
            fRounds[1] = 0
            fMeter[0] = 0
            fMeter[1] = 0
            gRound = 0
            gPhase = PH_SELECT
            gPhaseT = 0.0
            gBanner = BN_NONE
            sfx("sfx_round")
        }
    }
}

# ============================================================================
# 绘制
# ============================================================================
fn draw_stage() {
    var ox = gOx
    var oy = gOy
    if (gSW > 428) || (gSH > 142) {
        frect(0, 0, gSW, gSH, rgb(8, 10, 18))
    }

    # 夜空: 逐行渐变
    var y = 18
    while y < 126 {
        var t = itof(y - 18) * 0.009259259
        var cr = ftoi(12.0 + 132.0 * t * t)
        var cg = ftoi(14.0 + 84.0 * t * t)
        var cb = ftoi(36.0 + 80.0 * t)
        if cr > 255 { cr = 255 }
        if cg > 255 { cg = 255 }
        if cb > 255 { cb = 255 }
        frect(ox, y + oy, 428, 1, rgb(cr, cg, cb))
        y = y + 1
    }

    # 月亮
    var my = 35 + oy
    var i = 0
    while i < 9 {
        var mw = 16 - iabs(4 - i) * 3
        frect(336 + ox - mw / 2, my + i, mw, 1, rgb(238, 234, 202))
        i = i + 1
    }

    # 城市剪影
    i = 0
    while i < 24 {
        var bx = ((i * 29 + 7) % 404) + ox
        var bh = 10 + ((i * 37) % 20)
        var bw = 16 + (i % 3) * 8
        frect(bx, 126 - bh + oy, bw, bh, rgb(22, 24, 48))
        if (i % 3) == 0 {
            var wy = 130 - bh
            while wy < 122 {
                frect(bx + 3, wy + oy, 2, 2, rgb(238, 208, 120))
                frect(bx + 9, wy + oy, 2, 2, rgb(238, 208, 120))
                wy = wy + 6
            }
        }
        i = i + 1
    }

    # 擂台
    frect(ox, 126 + oy, 428, 16, rgb(72, 50, 42))
    frect(ox, 126 + oy, 428, 2, rgb(132, 96, 66))
    y = 132
    while y < 142 {
        frect(ox, y + oy, 428, 1, rgb(52, 36, 32))
        y = y + 5
    }
    var x2 = 0
    while x2 < 428 {
        frect(x2 + ox, 128 + oy, 1, 16, rgb(52, 36, 32))
        x2 = x2 + 26
    }
}

fn label_w(idx) {
    if idx == 0 { return 60 }
    if idx == 1 { return 54 }
    if idx == 2 { return 36 }
    return 42
}

fn label_draw(idx, x, y, hot) {
    if hot == 1 {
        if idx == 0 { text(x, y, "KEY SPRITE", rgb(255, 240, 140), 1) }
        else if idx == 1 { text(x, y, "IRON FIST", rgb(255, 240, 140), 1) }
        else if idx == 2 { text(x, y, "SHADOW", rgb(255, 240, 140), 1) }
        else { text(x, y, "BULWARK", rgb(255, 240, 140), 1) }
    } else {
        if idx == 0 { text(x, y, "KEY SPRITE", rgb(176, 190, 210), 1) }
        else if idx == 1 { text(x, y, "IRON FIST", rgb(176, 190, 210), 1) }
        else if idx == 2 { text(x, y, "SHADOW", rgb(176, 190, 210), 1) }
        else { text(x, y, "BULWARK", rgb(176, 190, 210), 1) }
    }
}

fn draw_one(who) {
    var ox = gOx
    var oy = gOy
    if fAir[who] == 0 {
        img("shadow", ftoi(fX[who]) + ox - imgw("shadow") / 2,
            128 + oy - imgh("shadow") / 2)
    }
    var fl = 0
    if fFace[who] < 0 { fl = 1 }
    img(spr[spr_index(who)], ftoi(fX[who]) + ox - kHalfW, ftoi(fY[who]) + oy - kSprH, fl)

    if fAct[who] == A_SUPERKB {
        var wy = fY[who] - 64.0
        if fActT[who] > atkT_startup[AK_SUPER] {
            var kk = (fActT[who] - atkT_startup[AK_SUPER]) / atkT_active[AK_SUPER]
            wy = wy + kk * 42.0
        }
        img("big_kb", ftoi(fX[who]) + fFace[who] * 4 + ox - imgw("big_kb") / 2,
            ftoi(wy) + oy - imgh("big_kb"), fl)
    }
    if fAct[who] == A_GUN {
        img("gun", ftoi(fX[who]) + fFace[who] * 18 + ox - imgw("gun") / 2,
            ftoi(fY[who]) - 42 + oy - imgh("gun"), fl)
    }
    if fAct[who] == A_KO {
        var i = 0
        while i < 3 {
            var a = (ftoi(fAnim[who] * 5.0) + i * 3) & 7
            img("dizzy",
                ftoi(fX[who]) + dxs[a] + ox - imgw("dizzy") / 2,
                ftoi(fY[who]) - 66 + dys[a] + oy - imgh("dizzy") / 2)
            i = i + 1
        }
    }
}

fn draw_prop(kind, cx, cy) {
    if kind == 0 {
        img("cap_small", cx - imgw("cap_small") / 2, cy - imgh("cap_small") / 2)
    } else if kind == 1 {
        img("cap_big", cx - imgw("cap_big") / 2, cy - imgh("cap_big") / 2)
    } else {
        img("cap_tiny", cx - imgw("cap_tiny") / 2, cy - imgh("cap_tiny") / 2)
    }
}

fn draw_fx_sprite(kind, cx, cy) {
    if kind == 0 {
        img("spark", cx - imgw("spark") / 2, cy - imgh("spark") / 2)
    } else if kind == 1 {
        img("ring", cx - imgw("ring") / 2, cy - imgh("ring") / 2)
    } else {
        img("dizzy", cx - imgw("dizzy") / 2, cy - imgh("dizzy") / 2)
    }
}

fn draw_world() {
    var i = 0
    while i < 20 {
        if sAlive[i] == 1 {
            draw_prop(sKind[i], ftoi(sX[i]) + gOx, ftoi(sY[i]) + gOy)
        }
        i = i + 1
    }
    if fY[0] <= fY[1] {
        draw_one(1)
        draw_one(0)
    } else {
        draw_one(0)
        draw_one(1)
    }
    i = 0
    while i < 24 {
        if eAlive[i] == 1 {
            var n = 1 + ftoi((1.0 - eT[i] / eLife[i]) * 1.6)
            var j = 0
            while j < n {
                draw_fx_sprite(eKind[i], ftoi(eX[i]) + gOx, ftoi(eY[i]) + gOy)
                j = j + 1
            }
        }
        i = i + 1
    }
}

fn draw_bar(who, x, y, w, rtl) {
    var full = w - 2
    var hot = 0
    if fMeter[who] >= 100 { hot = 1 }
    if rtl == 1 {
        label_draw(fDef[who], x + w - label_w(fDef[who]), y - 5, hot)
    } else {
        label_draw(fDef[who], x, y - 5, hot)
    }

    frect(x - 1, y - 1, w + 2, 12, rgb(68, 78, 108))
    frect(x, y, w, 10, rgb(30, 22, 24))
    var n_show = full * fHpShow[who] / chp[fDef[who]]
    var n_hp = full * fHp[who] / chp[fDef[who]]
    var bx = x + 1
    if rtl == 1 { bx = x + 1 + full - n_show }
    frect(bx, y + 1, n_show, 8, rgb(250, 214, 92))
    var hx = x + 1
    if rtl == 1 { hx = x + 1 + full - n_hp }
    frect(hx, y + 1, n_hp, 8, cbar[fDef[who]])
    frect(x + 1, y + 9, full, 1, rgb(20, 16, 18))
    var mw = full * fMeter[who] / 100
    var mx = x + 1
    if rtl == 1 { mx = x + 1 + full - mw }
    frect(x + 1, y + 11, full, 3, rgb(26, 34, 50))
    if hot == 1 {
        frect(mx, y + 11, mw, 3, rgb(255, 240, 140))
    } else {
        frect(mx, y + 11, mw, 3, rgb(88, 148, 200))
    }
}

fn draw_hud() {
    frect(0, 0, 428, 18, rgb(10, 12, 20))
    frect(0, 17, 428, 1, rgb(60, 74, 108))
    draw_bar(0, 6, 6, 152, 0)
    draw_bar(1, 270, 6, 152, 1)

    if gTimer <= 10 {
        if gTimer >= 10 { textnum(202, 2, gTimer, rgb(255, 96, 96), 2) }
        else { textnum(208, 2, gTimer, rgb(255, 96, 96), 2) }
    } else {
        textnum(202, 2, gTimer, rgb(255, 255, 255), 2)
    }

    if fRounds[0] > 0 { frect(190, 13, 6, 4, rgb(250, 214, 92)) }
    if fRounds[0] > 1 { frect(198, 13, 6, 4, rgb(250, 214, 92)) }
    if fRounds[1] > 0 { frect(226, 13, 6, 4, rgb(250, 214, 92)) }
    if fRounds[1] > 1 { frect(234, 13, 6, 4, rgb(250, 214, 92)) }
}

fn draw_select() {
    var i = 0
    while i < 4 {
        var cx = 58 + i * 104
        var sel = 0
        if i == gSel { sel = 1 }
        var bobv = 0
        var feet = 128
        var si = i * kFrameSlot
        if sel == 1 {
            var bobv = bob[(frame() >> 3) & 7]
            var feet = 114 + bobv
            var si = i * kFrameSlot + 24 + 1
        }
        img(spr[si], cx - kHalfW, feet - kSprH, 0)
        var nw = label_w(i)
        if sel == 1 {
            label_draw(i, cx - nw / 2, 133, 1)
            frect(cx - 26, 52, 52, 1, rgb(120, 220, 255))
            frect(cx - 26, 128, 52, 1, rgb(120, 220, 255))
        } else {
            label_draw(i, cx - nw / 2, 133, 0)
        }
        i = i + 1
    }
    text(124, 4, "LEFT/RIGHT  SELECT    A/B  FIGHT", rgb(150, 164, 190), 1)
    text(115, 125, "DOWN+FWD/BACK + A/B: SPECIAL", rgb(120, 134, 160), 1)
}

fn draw_banner() {
    if gBanner == BN_NONE { return }
    if gBannerT <= 0.0 { return }
    var y = 57
    frect(0, y, 428, 28, rgb(18, 12, 20))
    frect(0, y, 428, 1, rgb(240, 200, 90))
    frect(0, y + 27, 428, 1, rgb(240, 200, 90))
    if gBanner == BN_ROUND {
        text(160, y + 4, "ROUND", rgb(255, 236, 150), 3)
        textnum(250, y + 4, gRound + 1, rgb(255, 236, 150), 3)
    } else if gBanner == BN_FIGHT {
        text(160, y + 4, "FIGHT!", rgb(255, 236, 150), 3)
    } else if gBanner == BN_KO {
        text(178, y + 4, "K.O.", rgb(255, 236, 150), 3)
    } else if gBanner == BN_TIMEUP {
        text(151, y + 4, "TIME UP", rgb(255, 236, 150), 3)
    } else if gBanner == BN_YOUWIN {
        text(151, y + 4, "YOU WIN", rgb(255, 236, 150), 3)
    } else if gBanner == BN_YOULOSE {
        text(142, y + 4, "YOU LOSE", rgb(255, 236, 150), 3)
    } else if gBanner == BN_MWIN {
        text(133, y + 4, "MATCH WIN", rgb(255, 236, 150), 3)
    } else {
        text(124, y + 4, "MATCH LOST", rgb(255, 236, 150), 3)
    }
}

# ============================================================================
# 表格初始化
# ============================================================================
fn fill_tables() {
    chp[0] = 100
    chp[1] = 105
    chp[2] = 92
    chp[3] = 122
    cspd[0] = 1.00
    cspd[1] = 1.06
    cspd[2] = 1.26
    cspd[3] = 0.80
    cdmg[0] = 1.00
    cdmg[1] = 1.12
    cdmg[2] = 0.92
    cdmg[3] = 1.26
    cbar[0] = rgb(56, 214, 236)
    cbar[1] = rgb(240, 104, 96)
    cbar[2] = rgb(150, 168, 255)
    cbar[3] = rgb(150, 200, 120)
    ckb[0] = 1
    ckb[1] = 0
    ckb[2] = 0
    ckb[3] = 0

    # 拳 / 踢 / 蹲拳 / 蹲踢 / 巨键砸 / 跳击 / 冲拳 / 升龙 / 回旋踢 / 百裂
    atkT_startup[0] = 0.07
    atkT_startup[1] = 0.11
    atkT_startup[2] = 0.07
    atkT_startup[3] = 0.10
    atkT_startup[4] = 0.26
    atkT_startup[5] = 0.06
    atkT_startup[6] = 0.09
    atkT_startup[7] = 0.08
    atkT_startup[8] = 0.13
    atkT_startup[9] = 0.06

    atkT_active[0] = 0.06
    atkT_active[1] = 0.08
    atkT_active[2] = 0.06
    atkT_active[3] = 0.09
    atkT_active[4] = 0.14
    atkT_active[5] = 0.20
    atkT_active[6] = 0.07
    atkT_active[7] = 0.10
    atkT_active[8] = 0.09
    atkT_active[9] = 0.27

    atkT_recover[0] = 0.13
    atkT_recover[1] = 0.20
    atkT_recover[2] = 0.14
    atkT_recover[3] = 0.22
    atkT_recover[4] = 0.34
    atkT_recover[5] = 0.10
    atkT_recover[6] = 0.20
    atkT_recover[7] = 0.24
    atkT_recover[8] = 0.26
    atkT_recover[9] = 0.20

    atk_dmg[0] = 6
    atk_dmg[1] = 11
    atk_dmg[2] = 5
    atk_dmg[3] = 9
    atk_dmg[4] = 30
    atk_dmg[5] = 9
    atk_dmg[6] = 13
    atk_dmg[7] = 16
    atk_dmg[8] = 17
    atk_dmg[9] = 6

    atk_ox[0] = 14
    atk_ox[1] = 14
    atk_ox[2] = 12
    atk_ox[3] = 12
    atk_ox[4] = 10
    atk_ox[5] = 10
    atk_ox[6] = 16
    atk_ox[7] = 10
    atk_ox[8] = 16
    atk_ox[9] = 20

    atk_oy[0] = -46
    atk_oy[1] = -34
    atk_oy[2] = -30
    atk_oy[3] = -18
    atk_oy[4] = -66
    atk_oy[5] = -44
    atk_oy[6] = -44
    atk_oy[7] = -74
    atk_oy[8] = -40
    atk_oy[9] = -44

    atk_ow[0] = 26
    atk_ow[1] = 30
    atk_ow[2] = 24
    atk_ow[3] = 28
    atk_ow[4] = 56
    atk_ow[5] = 28
    atk_ow[6] = 30
    atk_ow[7] = 26
    atk_ow[8] = 34
    atk_ow[9] = 24

    atk_oh[0] = 14
    atk_oh[1] = 15
    atk_oh[2] = 13
    atk_oh[3] = 13
    atk_oh[4] = 56
    atk_oh[5] = 15
    atk_oh[6] = 15
    atk_oh[7] = 54
    atk_oh[8] = 16
    atk_oh[9] = 14

    atk_kb[0] = 90
    atk_kb[1] = 150
    atk_kb[2] = 70
    atk_kb[3] = 130
    atk_kb[4] = 300
    atk_kb[5] = 110
    atk_kb[6] = 130
    atk_kb[7] = 80
    atk_kb[8] = 200
    atk_kb[9] = 45

    # 眩晕星 8 向
    dxs[0] = 14
    dxs[1] = 10
    dxs[2] = 0
    dxs[3] = -10
    dxs[4] = -14
    dxs[5] = -10
    dxs[6] = 0
    dxs[7] = 10
    dys[0] = 0
    dys[1] = -10
    dys[2] = -14
    dys[3] = -10
    dys[4] = 0
    dys[5] = 10
    dys[6] = 14
    dys[7] = 10

    bob[0] = 0
    bob[1] = 1
    bob[2] = 2
    bob[3] = 1
    bob[4] = 0
    bob[5] = -1
    bob[6] = -2
    bob[7] = -1
}

# ============================================================================
# 主循环
# ============================================================================
on start {
    gSW = screen_w()
    gSH = screen_h()
    fill_tables()
    gTime = 0.0
    gSel = 0
    gRound = 0
    gShake = 0.0
    gHitStop = 0.0
    gBanner = BN_NONE
    gBannerT = 0.0
    gPhase = PH_SELECT
    gPhaseT = 0.0
    gTimer = 60
    gTimerAcc = 0.0
    gOx = 0
    gOy = 0
    fRounds[0] = 0
    fRounds[1] = 0
    fMeter[0] = 0
    fMeter[1] = 0
    start_fight(0, 1)
}

on update {
    gDt = dtf()
    if gDt > 0.08 { gDt = 0.08 }
    gTime = gTime + gDt
    if gShake > 0.0 {
        if gShake > gDt * 40.0 { gShake = gShake - gDt * 40.0 } else { gShake = 0.0 }
    }
    if gBannerT > 0.0 { gBannerT = gBannerT - gDt }

    var i = 0
    while i < 24 {
        if eAlive[i] == 1 {
            eT[i] = eT[i] + gDt
            if eKind[i] == 2 { eY[i] = eY[i] - 6.0 * gDt }
            if eT[i] >= eLife[i] { eAlive[i] = 0 }
        }
        i = i + 1
    }

    if gPhase == PH_SELECT { update_select() }
    else if gPhase == PH_INTRO { update_intro() }
    else if gPhase == PH_FIGHT { update_fight() }
    else if gPhase == PH_KO { update_ko() }
    else if gPhase == PH_ROUND_END { update_round_end() }
    else if gPhase == PH_MATCH_END { update_match_end() }
}

on render {
    clear(rgb(8, 10, 18))
    gOx = 0
    gOy = 0
    if gShake > 0.0 {
        var p = frame() & 3
        if p == 0 { gOx = 3 }
        else if p == 1 { gOx = -3 }
        else if p == 2 { gOy = 2 }
        else { gOy = -2 }
    }
    draw_stage()
    if gPhase == PH_SELECT {
        draw_select()
        draw_banner()
        return
    }
    draw_world()
    draw_hud()
    draw_banner()
}
