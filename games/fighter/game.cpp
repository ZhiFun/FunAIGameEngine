// ============================================================================
// games/fighter/game.cpp —— KEY FIGHTER(键盘格斗)
//
// 参照 FC「12人街霸」的做法与机制:
//   · 1P(玩家) vs CPU, 三回合两胜, 60 秒倒数
//   · 每回合: ROUND n -> FIGHT! -> 对打 -> K.O./TIME UP -> 结算 -> 下一回合
//   · 招式: 站立/蹲下 拳脚, 跳跃攻击, 后退格挡(只掉一点点血), 击退/硬直/命中停顿
//   · 简化指令(FC 版街霸本来就简化过): ↓ 后接前/后, 再按 A/B, 不用搓半圆
//   · 超必杀要攒气槽(打人涨得多, 挨打也涨一点), 满槽才放得出
//
// 角色(素材见 tools/gen_fighter.py):
//   KEY SPRITE 键盘精灵 —— 键盘固件主题三那只小键盘 + 卡通手脚
//   IRON FIST  铁拳小子 / SHADOW 影忍 / BULWARK 重装
//
// 键盘精灵的招式(用户指定):
//   小招  徒手扔键帽: ↓前+A 平抛(直飞) / ↓后+A 高抛(抛物线)
//   大招  巨型键盘砸人: ↓前+B        / 按键帽机关枪: ↓后+B
//
// 素材全部来自卡带 fighter.gbn, 精灵一律用品红键控(kColorMagenta)。
// 素材只画了朝右的姿势, 朝左那一侧在贴图时逐像素镜像 —— 省一半卡带体积。
// ============================================================================
#include "game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/Engine.h"

namespace games {
namespace {

using engine::Button;
using engine::Color;
using engine::Display;
using engine::Engine;
using engine::Image;
using engine::PadState;
using engine::is_held;
using engine::is_pressed;
using engine::rgb565;

constexpr Color kKey   = engine::kColorMagenta;      // 透明色
constexpr Color kInk   = rgb565(10, 12, 20);
constexpr Color kWhite = rgb565(255, 255, 255);

constexpr int kScrW   = 428;
constexpr int kScrH   = 142;
constexpr int kHudH   = 18;
constexpr int kFloorY = 126;                          // 脚底线
constexpr float kGravity = 640.0f;                    // 像素/秒^2
constexpr float kJumpVy  = -220.0f;

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
struct Rnd {
    uint32_t s = 0x13579BDFu;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int range(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
    float f01() { return (next() >> 8) / 16777216.0f; }
};
Rnd g_rnd;

template <typename T> T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Box { int x, y, w, h; };
inline bool overlap(const Box& a, const Box& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// 攻击帧数据: 起始/判定/收招(秒) + 伤害 + 判定框(相对脚底) + 击退力度
struct Atk {
    float startup, active, recover;
    int   dmg;
    int   ox, oy, ow, oh;      // ox 为向前偏移, oy 为相对脚底的纵向偏移(负=向上)
    int   kb;
};
constexpr Atk kPunch       = {0.07f, 0.06f, 0.13f,  6, 14, -46, 26, 14,  90};
constexpr Atk kKick        = {0.11f, 0.08f, 0.20f, 11, 14, -34, 30, 15, 150};
constexpr Atk kCrouchPunch = {0.07f, 0.06f, 0.14f,  5, 12, -30, 24, 13,  70};
constexpr Atk kCrouchKick  = {0.10f, 0.09f, 0.22f,  9, 12, -18, 28, 13, 130};
constexpr Atk kSuperKb     = {0.26f, 0.14f, 0.34f, 30, 10, -66, 56, 56, 300};
constexpr Atk kJumpAtk     = {0.06f, 0.20f, 0.10f,  9, 10, -44, 28, 15, 110};

// ---- 非键盘角色的四个专属必杀(徒手) ----------------------------------------
// 键盘系道具(键帽/巨键/机关枪)是键盘精灵的专利, 其他角色用同一套指令但换成
// 拳脚招 —— 复用现有的拳/踢姿势帧, 不额外占卡带体积。
constexpr Atk kRush  = {0.09f, 0.07f, 0.20f, 13, 16, -44, 30, 15, 130};  // 冲拳
constexpr Atk kUpper = {0.08f, 0.10f, 0.24f, 16, 10, -74, 26, 54,  80};  // 升龙拳(坐竖判定)
constexpr Atk kSpink = {0.13f, 0.09f, 0.26f, 17, 16, -40, 34, 16, 200};  // 回旋踢
constexpr Atk kCombo = {0.06f, 0.27f, 0.20f,  6, 20, -44, 24, 14,  45};  // 百裂连打(3 下)

enum Act {
    A_IDLE, A_WALK, A_CROUCH, A_JUMP, A_PUNCH, A_KICK, A_CPUNCH, A_CKICK,
    A_JPUNCH, A_JKICK, A_THROW, A_SUPERKB, A_GUN, A_HURT, A_BLOCK, A_KO,
    A_RUSH, A_UPPER, A_SPINK, A_COMBO,
};

struct Shot {
    bool  alive = false;
    float x = 0, y = 0, vx = 0, vy = 0;
    int   dmg = 0, kind = 0;         // 0=平抛 1=高抛 2=机枪
    bool  fromPlayer = true;
};

struct Fx {
    bool  alive = false;
    float x = 0, y = 0, t = 0, life = 0.28f;
    int   kind = 0;                  // 0=火花 1=波纹 2=眩晕星
};

struct CharDef {
    const char* id;                  // 素材前缀
    const char* label;
    int   hp;
    float speed;
    float dmg;
    Color bar;
    bool  kb;                        // 键盘精灵: 独占"键盘系"招式(扔键帽/巨键砸/键帽机枪)
};
const CharDef kChars[] = {
    {"keyspr", "KEY SPRITE", 100, 1.00f, 1.00f, rgb565( 56, 214, 236), true},
    {"boxer",  "IRON FIST",  105, 1.06f, 1.12f, rgb565(240, 104,  96), false},
    {"ninja",  "SHADOW",      92, 1.26f, 0.92f, rgb565(150, 168, 255), false},
    {"brute",  "BULWARK",    122, 0.80f, 1.26f, rgb565(150, 200, 120), false},
};
constexpr int kCharCount = (int)(sizeof(kChars) / sizeof(kChars[0]));

struct Fighter {
    const CharDef* def = nullptr;
    float x = 0, y = 0;              // 脚底中心
    float vy = 0;
    bool  air = false;
    int   face = 1;                  // +1 朝右, -1 朝左
    int   hp = 0, hpShow = 0;        // hpShow 是血条残影(慢慢追上去)
    int   meter = 0;                 // 0..100 气槽
    int   act = A_IDLE;
    float actT = 0;
    bool  hitDone = false;
    bool  guard = false;             // 是否在"拉后"格挡
    float stun = 0, blockT = 0;
    float animT = 0;
    bool  cpu = false;
    int   rounds = 0;
    int   throwKind = 0;
    int   gunLeft = 0;
    float gunT = 0;
    int   comboSlot = 0;                   // 连打已经打到第几下
    float downT = 0, armF = 0, armB = 0;   // 简化指令缓冲
    float decT = 0;                        // CPU 决策计时
    int   dec = 0;                         // CPU 当前决定
};

// ---------------------------------------------------------------------------
class FighterGame : public engine::Game {
public:
    const char* name() const override { return "fighter"; }

    void on_start(Engine& e) override {
        cache_assets(e);
        g_rnd.s = 0x2468ACE0u;
        m_time = 0;
        m_sel = 0;
        m_round = 0;
        m_shake = 0;
        m_hitStop = 0;
        m_banner[0] = 0;
        m_bannerT = 0;
        m_phase = PH_SELECT;
        m_phaseT = 0;
        m_player.rounds = m_cpu.rounds = 0;
        m_player.meter = m_cpu.meter = 0;
        // 先随便配一组, 选人界面只用到素材
        start_fight(e, 0, 1);
    }

    void on_update(Engine& e, float dt) override {
        if (dt > 0.08f) dt = 0.08f;                 // 掉帧时别一下跳太多
        m_time += dt;
        if (m_shake > 0) m_shake = (m_shake > dt * 40) ? m_shake - dt * 40 : 0;
        if (m_bannerT > 0) m_bannerT -= dt;

        for (Fx& f : m_fx) {
            if (!f.alive) continue;
            f.t += dt;
            if (f.kind == 2) f.y -= 6 * dt;
            if (f.t >= f.life) f.alive = false;
        }

        switch (m_phase) {
        case PH_SELECT:    update_select(e, dt);    break;
        case PH_INTRO:     update_intro(dt);        break;
        case PH_FIGHT:     update_fight(e, dt);     break;
        case PH_KO:        update_ko(e, dt);        break;
        case PH_ROUND_END: update_round_end(e, dt); break;
        case PH_MATCH_END: update_match_end(e, dt); break;
        }
    }

    void on_render(Engine& e) override {
        Display& d = e.display;
        const int W = d.width(), H = d.height();
        int ox = 0, oy = 0;
        if (m_shake > 0) {
            ox = (int)(std::sin(m_time * 61.0f) * m_shake);
            oy = (int)(std::sin(m_time * 47.0f) * m_shake * 0.6f);
        }
        draw_stage(d, W, H, ox, oy);
        if (m_phase == PH_SELECT) {
            draw_select(d);
            draw_banner(d);
            return;
        }
        draw_world(d, ox, oy);
        draw_hud(d);
        draw_banner(d);
    }

private:
    enum Phase { PH_SELECT, PH_INTRO, PH_FIGHT, PH_KO, PH_ROUND_END, PH_MATCH_END };
    static constexpr int kMaxShot = 20;
    static constexpr int kMaxFx   = 24;

    Fighter m_player, m_cpu;
    Shot    m_shots[kMaxShot];
    Fx      m_fx[kMaxFx];
    Phase   m_phase = PH_SELECT;
    float   m_phaseT = 0, m_time = 0;
    int     m_sel = 0, m_round = 0, m_timer = 60;
    float   m_timerAcc = 0, m_shake = 0, m_hitStop = 0;
    char    m_banner[24] = {0};
    float   m_bannerT = 0;

    Engine* m_engine = nullptr;
    const Image* m_shadow = nullptr;
    const Image* m_bigkb = nullptr;
    const Image* m_gun = nullptr;
    const Image* m_spark = nullptr;
    const Image* m_ring = nullptr;
    const Image* m_dizzy = nullptr;
    const Image* m_capS = nullptr;
    const Image* m_capB = nullptr;
    const Image* m_capT = nullptr;

    void cache_assets(Engine& e) {
        m_engine = &e;
        m_shadow = e.img("shadow");
        m_bigkb  = e.img("big_kb");
        m_gun    = e.img("gun");
        m_spark  = e.img("spark");
        m_ring   = e.img("ring");
        m_dizzy  = e.img("dizzy");
        m_capS   = e.img("cap_small");
        m_capB   = e.img("cap_big");
        m_capT   = e.img("cap_tiny");
    }

    // ---- 取精灵 -----------------------------------------------------------
    // 素材名 = <角色>_<动作><帧>。帧数是写死的(与 tools/gen_fighter.py 的 ACTIONS 表
    // 一一对应), 改帧数要两边一起改, 否则会取到空图。
    static const char* pose_of(const Fighter& f) {
        switch (f.act) {
        case A_WALK:                                return "walk";
        case A_CROUCH:                              return "crouch";
        case A_CPUNCH:                              return "cpush";
        case A_CKICK:                               return "ckick";
        case A_JUMP:   case A_JPUNCH: case A_JKICK:
        case A_UPPER:                               return "jump";
        case A_PUNCH:  case A_THROW:  case A_GUN:
        case A_RUSH:   case A_COMBO:                return "punch";
        case A_KICK:   case A_SPINK:                return "kick";
        case A_SUPERKB:                             return "super";
        case A_HURT:   case A_BLOCK:                return "hurt";
        case A_KO:                                  return "ko";
        default:                                    return "idle";
        }
    }
    static int frame_count(const char* pose) {
        if (strcmp(pose, "idle") == 0)   return 2;
        if (strcmp(pose, "walk") == 0)   return 4;
        if (strcmp(pose, "crouch") == 0) return 1;
        if (strcmp(pose, "jump") == 0)   return 2;
        if (strcmp(pose, "punch") == 0)  return 4;
        if (strcmp(pose, "kick") == 0)   return 4;
        if (strcmp(pose, "cpush") == 0)  return 2;
        if (strcmp(pose, "ckick") == 0)  return 3;
        if (strcmp(pose, "hurt") == 0)   return 2;
        if (strcmp(pose, "super") == 0)  return 3;
        if (strcmp(pose, "ko") == 0)     return 2;
        return 1;
    }
    // 取第几帧: 站/走按时间循环; 攻击按 startup/active/recover 三段映射
    // (0 = 蓄力, 1 = "打满"那张, 再往后 = 收势)。这样画面和判定窗口是对齐的,
    // 而每帧动作有 2~4 张图, 不再是一个动作一张图那样僵硬。
    static int frame_of(const Fighter& f) {
        const char* pose = pose_of(f);
        const int n = frame_count(pose);
        if (n <= 1) return 0;
        switch (f.act) {
        case A_IDLE:  return ((int)(f.animT * 2.4f)) & 1;
        case A_WALK:  return ((int)(f.animT * 9.0f)) & 3;
        case A_JUMP:  return (f.vy > 0.0f) ? 1 : 0;
        case A_KO:    return (f.actT < 0.30f) ? 0 : 1;
        case A_HURT:  return (f.actT < 0.14f) ? 0 : 1;
        case A_BLOCK: return 0;
        // 投掷/机关枪没有 Atk 表, 按时间铺帧: 抡起来 -> 出手 -> 收
        case A_THROW: return (f.actT < 0.12f) ? 0 : (f.actT < 0.24f ? 1 : 2);
        case A_GUN:   return (f.actT < 0.12f) ? 0 : 1;
        default: break;
        }
        const Atk* a = current_atk(f);
        if (a == nullptr) return 0;
        if (f.actT < a->startup) return 0;
        if (f.actT < a->startup + a->active) return 1;
        const float r = (f.actT - a->startup - a->active) /
                        (a->recover > 0.0f ? a->recover : 1.0f);
        int fr = 2 + (int)(r * (float)(n - 3));
        if (fr < 1) fr = 1;
        if (fr > n - 1) fr = n - 1;
        return fr;
    }

    const Image* sprite(const Fighter& f) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s_%s%d", f.def->id, pose_of(f), frame_of(f));
        return m_engine->img(buf);
    }
    const Image* char_sprite(int idx, const char* pose, int frame) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s_%s%d", kChars[idx].id, pose, frame);
        return m_engine->img(buf);
    }

    void set_act(Fighter& f, int act) {
        if (f.act == A_KO && act != A_KO) return;      // 倒地后不再切动作
        f.act = act;
        f.actT = 0;
        f.hitDone = false;
        if (act == A_BLOCK) f.blockT = 0.22f;
        if (act == A_COMBO) f.comboSlot = 0;
        if (act == A_UPPER) {                          // 升龙拳: 人跟着起来
            f.air = true;
            f.vy  = -300.0f;
        }
    }

    void add_fx(float x, float y, int kind, float life = 0.26f) {
        for (Fx& f : m_fx) {
            if (f.alive) continue;
            f.alive = true; f.x = x; f.y = y; f.t = 0; f.life = life; f.kind = kind;
            return;
        }
    }

    bool spawn_shot(Fighter& f, int kind) {
        size_t i = 0;
        for (; i < kMaxShot; ++i) { if (!m_shots[i].alive) break; }
        if (i == kMaxShot) return false;
        Shot& s = m_shots[i];
        s.alive = true;
        s.kind = kind;
        s.fromPlayer = !f.cpu;
        s.x = f.x + f.face * 22.0f;
        s.y = f.y - 44.0f;
        if (kind == 0)      { s.vx = f.face * 260.0f; s.vy = 0;         s.dmg = 8; }
        else if (kind == 1) { s.vx = f.face * 150.0f; s.vy = -180.0f;   s.dmg = 11; }
        else                { s.vx = f.face * 320.0f;
                              s.vy = (g_rnd.f01() - 0.5f) * 36.0f;      s.dmg = 5; }
        return true;
    }

    // =======================================================================
    // 命中裁定
    // =======================================================================
    void apply_hit(Engine& e, Fighter& atk, Fighter& def, int dmg, int kb, int dir,
                   bool heavy, float stunT) {
        const bool blocked = def.guard && !def.air &&
                             (def.act == A_IDLE || def.act == A_WALK ||
                              def.act == A_CROUCH || def.act == A_BLOCK);
        if (blocked) {
            const int chip = dmg / 6 + 1;
            def.hp = clampv(def.hp - chip, 0, def.def->hp);
            set_act(def, A_BLOCK);
            def.x += dir * 7;
            def.meter = clampv(def.meter + 3, 0, 100);
            add_fx(def.x - dir * 14, def.y - 42, 1, 0.20f);
            e.audio.play_sfx(e.snd("sfx_block"), 1.0f);
            m_shake = m_shake > 2 ? m_shake : 2;
            return;
        }
        const int real = (int)(dmg * atk.def->dmg);
        def.hp = clampv(def.hp - real, 0, def.def->hp);
        def.x = clampv(def.x + dir * (kb / 26), 26.0f, (float)(kScrW - 26));
        def.face = -dir;
        if (def.hp <= 0) {
            set_act(def, A_KO);
            def.vy = -160; def.air = true; def.stun = 99; def.guard = false;
            add_fx(def.x, def.y - 66, 2, 1.6f);
        } else {
            set_act(def, A_HURT);
            def.stun = stunT;
        }
        atk.meter = clampv(atk.meter + real / 2 + 3, 0, 100);
        def.meter = clampv(def.meter + real / 3 + 2, 0, 100);
        m_hitStop = heavy ? 0.10f : 0.055f;
        m_shake = heavy ? 9.0f : 3.0f;
        add_fx(def.x + def.face * 10, def.y - 46, 0, heavy ? 0.34f : 0.24f);
        e.audio.play_sfx(e.snd(heavy ? "sfx_slam" : "sfx_hit"), 1.0f);
    }

    static Box hurt_box(const Fighter& f) {
        const bool low = (f.act == A_CROUCH || f.act == A_CPUNCH || f.act == A_CKICK);
        const int h = low ? 34 : 56;
        return Box{(int)f.x - 13, (int)f.y - h, 26, h};
    }
    static Box atk_box(const Fighter& f, const Atk& a) {
        const int x = (f.face > 0) ? (int)f.x + a.ox : (int)f.x - a.ox - a.ow;
        return Box{x, (int)f.y + a.oy, a.ow, a.oh};
    }
    static const Atk* current_atk(const Fighter& f) {
        switch (f.act) {
        case A_PUNCH:   return &kPunch;
        case A_KICK:    return &kKick;
        case A_CPUNCH:  return &kCrouchPunch;
        case A_CKICK:   return &kCrouchKick;
        case A_SUPERKB: return &kSuperKb;
        case A_RUSH:    return &kRush;
        case A_UPPER:   return &kUpper;
        case A_SPINK:   return &kSpink;
        case A_COMBO:   return &kCombo;
        case A_JPUNCH:
        case A_JKICK:   return &kJumpAtk;
        default:        return nullptr;
        }
    }

    void resolve_melee(Engine& e, Fighter& a, Fighter& b) {
        const Atk* atk = current_atk(a);
        if (atk == nullptr) return;
        // 百裂连打: 每 0.09s 重置命中标记, 一次出招打三下
        if (a.act == A_COMBO && a.hitDone && a.comboSlot < 3) {
            const int slot = (int)((a.actT - kCombo.startup) / 0.09f);
            if (slot > a.comboSlot) { a.comboSlot = slot; a.hitDone = false; }
        }
        if (a.hitDone) return;
        if (a.actT < atk->startup || a.actT > atk->startup + atk->active) return;
        if (!overlap(atk_box(a, *atk), hurt_box(b))) return;
        a.hitDone = true;
        const int dir = (b.x >= a.x) ? 1 : -1;
        const bool heavy = (a.act == A_SUPERKB || a.act == A_SPINK);
        apply_hit(e, a, b, atk->dmg, atk->kb, dir, heavy, heavy ? 0.55f : 0.30f);
    }

    void update_shots(Engine& e, float dt) {
        for (Shot& s : m_shots) {
            if (!s.alive) continue;
            if (s.kind == 1) s.vy += 430.0f * dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            if (s.x < -24 || s.x > kScrW + 24 || s.y > kFloorY + 6) {
                s.alive = false;
                if (s.kind == 1) add_fx(s.x, (float)kFloorY - 2, 0, 0.18f);
                continue;
            }
            Fighter& target = s.fromPlayer ? m_cpu : m_player;
            if (overlap(Box{(int)s.x - 5, (int)s.y - 5, 10, 10}, hurt_box(target))) {
                s.alive = false;
                Fighter& shooter = s.fromPlayer ? m_player : m_cpu;
                const int dir = (s.vx >= 0) ? 1 : -1;
                apply_hit(e, shooter, target, s.dmg, 70, dir, false, 0.22f);
                e.audio.play_sfx(e.snd("sfx_clack"), 0.9f);
            }
        }
    }

    // =======================================================================
    // 流程
    // =======================================================================
    void start_fight(Engine& e, int pIdx, int cIdx) {
        (void)e;
        const int pMeter = m_player.meter;
        const int cMeter = m_cpu.meter;
        m_player = Fighter{};
        m_cpu = Fighter{};
        m_player.def = &kChars[pIdx];
        m_cpu.def = &kChars[cIdx];
        m_player.cpu = false;
        m_cpu.cpu = true;
        m_player.meter = pMeter;
        m_cpu.meter = cMeter;
        reset_round();
    }

    void reset_round() {
        m_player.x = kScrW * 0.32f;
        m_cpu.x    = kScrW * 0.68f;
        m_player.face = 1;
        m_cpu.face = -1;
        m_player.y = m_cpu.y = (float)kFloorY;
        m_player.hp = m_player.hpShow = m_player.def->hp;
        m_cpu.hp = m_cpu.hpShow = m_cpu.def->hp;
        m_player.air = m_cpu.air = false;
        m_player.vy = m_cpu.vy = 0;
        m_player.stun = m_cpu.stun = 0;
        m_player.guard = m_cpu.guard = false;
        m_player.gunLeft = m_cpu.gunLeft = 0;
        set_act(m_player, A_IDLE);
        set_act(m_cpu, A_IDLE);
        m_player.act = m_cpu.act = A_IDLE;      // set_act 会被 KO 保护挡住, 这里强制复位
        m_player.hitDone = m_cpu.hitDone = false;
        for (Shot& s : m_shots) s.alive = false;
        m_hitStop = 0;
        m_shake = 0;
    }

    void begin_round(Engine& e) {
        reset_round();
        m_phase = PH_INTRO;
        m_phaseT = 0;
        m_timer = 60;
        m_timerAcc = 0;
        snprintf(m_banner, sizeof(m_banner), "ROUND %d", m_round + 1);
        m_bannerT = 1.3f;
        e.audio.play_sfx(e.snd("sfx_round"), 1.0f);
    }

    void update_select(Engine& e, float dt) {
        (void)dt;
        const PadState& p = e.input;
        if (is_pressed(p, Button::Left) && m_sel > 0) {
            m_sel--;
            e.audio.play_sfx(e.snd("sfx_round"), 0.45f);
        }
        if (is_pressed(p, Button::Right) && m_sel < kCharCount - 1) {
            m_sel++;
            e.audio.play_sfx(e.snd("sfx_round"), 0.45f);
        }
        if (is_pressed(p, Button::A) || is_pressed(p, Button::B) ||
            is_pressed(p, Button::Up)) {
            const int rival = (m_sel + 1 + g_rnd.range(0, kCharCount - 2)) % kCharCount;
            m_player.rounds = m_cpu.rounds = 0;
            m_player.meter = m_cpu.meter = 0;
            m_round = 0;
            start_fight(e, m_sel, rival);
            begin_round(e);
        }
    }

    void update_intro(float dt) {
        m_phaseT += dt;
        m_player.animT += dt;
        m_cpu.animT += dt;
        if (m_phaseT > 1.30f && m_banner[0] != 'F') {
            snprintf(m_banner, sizeof(m_banner), "FIGHT!");
            m_bannerT = 0.95f;
        }
        if (m_phaseT > 2.15f) {
            m_phase = PH_FIGHT;
            m_phaseT = 0;
        }
    }

    void update_fight(Engine& e, float dt) {
        if (m_hitStop > 0) {                    // 命中停顿: 全场冻结, 只让血条追
            m_hitStop -= dt;
            if (m_hitStop > 0) { tick_bars(dt); return; }
        }
        m_timerAcc += dt;
        if (m_timerAcc >= 1.0f) {
            m_timerAcc -= 1.0f;
            if (m_timer > 0) m_timer--;
        }
        if (m_timer <= 0) {
            m_phase = PH_KO;
            m_phaseT = 0;
            snprintf(m_banner, sizeof(m_banner), "TIME UP");
            m_bannerT = 1.5f;
            return;
        }

        control(m_player, e.input, dt);
        ai(m_cpu, dt);
        step(m_player, dt);
        step(m_cpu, dt);
        push_apart();
        resolve_melee(e, m_player, m_cpu);
        resolve_melee(e, m_cpu, m_player);
        update_shots(e, dt);
        tick_bars(dt);

        if (m_player.hp <= 0 || m_cpu.hp <= 0) {
            m_phase = PH_KO;
            m_phaseT = 0;
            m_shake = 7;
            snprintf(m_banner, sizeof(m_banner), "K.O.");
            m_bannerT = 1.5f;
            e.audio.play_sfx(e.snd("sfx_ko"), 1.0f);
        }
    }

    void tick_bars(float dt) {
        Fighter* both[2] = {&m_player, &m_cpu};
        for (Fighter* f : both) {
            if (f->hpShow <= f->hp) continue;
            const float step = ((f->hpShow - f->hp) * 4.5f + 14.0f) * dt;
            f->hpShow = (f->hpShow - (int)step <= f->hp) ? f->hp
                                                         : f->hpShow - (int)step;
        }
    }

    void update_ko(Engine& e, float dt) {
        m_phaseT += dt;
        m_player.animT += dt;
        m_cpu.animT += dt;
        Fighter* both[2] = {&m_player, &m_cpu};
        for (Fighter* f : both) {
            if (f->act != A_KO) continue;
            f->vy += kGravity * 0.55f * dt;
            f->y += f->vy * dt;
            if (f->y >= kFloorY) { f->y = kFloorY; f->vy = 0; f->air = false; }
            f->x = clampv(f->x - f->face * 16 * dt, 26.0f, (float)(kScrW - 26));
        }
        update_shots(e, dt);
        tick_bars(dt);
        if (m_phaseT > 1.9f) {
            const bool pWon = (m_cpu.hp <= 0) ||
                              (m_player.hp > 0 && m_player.hp > m_cpu.hp);
            if (pWon) m_player.rounds++; else m_cpu.rounds++;
            m_phase = PH_ROUND_END;
            m_phaseT = 0;
            snprintf(m_banner, sizeof(m_banner), "%s", pWon ? "YOU WIN" : "YOU LOSE");
            m_bannerT = 1.6f;
        }
    }

    void update_round_end(Engine& e, float dt) {
        m_phaseT += dt;
        if (m_phaseT < 1.7f) return;
        if (m_player.rounds >= 2 || m_cpu.rounds >= 2) {
            m_phase = PH_MATCH_END;
            m_phaseT = 0;
            snprintf(m_banner, sizeof(m_banner), "%s",
                     m_player.rounds >= 2 ? "MATCH  WIN" : "MATCH  LOST");
            m_bannerT = 3.0f;
            e.audio.play_sfx(e.snd("sfx_ko"), 1.0f);
            return;
        }
        m_round++;
        begin_round(e);
    }

    void update_match_end(Engine& e, float dt) {
        m_phaseT += dt;
        m_player.animT += dt;
        m_cpu.animT += dt;
        if (m_phaseT > 1.4f &&
            (is_pressed(e.input, Button::A) || is_pressed(e.input, Button::B) ||
             is_pressed(e.input, Button::Up))) {
            m_player.rounds = m_cpu.rounds = 0;
            m_player.meter = m_cpu.meter = 0;
            m_round = 0;
            m_phase = PH_SELECT;
            m_phaseT = 0;
            m_banner[0] = 0;
            e.audio.play_sfx(e.snd("sfx_round"), 1.0f);
        }
    }

    // =======================================================================
    // 玩家操作
    // =======================================================================
    static bool busy_act(const Fighter& f) {
        switch (f.act) {
        case A_PUNCH: case A_KICK: case A_CPUNCH: case A_CKICK:
        case A_JPUNCH: case A_JKICK: case A_THROW: case A_SUPERKB:
        case A_GUN: case A_HURT: case A_BLOCK: case A_KO:
        case A_RUSH: case A_UPPER: case A_SPINK: case A_COMBO:
            return true;
        default:
            return false;
        }
    }

    void control(Fighter& f, const PadState& p, float dt) {
        // 简化指令: 先 ↓, 0.42s 内按前/后, 再 0.45s 内按 A/B
        if (f.downT > 0) f.downT -= dt;
        if (f.armF > 0)  f.armF -= dt;
        if (f.armB > 0)  f.armB -= dt;
        if (is_pressed(p, Button::Down)) f.downT = 0.42f;
        if (f.downT > 0) {
            if (is_pressed(p, f.face > 0 ? Button::Right : Button::Left)) f.armF = 0.45f;
            if (is_pressed(p, f.face > 0 ? Button::Left : Button::Right)) f.armB = 0.45f;
        }

        const Button fwd  = (f.face > 0) ? Button::Right : Button::Left;
        const Button back = (f.face > 0) ? Button::Left  : Button::Right;
        f.guard = is_held(p, back) && !f.air;

        if (busy_act(f)) return;

        // 拳/踢各有两个键: A/B 是主键, C/D 复用(它们就在十字键正上方, 最顺手)。
        // 设备上手柄只有 A/B/C/D 四个动作键, 多给一对能顺手很多。
        const bool punchBtn = is_pressed(p, Button::A) || is_pressed(p, Button::C);
        const bool kickBtn  = is_pressed(p, Button::B) || is_pressed(p, Button::D);

        if (punchBtn) {
            if (f.armF > 0) {                    // ↓前+A
                f.armF = 0;
                if (f.def->kb) { f.throwKind = 0; set_act(f, A_THROW); }   // 平抛键帽
                else            set_act(f, A_RUSH);                        // 冲拳
            } else if (f.armB > 0) {             // ↓后+A
                f.armB = 0;
                if (f.def->kb) { f.throwKind = 1; set_act(f, A_THROW); }   // 高抛键帽
                else            set_act(f, A_UPPER);                       // 升龙拳
            } else {
                set_act(f, f.air ? A_JPUNCH
                                 : (is_held(p, Button::Down) ? A_CPUNCH : A_PUNCH));
            }
        } else if (kickBtn) {
            if (f.armF > 0 && f.meter >= 100) {  // ↓前+B
                f.armF = 0; f.meter = 0;
                if (f.def->kb) set_act(f, A_SUPERKB);      // 巨型键盘砸
                else            set_act(f, A_SPINK);        // 回旋踢
            } else if (f.armB > 0 && f.meter >= 100) {     // ↓后+B
                f.armB = 0; f.meter = 0;
                if (f.def->kb) { set_act(f, A_GUN); f.gunLeft = 14; f.gunT = 0; }  // 键帽机枪
                else            set_act(f, A_COMBO);                                // 百裂连打
            } else {
                set_act(f, f.air ? A_JKICK
                                 : (is_held(p, Button::Down) ? A_CKICK : A_KICK));
            }
        }

        if (busy_act(f)) return;

        // 走 / 蹲 / 跳
        if (!f.air && is_pressed(p, Button::Up)) {
            set_act(f, A_JUMP);
            f.air = true;
            f.vy = kJumpVy;
            if (is_held(p, Button::Left))  f.face = (f.x > kScrW / 2) ? -1 : f.face;
            if (is_held(p, Button::Right)) f.face = (f.x < kScrW / 2) ? 1 : f.face;
            return;
        }
        if (is_held(p, Button::Down) && !f.air) {
            set_act(f, A_CROUCH);
            return;
        }
        int dir = 0;
        if (is_held(p, Button::Left))  dir -= 1;
        if (is_held(p, Button::Right)) dir += 1;
        if (dir != 0 && !f.air) {
            if (f.act != A_WALK) set_act(f, A_WALK);
            f.x = clampv(f.x + dir * 72.0f * f.def->speed * dt, 26.0f, (float)(kScrW - 26));
            (void)fwd;
        } else if (dir != 0 && f.air) {
            f.x = clampv(f.x + dir * 54.0f * f.def->speed * dt, 26.0f, (float)(kScrW - 26));
        } else if (f.act == A_WALK || f.act == A_CROUCH) {
            set_act(f, A_IDLE);
        }
    }

    // =======================================================================
    // CPU
    // =======================================================================
    void ai(Fighter& f, float dt) {
        Fighter& p = m_player;
        f.face = (p.x >= f.x) ? 1 : -1;
        const float dist = std::fabs(p.x - f.x);

        if (!f.air && f.act != A_HURT && f.act != A_KO && f.act != A_BLOCK) {
            // 平时不开格挡, 只有判定"对手正在出招"时才拉后
            const bool pAtk = current_atk(p) != nullptr && !p.hitDone;
            f.guard = pAtk && dist < 70.0f && g_rnd.f01() < 0.55f;
        }

        if (busy_act(f)) return;

        f.decT -= dt;
        if (f.decT > 0) {
            switch (f.dec) {
            case 1: if (dist > 42) { set_act(f, A_WALK);
                        f.x = clampv(f.x + f.face * 70.0f * f.def->speed * dt,
                                     26.0f, (float)(kScrW - 26)); }
                    break;
            case 2: set_act(f, A_WALK);
                    f.x = clampv(f.x - f.face * 56.0f * f.def->speed * dt,
                                 26.0f, (float)(kScrW - 26));
                    break;
            case 3: set_act(f, (dist < 60) ? A_PUNCH : A_KICK); break;
            case 4: set_act(f, (dist < 60) ? A_KICK : A_PUNCH); break;
            case 5: set_act(f, A_CKICK); break;
            case 6: set_act(f, A_CROUCH); break;
            case 7:  if (f.def->kb) { f.throwKind = 0; set_act(f, A_THROW); }
                     else set_act(f, A_RUSH);
                     break;
            case 8:  if (f.def->kb) { f.throwKind = 1; set_act(f, A_THROW); }
                     else set_act(f, A_UPPER);
                     break;
            case 9: if (dist < 80) { f.meter = 0; set_act(f, f.def->kb ? A_SUPERKB : A_SPINK); }
                    else set_act(f, A_PUNCH);
                    break;
            case 10: if (dist > 88) {
                         f.meter = 0;
                         if (f.def->kb) { set_act(f, A_GUN); f.gunLeft = 14; f.gunT = 0; }
                         else set_act(f, A_COMBO);
                     } else if (f.def->kb) { f.throwKind = 0; set_act(f, A_THROW); }
                     else set_act(f, A_RUSH);
                     break;
            default: break;
            }
            return;
        }

        // 重新决策(回合越往后越凶)
        const float lvl = 0.55f + 0.14f * (float)m_round;
        f.decT = 0.15f + g_rnd.f01() * 0.20f;
        const float r = g_rnd.f01();
        if (f.meter >= 100) {
            f.dec = (dist < 78) ? 9 : (dist > 92 ? 10 : 1);
        } else if (dist > 150) {
            f.dec = (r < 0.45f) ? 1 : (r < 0.78f ? 7 : 8);
        } else if (dist > 74) {
            f.dec = (r < 0.45f + 0.2f * lvl) ? 1 : (r < 0.82f ? 7 : 2);
        } else if (dist > 50) {
            f.dec = (r < 0.38f) ? 3 : (r < 0.72f ? 4 : (r < 0.86f ? 5 : 2));
        } else {
            f.dec = (r < 0.22f + 0.22f * lvl) ? 3
                  : (r < 0.70f ? 4 : (r < 0.86f ? 5 : 2));
        }
    }

    // =======================================================================
    // 逐帧推进(双方共用)
    // =======================================================================
    void step(Fighter& f, float dt) {
        f.animT += dt;
        f.actT += dt;
        if (f.stun > 0) f.stun -= dt;
        if (f.blockT > 0) f.blockT -= dt;

        if (f.air) {
            f.vy += kGravity * dt;
            f.y += f.vy * dt;
            if (f.y >= kFloorY) {
                f.y = kFloorY;
                f.vy = 0;
                f.air = false;
                if (f.act != A_KO) set_act(f, A_IDLE);
            }
        }

        // 机关枪: 持续吐键帽
        if (f.act == A_GUN) {
            f.gunT -= dt;
            if (f.gunLeft > 0 && f.gunT <= 0) {
                f.gunLeft--;
                f.gunT = 0.055f;
                spawn_shot(f, 2);
            } else if (f.gunLeft <= 0 && f.actT > 0.50f) {
                set_act(f, A_IDLE);
            }
        }
        // 投掷动作到出手帧 -> 生成键帽
        if (f.act == A_THROW && !f.hitDone && f.actT >= 0.12f) {
            f.hitDone = true;
            spawn_shot(f, f.throwKind);
        }

        switch (f.act) {
        case A_JUMP:    if (!f.air) set_act(f, A_IDLE); break;
        case A_JPUNCH:  if (!f.air || f.actT > 0.55f) set_act(f, A_JUMP); break;
        case A_JKICK:   if (!f.air || f.actT > 0.55f) set_act(f, A_JUMP); break;
        case A_PUNCH:   if (f.actT > kPunch.startup + kPunch.active + kPunch.recover)
                            set_act(f, A_IDLE); break;
        case A_KICK:    if (f.actT > kKick.startup + kKick.active + kKick.recover)
                            set_act(f, A_IDLE); break;
        case A_CPUNCH:  if (f.actT > kCrouchPunch.startup + kCrouchPunch.active +
                                    kCrouchPunch.recover) set_act(f, A_CROUCH); break;
        case A_CKICK:   if (f.actT > kCrouchKick.startup + kCrouchKick.active +
                                    kCrouchKick.recover) set_act(f, A_CROUCH); break;
        case A_THROW:   if (f.actT > 0.32f) set_act(f, A_IDLE); break;
        case A_SUPERKB: if (f.actT > kSuperKb.startup + kSuperKb.active + kSuperKb.recover)
                            set_act(f, A_IDLE); break;
        case A_RUSH:    if (f.actT > kRush.startup + kRush.active + kRush.recover)
                            set_act(f, A_IDLE); break;
        case A_SPINK:   if (f.actT > kSpink.startup + kSpink.active + kSpink.recover)
                            set_act(f, A_IDLE); break;
        case A_COMBO:   if (f.actT > kCombo.startup + kCombo.active + kCombo.recover)
                            set_act(f, A_IDLE); break;
        // A_UPPER 是跳起来的, 落地时由上面的 air 分支收回 A_IDLE
        case A_HURT:    if (f.stun <= 0) set_act(f, A_IDLE); break;
        case A_BLOCK:   if (f.blockT <= 0) set_act(f, A_IDLE); break;
        default: break;
        }
    }

    void push_apart() {
        const float dx = m_cpu.x - m_player.x;
        const float minD = 34.0f;
        if (std::fabs(dx) >= minD) return;
        const float push = (minD - std::fabs(dx)) * 0.5f;
        const float s = (dx >= 0) ? 1.0f : -1.0f;
        m_player.x = clampv(m_player.x - s * push, 26.0f, (float)(kScrW - 26));
        m_cpu.x    = clampv(m_cpu.x + s * push, 26.0f, (float)(kScrW - 26));
    }

    // =======================================================================
    // 绘制
    // =======================================================================
    // 取一个像素: 调色板非空就是 4bpp 索引图(索引 0 = 透明), 否则是 RGB565(品红 = 透明)。
    // 返回 -1 表示透明, 否则是 RGB565 颜色值。
    static inline int pix_at(const Image& im, int x, int y) {
        if (im.pal != nullptr) {
            const size_t i = (size_t)y * im.w + (size_t)x;
            const uint8_t b = im.data[i >> 1];
            const uint8_t v = (i & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)(b >> 4);
            return (v == 0) ? -1 : (int)im.pal[v];
        }
        const uint8_t* p = im.data + ((size_t)y * im.w + (size_t)x) * 2;
        const uint16_t c = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
        return (c == kKey) ? -1 : (int)c;
    }

    void blit(Display& d, const Image* im, int cx, int feetY, bool flip) {
        if (im == nullptr) return;
        const int x0 = cx - im->w / 2;
        const int y0 = feetY - im->h;
        for (int y = 0; y < im->h; ++y) {
            const int dy = y0 + y;
            if (dy < 0 || dy >= d.height()) continue;
            for (int x = 0; x < im->w; ++x) {
                const int sx = flip ? (im->w - 1 - x) : x;
                const int c = pix_at(*im, sx, y);
                if (c < 0) continue;
                const int dx = x0 + x;
                if (dx < 0 || dx >= d.width()) continue;
                d.set_pixel(dx, dy, (uint16_t)c);
            }
        }
    }

    void blit_c(Display& d, const Image* im, int cx, int cy) {
        if (im == nullptr) return;
        const int x0 = cx - im->w / 2;
        const int y0 = cy - im->h / 2;
        for (int y = 0; y < im->h; ++y) {
            const int dy = y0 + y;
            if (dy < 0 || dy >= d.height()) continue;
            for (int x = 0; x < im->w; ++x) {
                const int c = pix_at(*im, x, y);
                if (c < 0) continue;
                const int dx = x0 + x;
                if (dx < 0 || dx >= d.width()) continue;
                d.set_pixel(dx, dy, (uint16_t)c);
            }
        }
    }

    // 游戏是按 428x142 的逻辑视口画的。平台分辨率更大时(比如 S3 开发板
    // 480x320)先铺一层暗色外框, 游戏只占左上角 428x142 —— 这样既不会在
    // 右侧/下方留下上一帧的残影, 看上去也像是"这台设备上的游戏画面"。
    void draw_stage(Display& d, int W, int H, int ox, int oy) {
        if (W > kScrW || H > kScrH) d.fill_rect(0, 0, W, H, rgb565(8, 10, 18));
        const int vw = kScrW;

        // 夜空: 逐行渐变。早先只有 8 段色带, 在 142 行的屏上看着是一条条横纹,
        // 改成每行一色就干净了(108 次整行填充 + 亮度抖动, 相对整屏重绘可忽略)。
        const int yTop = kHudH, yBot = kFloorY;
        for (int y = yTop; y < yBot; ++y) {
            const float t = (float)(y - yTop) / (float)(yBot - yTop);
            const int r = (int)(12 + 132 * t * t);
            const int g = (int)(14 +  84 * t * t);
            const int b = (int)(36 +  80 * t);
            d.fill_rect(ox, y + oy, vw, 1, rgb565((uint8_t)(r > 255 ? 255 : r),
                                                  (uint8_t)(g > 255 ? 255 : g),
                                                  (uint8_t)(b > 255 ? 255 : b)));
        }
        // 月亮
        const int mx = 336 + ox, my = 40 + oy;
        for (int i = 0; i < 9; ++i) {
            const int w = 16 - std::abs(4 - i) * 3;
            d.fill_rect(mx - w / 2, my - 5 + i, w, 1, rgb565(238, 234, 202));
        }
        // 远山/城市剪影
        for (int i = 0; i < 24; ++i) {
            const int bx = ((i * 29 + 7) % (vw - 24)) + ox;
            const int bh = 10 + ((i * 37) % 20);
            const int bw = 16 + (i % 3) * 8;
            d.fill_rect(bx, kFloorY - bh + oy, bw, bh, rgb565(22, 24, 48));
            if ((i % 3) == 0) {
                for (int wy = kFloorY - bh + 4; wy < kFloorY - 4; wy += 6) {
                    d.fill_rect(bx + 3, wy + oy, 2, 2, rgb565(238, 208, 120));
                    d.fill_rect(bx + 9, wy + oy, 2, 2, rgb565(238, 208, 120));
                }
            }
        }
        // 擂台: 台面 + 木板纹
        d.fill_rect(ox, kFloorY + oy, vw, kScrH - kFloorY, rgb565(72, 50, 42));
        d.fill_rect(ox, kFloorY + oy, vw, 2, rgb565(132, 96, 66));
        for (int y = kFloorY + 6; y < kScrH; y += 5) {
            d.fill_rect(ox, y + oy, vw, 1, rgb565(52, 36, 32));
        }
        for (int x = 0; x < vw; x += 26) {
            d.fill_rect(x + ox, kFloorY + 2 + oy, 1, kScrH - kFloorY, rgb565(52, 36, 32));
        }
    }

    void draw_one(Display& d, Fighter& f, int ox, int oy) {
        if (m_shadow != nullptr && !f.air) {
            blit_c(d, m_shadow, (int)f.x + ox, kFloorY + 2 + oy);
        }
        blit(d, sprite(f), (int)f.x + ox, (int)f.y + oy, f.face < 0);

        if (f.act == A_SUPERKB && m_bigkb != nullptr) {
            float wy = f.y - 64.0f;
            if (f.actT > kSuperKb.startup) {          // 抡下来
                const float k = (f.actT - kSuperKb.startup) / kSuperKb.active;
                wy += k * 42.0f;
            }
            blit(d, m_bigkb, (int)f.x + f.face * 4 + ox, (int)wy + oy, f.face < 0);
        }
        if (f.act == A_GUN && m_gun != nullptr) {
            blit(d, m_gun, (int)f.x + f.face * 18 + ox, (int)f.y - 42 + oy, f.face < 0);
        }
        if (f.act == A_KO && m_dizzy != nullptr) {
            for (int i = 0; i < 3; ++i) {
                const float a = f.animT * 3.2f + i * 2.1f;
                blit_c(d, m_dizzy,
                       (int)(f.x + std::cos(a) * 14) + ox,
                       (int)(f.y - 66 - std::fabs(std::sin(a)) * 5) + oy);
            }
        }
    }

    void draw_world(Display& d, int ox, int oy) {
        for (Shot& s : m_shots) {
            if (!s.alive) continue;
            const Image* im = (s.kind == 1) ? m_capB : (s.kind == 2 ? m_capT : m_capS);
            blit_c(d, im, (int)s.x + ox, (int)s.y + oy);
        }
        // 跳到后面的人先画
        if (m_player.y <= m_cpu.y) { draw_one(d, m_cpu, ox, oy); draw_one(d, m_player, ox, oy); }
        else                       { draw_one(d, m_player, ox, oy); draw_one(d, m_cpu, ox, oy); }
        for (Fx& f : m_fx) {
            if (!f.alive) continue;
            const Image* im = (f.kind == 0) ? m_spark : (f.kind == 1 ? m_ring : m_dizzy);
            if (im == nullptr) continue;
            const int n = 1 + (int)((1.0f - f.t / f.life) * 1.6f);
            for (int i = 0; i < n; ++i) blit_c(d, im, (int)f.x + ox, (int)f.y + oy);
        }
    }

    void draw_hud(Display& d) {
        const int W = kScrW;
        d.fill_rect(0, 0, W, kHudH, kInk);
        d.fill_rect(0, kHudH - 1, W, 1, rgb565(60, 74, 108));
        draw_bar(d, m_player, 6, 6, 152, false);
        draw_bar(d, m_cpu, W - 158, 6, 152, true);

        char buf[8];
        snprintf(buf, sizeof(buf), "%02d", m_timer < 0 ? 0 : m_timer);
        const int tw = (int)strlen(buf) * 12;
        d.text((W - tw) / 2, 1, buf, m_timer <= 10 ? rgb565(255, 96, 96) : kWhite, 2);

        for (int i = 0; i < 2; ++i) {
            if (m_player.rounds > i) d.fill_rect(W / 2 - 24 + i * 8, 13, 6, 4, rgb565(250, 214, 92));
            if (m_cpu.rounds > i)    d.fill_rect(W / 2 + 12 + i * 8, 13, 6, 4, rgb565(250, 214, 92));
        }
    }

    void draw_bar(Display& d, Fighter& f, int x, int y, int w, bool rtl) {
        const int full = w - 2;
        const char* nm = f.def->label;
        const int nw = (int)strlen(nm) * 6;
        d.text(rtl ? (x + w - nw) : x, y - 5, nm,
               f.meter >= 100 ? rgb565(255, 240, 140) : rgb565(176, 190, 210), 1);

        d.fill_rect(x - 1, y - 1, w + 2, 12, rgb565(68, 78, 108));
        d.fill_rect(x, y, w, 10, rgb565(30, 22, 24));
        const int nShow = full * f.hpShow / f.def->hp;
        const int nHp   = full * f.hp / f.def->hp;
        const int bx = rtl ? (x + 1 + full - nShow) : (x + 1);
        d.fill_rect(bx, y + 1, nShow, 8, rgb565(250, 214, 92));
        const int hx = rtl ? (x + 1 + full - nHp) : (x + 1);
        d.fill_rect(hx, y + 1, nHp, 8, f.def->bar);
        d.fill_rect(x + 1, y + 9, full, 1, rgb565(20, 16, 18));
        const int mw = full * f.meter / 100;
        const int mx = rtl ? (x + 1 + full - mw) : (x + 1);
        d.fill_rect(x + 1, y + 11, full, 3, rgb565(26, 34, 50));
        d.fill_rect(mx, y + 11, mw, 3,
                    f.meter >= 100 ? rgb565(255, 240, 140) : rgb565(88, 148, 200));
    }

    void draw_select(Display& d) {
        const int W = kScrW;
        for (int i = 0; i < kCharCount; ++i) {
            const bool sel = (i == m_sel);
            const int cx = 58 + i * 104;
            const float bob = sel ? std::sin(m_time * 6.0f) * 2.0f : 0.0f;
            const int feet = (int)((float)(kFloorY + 2) - (sel ? 14.0f : 0.0f) + bob);
            // 选中的人摆大招砸键那一下(比待机姿势更有气势)
            blit(d, char_sprite(i, sel ? "super" : "idle", sel ? 1 : 0), cx, feet, false);
            const int nw = (int)strlen(kChars[i].label) * 6;
            d.text(cx - nw / 2, kScrH - 9, kChars[i].label,
                   sel ? rgb565(255, 240, 140) : rgb565(130, 144, 168), 1);
            if (sel) {
                d.fill_rect(cx - 26, kFloorY - 74, 52, 1, rgb565(120, 220, 255));
                d.fill_rect(cx - 26, kFloorY + 2, 52, 1, rgb565(120, 220, 255));
            }
        }
        const char* hint = "LEFT/RIGHT  SELECT    A/B  FIGHT";
        const int hw = (int)strlen(hint) * 6;
        d.text((W - hw) / 2, kScrH - 8 - 0, hint, rgb565(150, 164, 190), 1);
        // 必杀指令提示: 键盘精灵是键帽系招, 其余三位是徒手招
        const char* hint2 = kChars[m_sel].kb ? "DOWN+FWD/BACK + A/B: KEYCAP MOVES"
                                             : "DOWN+FWD/BACK + A/B: SPECIAL";
        const int hw2 = (int)strlen(hint2) * 6;
        d.text((W - hw2) / 2, kScrH - 17, hint2, rgb565(120, 134, 160), 1);
    }

    void draw_banner(Display& d) {
        if (m_banner[0] == 0 || m_bannerT <= 0) return;
        const int W = kScrW, H = kScrH;
        const int scale = 3;
        const int tw = (int)strlen(m_banner) * 6 * scale;
        const int y = H / 2 - 14;
        d.fill_rect(0, y, W, 28, rgb565(18, 12, 20));
        d.fill_rect(0, y, W, 1, rgb565(240, 200, 90));
        d.fill_rect(0, y + 27, W, 1, rgb565(240, 200, 90));
        d.text((W - tw) / 2, y + 4, m_banner, rgb565(255, 236, 150), scale);
    }
};

} // namespace

engine::Game* make_fighter() { return new FighterGame(); }

} // namespace games
