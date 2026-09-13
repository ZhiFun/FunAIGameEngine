// ============================================================================
// games/keychase/game.cpp —— KEY CHASE(键帽迷宫追逐)
//
// 玩法: 迷宫吃豆类 —— 吃光迷宫里的豆过关, 期间躲开四个追兵; 吃到四角的大力丸
// 可以反过来吃掉追兵(连吃加分)。规则与手感都照街机那一套做全:
//
//   · 迷宫 30x9(屏幕只有 428x142, 放不下方迷宫), 4 条横向走廊 + 竖井,
//     第 4 行左右两端是隧道, 走出去会从另一边回来
//   · 中间是鬼屋: 追兵按吃豆进度依次放出来; 门只有追兵能穿, 玩家进不去
//   · 四个追兵各有各的脾气(直线追 / 抄你前路 / 反折夹击 / 近则回角),
//     并且按 巡逻-追击 交替切换; 大力丸期间乱跑且变慢, 被吃后只剩眼睛跑回鬼屋
//   · 豆 10 分, 大力丸 50 分, 连吃追兵 200/400/800/1600, 水果按关数给分
//   · 3 条命, 10000 分加一条, 水果在第 70/170 颗豆时出现
//   · 通关后迷宫闪两下, 下一关追兵更快、大力丸时间更短
//
// 迷宫数据由 tools/gen_keychase.py 生成(那边会做连通性校验, 见 maze_data.h);
// 精灵/音效也是同一个生成器出的。素材名一律 <名字>.img/.snd, 走 4bpp 索引图。
// ============================================================================
#include "game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/Engine.h"
#include "maze_data.h"

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

using keychase::kDotCount;
using keychase::kDoorCol;
using keychase::kDoorRow;
using keychase::kFruitCol;
using keychase::kFruitRow;
using keychase::kHomeCol;
using keychase::kHomeRow;
using keychase::kMaze;
using keychase::kMazeCols;
using keychase::kMazeRows;
using keychase::kPowerCount;
using keychase::kScatterCol;
using keychase::kScatterRow;
using keychase::kStartCol;
using keychase::kStartRow;

// ---------------------------------------------------------------------------
// 屏幕/迷宫几何
// ---------------------------------------------------------------------------
constexpr int kScrW  = 428;
constexpr int kScrH  = 142;
constexpr int kHudH  = 16;                                  // 顶部状态条
constexpr int kTile  = 14;                                  // 格子边长(px)
constexpr int kMazeW = kMazeCols * kTile;                    // 420
constexpr int kMazeH = kMazeRows * kTile;                    // 126
constexpr int kMazeX = (kScrW - kMazeW) / 2;                 // 4
constexpr int kMazeY = kHudH;                                // 16
constexpr int kTunnelRow = 4;                                // 隧道所在行

// 颜色(整屏只有这一套, 想改风格改这里)
constexpr Color kBg        = rgb565(   8,  10,  20);
constexpr Color kWallBody  = rgb565(  14,  26,  62);
constexpr Color kWallEdge  = rgb565(  52, 128, 232);
constexpr Color kWallFlash = rgb565( 228, 240, 255);
constexpr Color kDotCol    = rgb565( 246, 226, 168);
constexpr Color kTextCol   = rgb565( 214, 226, 244);
constexpr Color kDimCol    = rgb565( 120, 136, 168);
constexpr Color kAccent    = rgb565(  96, 220, 244);

// 方向
enum { DIR_R = 0, DIR_L, DIR_U, DIR_D, DIR_NONE };
constexpr int kDX[4] = { 1, -1, 0, 0 };
constexpr int kDY[4] = { 0, 0, -1, 1 };
inline int rev_dir(int d) { return (d == DIR_R) ? DIR_L : (d == DIR_L) ? DIR_R
                                : (d == DIR_U) ? DIR_D : DIR_U; }

// 阶段 / 状态
enum { PH_READY, PH_PLAY, PH_DYING, PH_CLEAR, PH_OVER };
enum { GH_HOME, GH_LEAVING, GH_NORMAL, GH_SCARED, GH_EATEN };
enum { MD_SCATTER, MD_CHASE };

// 瓦片
enum : uint8_t { T_EMPTY = 0, T_DOT = 1, T_POWER = 2 };

// 打法常量
constexpr float kReadyTime   = 1.7f;
constexpr float kDyingTime   = 1.9f;
constexpr float kClearTime   = 2.3f;
constexpr float kFruitShow   = 9.0f;
constexpr int   kStartLives  = 3;
constexpr int   kExtraAt     = 10000;
constexpr int   kFruitScore[3] = { 100, 300, 500 };

// 速度(px/s): 玩家 / 追兵 / 大力丸期间 / 眼睛回家 / 隧道内
constexpr float kSpeedPlayer = 62.0f;
constexpr float kSpeedGhost  = 57.0f;
constexpr float kSpeedScared = 34.0f;
constexpr float kSpeedEaten  = 126.0f;
constexpr float kSpeedTunnel = 38.0f;

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
struct Rnd {
    uint32_t s = 0x9E3779B9u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int range(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
};
Rnd g_rnd;

template <typename T> T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 列方向环绕。⚠ 不能用 c % kMazeCols: C++ 里负数取模还是负数, 会读到数组外面去
// (隧道里 x 会掉到 -3, col_of 给出 -1)。
inline int wrap_col(int c) {
    c %= kMazeCols;
    return (c < 0) ? c + kMazeCols : c;
}
inline bool maze_wall(int c, int r) {
    if (r < 0 || r >= kMazeRows) return true;
    return kMaze[r][wrap_col(c)] == '#';
}
inline bool maze_door(int c, int r) {
    if (r < 0 || r >= kMazeRows) return false;
    return kMaze[r][wrap_col(c)] == '-';
}

struct Actor {
    float x = 0, y = 0;      // 像素坐标(格心)
    int   dir = DIR_L;
    int   want = DIR_NONE;   // 玩家: 想转的方向
    float speed = kSpeedPlayer;
};

struct Ghost {
    Actor a;
    int   id = 0;
    int   state = GH_HOME;
    int   releaseDots = 0;   // 吃够这么多豆放出来
    float releaseWait = 0;   // 或者最多等这么久(秒)
    float timer = 0;         // 在屋里待了多久
    int   quitState = 0;     // 离开鬼屋的两段: 0 先到门口, 1 再往上出
};

struct Fx {
    float t = 0, life = 0.35f;
    int   x = 0, y = 0, score = 0;
};

// ---------------------------------------------------------------------------
class KeyChaseGame : public engine::Game {
public:
    const char* name() const override { return "keychase"; }

    // ---- 生命周期 ----------------------------------------------------------
    void on_start(Engine& e) override {
        m_e = &e;
        m_img.muncher[0][0] = e.img("muncher_r0");
        m_img.muncher[0][1] = e.img("muncher_r1");
        m_img.muncher[1][0] = e.img("muncher_l0");
        m_img.muncher[1][1] = e.img("muncher_l1");
        m_img.muncher[2][0] = e.img("muncher_u0");
        m_img.muncher[2][1] = e.img("muncher_u1");
        m_img.muncher[3][0] = e.img("muncher_d0");
        m_img.muncher[3][1] = e.img("muncher_d1");
        const char* gname[4] = { "ghost_g1", "ghost_g2", "ghost_g3", "ghost_g4" };
        for (int i = 0; i < 4; ++i) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%s_0", gname[i]);
            m_img.ghost[i][0] = e.img(buf);
            snprintf(buf, sizeof(buf), "%s_1", gname[i]);
            m_img.ghost[i][1] = e.img(buf);
        }
        m_img.scared[0] = e.img("ghost_scared0");
        m_img.scared[1] = e.img("ghost_scared1");
        m_img.eyes[DIR_R] = e.img("eyes_r");
        m_img.eyes[DIR_L] = e.img("eyes_l");
        m_img.eyes[DIR_U] = e.img("eyes_u");
        m_img.eyes[DIR_D] = e.img("eyes_d");
        m_img.dot    = e.img("dot");
        m_img.pellet[0] = e.img("pellet0");
        m_img.pellet[1] = e.img("pellet1");
        m_img.fruit[0] = e.img("fruit0");
        m_img.fruit[1] = e.img("fruit1");
        m_img.fruit[2] = e.img("fruit2");

        g_rnd.s = 0x2468ACE0u;
        m_score = 0;
        m_lives = kStartLives;
        m_level = 1;
        start_level(e, true);
    }

    void on_update(Engine& e, float dt) override {
        if (dt > 0.10f) dt = 0.10f;              // 掉帧别让追兵瞬移
        m_time += dt;
        if (m_fx.life > 0) m_fx.life -= dt;

        switch (m_phase) {
        case PH_READY: update_ready(e, dt); break;
        case PH_PLAY:  update_play(e, dt);  break;
        case PH_DYING: update_dying(e, dt); break;
        case PH_CLEAR: update_clear(e, dt); break;
        case PH_OVER:  update_over(e, dt);  break;
        }
    }

    void on_render(Engine& e) override {
        Display& d = e.display;
        const int W = d.width(), H = d.height();
        if (W > kScrW || H > kScrH) d.fill_rect(0, 0, W, H, kBg);   // 平台更大时补底
        draw_maze(d);
        draw_pellets(d);
        draw_hud(d);
        if (m_phase == PH_READY)      draw_banner(d, "READY!");
        else if (m_phase == PH_OVER)  draw_banner(d, "GAME OVER");
        else if (m_phase == PH_CLEAR) draw_banner(d, "LEVEL CLEAR");
        if (m_phase == PH_DYING) { draw_dying(d); return; }
        draw_actors(d);
        draw_float_score(d);
    }

private:
    // ---- 状态 ---------------------------------------------------------------
    Engine* m_e = nullptr;
    struct Images {
        const Image* muncher[4][2] = {};
        const Image* ghost[4][2]   = {};
        const Image* scared[2]     = {};
        const Image* eyes[4]       = {};
        const Image* dot = nullptr;
        const Image* pellet[2] = {};
        const Image* fruit[3]  = {};
    } m_img;

    uint8_t m_left[kMazeRows][kMazeCols] = {};    // 每格还剩什么
    int     m_dotsLeft = 0;
    int     m_eaten = 0;

    Actor   m_player;
    Ghost   m_ghost[4];
    int     m_phase = PH_READY;
    float   m_phaseT = 0, m_time = 0, m_animT = 0;

    int     m_score = 0, m_high = 0, m_lives = kStartLives, m_level = 1;
    bool    m_extraGiven = false;
    int     m_nextExtra = kExtraAt;

    int     m_mode = MD_SCATTER;
    float   m_modeT = 0;
    int     m_frightChain = 0;                    // 本次大力丸连吃计数
    float   m_frightT = 0;
    float   m_frightMax = 6.0f;

    float   m_fruitT = 0;                          // >0 表示水果在场
    int     m_fruitShown = 0;                      // 已经出现过几次(70/170 各一次)
    bool    m_fruitEaten = false;

    float   m_deathT = 0;
    int     m_flash = 0;
    Fx      m_fx;

    // ---- 关卡/复位 ----------------------------------------------------------
    void start_level(Engine& e, bool full) {
        for (int r = 0; r < kMazeRows; ++r) {
            for (int c = 0; c < kMazeCols; ++c) {
                const char t = kMaze[r][c];
                m_left[r][c] = (t == '.') ? T_DOT : (t == 'o') ? T_POWER : T_EMPTY;
            }
        }
        m_dotsLeft = kDotCount + kPowerCount;
        m_eaten = 0;
        m_fruitT = 0;
        m_fruitShown = 0;
        m_fruitEaten = false;
        if (full) m_high = m_score > m_high ? m_score : m_high;
        reset_round(e);
    }

    void reset_round(Engine& e) {
        m_player.x = tile_cx(tile_cx_col(kStartCol));
        m_player.y = tile_cy(kStartRow);
        m_player.dir = DIR_L;
        m_player.want = DIR_NONE;
        m_player.speed = kSpeedPlayer;

        // 追兵难度随关数走
        const float gspd = kSpeedGhost + (float)(m_level - 1) * 3.0f;
        const int   rel[4] = { 0, 6, 20, 46 };          // 吃豆数
        const float relT[4] = { 0.0f, 1.2f, 3.0f, 5.0f }; // 或者等这么久
        for (int i = 0; i < 4; ++i) {
            Ghost& g = m_ghost[i];
            g.id = i;
            g.state = (i == 0) ? GH_LEAVING : GH_HOME;
            g.a.x = tile_cx(kHomeCol[i]);
            g.a.y = tile_cy(kHomeRow[i]);
            g.a.dir = (i == 0) ? DIR_L : (i & 1) ? DIR_U : DIR_D;
            g.a.speed = gspd;
            g.releaseDots = rel[i];
            g.releaseWait = relT[i] + (float)(m_level - 1) * 0.4f;
            g.timer = 0;
            g.quitState = 0;
        }
        m_frightT = 0;
        m_frightChain = 0;
        m_mode = MD_SCATTER;
        m_modeT = 0;
        m_phase = PH_READY;
        m_phaseT = 0;
        m_deathT = 0;
        m_animT = 0;
        play_bgm(e, false);
        e.audio.play_sfx(e.snd("sfx_ready"), 1.0f);
    }

    void play_bgm(Engine& e, bool hot) {
        const char* n = hot ? "bgm_siren_hot" : "bgm_siren";
        if (m_bgm == n) return;
        m_bgm = n;
        e.audio.play_bgm(e.snd(n), 1.0f);
    }
    const char* m_bgm = nullptr;

    // ---- 几何 ---------------------------------------------------------------
    static int tile_cx_col(int c) { return c; }     // 只是给上面那行读起来顺一点
    static float tile_cx(int c) { return kMazeX + c * kTile + kTile / 2.0f; }
    static float tile_cy(int r) { return kMazeY + r * kTile + kTile / 2.0f; }
    static int col_of(float x) { return (int)std::floor((x - kMazeX) / kTile); }
    static int row_of(float y) { return (int)std::floor((y - kMazeY) / kTile); }

    // 玩家能不能走这一格(门不行)
    static bool player_can(int c, int r) {
        if (maze_wall(c, r) || maze_door(c, r)) return false;
        return true;
    }
    // 追兵: 出门/回家时可以穿门
    static bool ghost_can(int c, int r, bool door) {
        if (maze_wall(c, r)) return false;
        if (maze_door(c, r) && !door) return false;
        return true;
    }

    // ---- 移动 ---------------------------------------------------------------
    // 隧道环绕
    static void wrap_tunnel(Actor& a) {
        const float left = kMazeX - kTile / 2.0f, right = kMazeX + kMazeW + kTile / 2.0f;
        if (a.x < left)  a.x += (float)kMazeW;
        if (a.x > right) a.x -= (float)kMazeW;
    }

    // 走一个 actor(玩家/追兵共用)。
    //
    // 这个函数踩过两个坑, 都写在这儿免得以后再踩:
    //
    //  ① "到格心的容差 = speed*dt" + "到了就吸附到格心": 每帧位移正好也是 speed*dt,
    //     于是每帧都判定"到了格心"并被吸回去 —— **一步也走不出去**。
    //  ② 改成"每格只决策一次"之后: 撞墙停在格心时不会再决策, 再按方向键也不理你
    //     —— **一撞墙就永久卡死**(用户报的就是这个)。
    //
    // 现在的写法把两件事拆开:
    //   · **决策**: 只要人在格心上, 每帧都做(转向/追兵选路)。转向只在 want 可走时才生效,
    //     想转的方向不可走时不消费 want(所以"预输入"能保持到下一个格心)。
    //   · **移动**: 每帧最多走到下一个格心(room), 永远不过头; 前方是墙就停在格心。
    //     因为停下时"没过格心", 下一帧依旧在格心上 —— 决策照样发生。
    void step_actor(Actor& a, float dt, bool can_door, Ghost* ghost) {
        const float step = (a.speed > 0.0f) ? a.speed * dt : 0.0f;
        const int   c = col_of(a.x), r = row_of(a.y);
        const float cx = tile_cx(c), cy = tile_cy(r);
        const bool  horiz = (a.dir == DIR_R || a.dir == DIR_L);

        // 沿行进轴, 从当前格心往前的距离(在格心上 = 0)
        float along = horiz ? (a.x - cx) : (a.y - cy);
        if ((horiz && a.dir == DIR_L) || (!horiz && a.dir == DIR_U)) along = -along;

        if (along > -0.05f && along < 0.05f) {           // 就在格心上
            a.x = cx;
            a.y = cy;
            along = 0.0f;
            if (ghost != nullptr) {
                ghost_think(*ghost, can_door);
            } else if (a.want != DIR_NONE && a.want != a.dir &&
                       ghost_can(c + kDX[a.want], r + kDY[a.want], can_door)) {
                a.dir = a.want;
                a.want = DIR_NONE;
            }
            if (!ghost_can(c + kDX[a.dir], r + kDY[a.dir], can_door)) {
                wrap_tunnel(a);
                return;                                  // 前方是墙: 停在格心(下一帧还会来决策)
            }
        }

        // 本帧最多走到下一个格心
        const float room = (along >= 0.0f) ? ((float)kTile - along) : (-along);
        const float s = (step < room) ? step : room;
        a.x += kDX[a.dir] * s;
        a.y += kDY[a.dir] * s;
        wrap_tunnel(a);
    }

    // ---- 追兵决策 -----------------------------------------------------------
    void ghost_target(const Ghost& g, int& tc, int& tr) const {
        const int pc = col_of(m_player.x), pr = row_of(m_player.y);
        const int pd = m_player.dir;
        switch (g.id) {
        case 0:                                   // 直线追你
            tc = pc; tr = pr;
            break;
        case 1:                                   // 抄你前路四格
            tc = pc + kDX[pd] * 4; tr = pr + kDY[pd] * 4;
            break;
        case 2: {                                 // 把"前锋点"按头号追兵的位置反折
            const int ax = pc + kDX[pd] * 2, ay = pr + kDY[pd] * 2;
            tc = ax * 2 - col_of(m_ghost[0].a.x);
            tr = ay * 2 - row_of(m_ghost[0].a.y);
            break;
        }
        default: {                                // 离得远就追, 贴脸了就回自己那个角
            const int d = std::abs(pc - col_of(g.a.x)) + std::abs(pr - row_of(g.a.y));
            if (d > 8) { tc = pc; tr = pr; }
            else       { tc = kScatterCol[3]; tr = kScatterRow[3]; }
            break;
        }
        }
    }

    void ghost_think(Ghost& g, bool can_door) {
        const int c = col_of(g.a.x), r = row_of(g.a.y);
        // (调用点就在格心, 不用再自己判位置)
        g.a.x = tile_cx(c);
        g.a.y = tile_cy(r);

        int tc = 0, tr = 0;
        if (g.state == GH_SCARED) {                // 乱跑
            int opts[4], n = 0;
            for (int d = 0; d < 4; ++d) {
                if (d == rev_dir(g.a.dir)) continue;
                if (!ghost_can(c + kDX[d], r + kDY[d], can_door)) continue;
                opts[n++] = d;
            }
            if (n == 0) g.a.dir = rev_dir(g.a.dir);
            else        g.a.dir = opts[g_rnd.range(0, n - 1)];
            return;
        }
        if (g.state == GH_EATEN) {                 // 目标: 鬼屋
            tc = kDoorCol; tr = kDoorRow + 1;
        } else if (m_mode == MD_SCATTER) {
            tc = kScatterCol[g.id]; tr = kScatterRow[g.id];
        } else {
            ghost_target(g, tc, tr);
        }

        // 四选一: 不许掉头(没得选才掉头), 选离目标最近的那格
        int best = -1;
        long bestD = -1;
        for (int d = 0; d < 4; ++d) {
            if (d == rev_dir(g.a.dir)) continue;
            const int nc = c + kDX[d], nr = r + kDY[d];
            if (!ghost_can(nc, nr, can_door)) continue;
            const long dx = (long)nc - tc, dy = (long)nr - tr;
            const long dist = dx * dx + dy * dy;
            if (best < 0 || dist < bestD) { best = d; bestD = dist; }
        }
        if (best < 0) g.a.dir = rev_dir(g.a.dir);
        else          g.a.dir = best;
    }

    // ---- 各阶段 -------------------------------------------------------------
    void update_ready(Engine& e, float dt) {
        m_phaseT += dt;
        m_animT += dt;
        if (m_phaseT >= kReadyTime) { m_phase = PH_PLAY; m_phaseT = 0; }
    }

    void update_play(Engine& e, float dt) {
        const PadState& p = e.input;
        m_animT += dt;
        m_phaseT += dt;

        // --- 玩家输入: 方向键随时可以"预输入"转向 ---
        if (is_pressed(p, Button::Left))  m_player.want = DIR_L;
        if (is_pressed(p, Button::Right)) m_player.want = DIR_R;
        if (is_pressed(p, Button::Up))    m_player.want = DIR_U;
        if (is_pressed(p, Button::Down))  m_player.want = DIR_D;
        // 按住不放也持续转向(手感更跟手)
        if (is_held(p, Button::Left))  m_player.want = DIR_L;
        if (is_held(p, Button::Right)) m_player.want = DIR_R;
        if (is_held(p, Button::Up))    m_player.want = DIR_U;
        if (is_held(p, Button::Down))  m_player.want = DIR_D;

        const bool in_tunnel = (row_of(m_player.y) == kTunnelRow &&
                                (col_of(m_player.x) <= 1 || col_of(m_player.x) >= kMazeCols - 2));
        m_player.speed = in_tunnel ? kSpeedTunnel : kSpeedPlayer;
        step_actor(m_player, dt, false, nullptr);

        // --- 吃豆 ---
        eat_pellet(e);

        // --- 追兵 ---
        update_ghosts(e, dt);

        // --- 大力丸计时 ---
        if (m_frightT > 0) {
            m_frightT -= dt;
            if (m_frightT <= 0) {
                m_frightT = 0;
                m_frightChain = 0;
                for (Ghost& g : m_ghost) if (g.state == GH_SCARED) g.state = GH_NORMAL;
                play_bgm(e, false);
            }
        }

        // --- 巡逻/追击 交替 ---
        m_modeT += dt;
        const float scatterLen = clampv(7.0f - 0.5f * (float)(m_level - 1), 4.0f, 7.0f);
        const float chaseLen   = 20.0f;
        if (m_mode == MD_SCATTER && m_modeT > scatterLen) { m_mode = MD_CHASE; m_modeT = 0; }
        else if (m_mode == MD_CHASE && m_modeT > chaseLen) { m_mode = MD_SCATTER; m_modeT = 0; }

        // --- 水果 ---
        if (m_fruitT > 0) {
            m_fruitT -= dt;
            if (m_fruitT <= 0) m_fruitT = 0;
        } else if (m_fruitShown == 0 && m_eaten >= 70) {
            m_fruitT = kFruitShow; m_fruitShown = 1;
        } else if (m_fruitShown == 1 && m_eaten >= 170) {
            m_fruitT = kFruitShow; m_fruitShown = 2;
        }

        // --- 过关 ---
        if (m_dotsLeft <= 0) {
            m_phase = PH_CLEAR;
            m_phaseT = 0;
            m_flash = 0;
            e.audio.stop_bgm();
            m_bgm = nullptr;
            e.audio.play_sfx(e.snd("sfx_level"), 1.0f);
        }
    }

    void eat_pellet(Engine& e) {
        const int c = col_of(m_player.x), r = row_of(m_player.y);
        if (c < 0 || c >= kMazeCols || r < 0 || r >= kMazeRows) return;
        if (std::fabs(m_player.x - tile_cx(c)) > 5.0f) return;
        if (std::fabs(m_player.y - tile_cy(r)) > 5.0f) return;

        const int t = m_left[r][c];
        if (t == T_DOT) {
            m_left[r][c] = T_EMPTY;
            m_dotsLeft--;
            m_eaten++;
            add_score(e, 10);
            e.audio.play_sfx(e.snd(m_chompToggle ? "sfx_chomp0" : "sfx_chomp1"), 0.9f);
            m_chompToggle = !m_chompToggle;
        } else if (t == T_POWER) {
            m_left[r][c] = T_EMPTY;
            m_dotsLeft--;
            m_eaten++;
            add_score(e, 50);
            m_frightChain = 0;
            m_frightMax = clampv(7.0f - 0.6f * (float)m_level, 2.0f, 7.0f);
            m_frightT = m_frightMax;
            for (Ghost& g : m_ghost) {
                if (g.state == GH_NORMAL) { g.state = GH_SCARED; g.a.dir = rev_dir(g.a.dir); }
            }
            play_bgm(e, true);
            e.audio.play_sfx(e.snd("sfx_pellet"), 1.0f);
        } else if (m_fruitT > 0 && c == kFruitCol && r == kFruitRow) {
            const int idx = clampv(m_level - 1, 0, 2);
            const int pts = kFruitScore[idx];
            m_fruitT = 0;
            m_fruitEaten = true;
            add_score(e, pts);
            m_fx.x = tile_cx(kFruitCol);
            m_fx.y = tile_cy(kFruitRow);
            m_fx.score = pts;
            m_fx.life = 0.9f;
            e.audio.play_sfx(e.snd("sfx_fruit"), 1.0f);
        }
    }
    bool m_chompToggle = false;

    void update_ghosts(Engine& e, float dt) {
        const float base = kSpeedGhost + (float)(m_level - 1) * 3.0f;
        for (Ghost& g : m_ghost) {
            switch (g.state) {
            case GH_HOME: {
                g.timer += dt;
                // 屋里上下蹭
                const float cy = tile_cy(kHomeRow[g.id]);
                g.a.y += (g.a.dir == DIR_U ? -1.0f : 1.0f) * 26.0f * dt;
                if (g.a.y < cy - 3.0f) g.a.dir = DIR_D;
                if (g.a.y > cy + 3.0f) g.a.dir = DIR_U;
                if (m_eaten >= g.releaseDots || g.timer >= g.releaseWait) {
                    g.state = GH_LEAVING;
                    g.quitState = 0;
                }
                break;
            }
            case GH_LEAVING: {
                // 先横移到门口那一列, 再往上出去
                const float door_x = tile_cx(kDoorCol);
                g.a.speed = kSpeedGhost * 0.8f;
                if (g.quitState == 0) {
                    const float dx = door_x - g.a.x;
                    if (std::fabs(dx) <= g.a.speed * dt) {
                        g.a.x = door_x;
                        g.a.y = tile_cy(kDoorRow + 1);
                        g.quitState = 1;
                    } else {
                        g.a.x += (dx > 0 ? 1.0f : -1.0f) * g.a.speed * dt;
                    }
                } else {
                    g.a.y -= g.a.speed * dt;
                    if (g.a.y <= tile_cy(kDoorRow - 1)) {   // 到门外那一行
                        g.a.y = tile_cy(kDoorRow - 1);
                        g.a.dir = (g.a.x < tile_cx(kMazeCols / 2)) ? DIR_L : DIR_R;
                        g.state = (m_frightT > 0) ? GH_SCARED : GH_NORMAL;
                    }
                }
                break;
            }
            case GH_NORMAL:
            case GH_SCARED:
                g.a.speed = (g.state == GH_SCARED) ? kSpeedScared
                          : (row_of(g.a.y) == kTunnelRow ? base * 0.7f : base);
                step_actor(g.a, dt, false, &g);
                break;
            case GH_EATEN:
                g.a.speed = kSpeedEaten;
                step_actor(g.a, dt, true, &g);
                if (std::fabs(g.a.x - tile_cx(kHomeCol[g.id])) < 2.0f &&
                    std::fabs(g.a.y - tile_cy(kHomeRow[g.id])) < 2.0f) {
                    g.state = GH_HOME;
                    g.timer = 0;
                    g.a.dir = DIR_U;
                }
                break;
            }
        }

        // --- 抓到玩家 ---
        for (Ghost& g : m_ghost) {
            if (g.state == GH_EATEN) continue;
            if (std::fabs(g.a.x - m_player.x) > 7.0f) continue;
            if (std::fabs(g.a.y - m_player.y) > 7.0f) continue;
            if (g.state == GH_SCARED) {
                g.state = GH_EATEN;                     // 反吃
                m_frightChain = m_frightChain < 4 ? m_frightChain + 1 : 4;
                const int pts = 200 << (m_frightChain - 1);
                add_score(e, pts);
                m_fx.x = (int)g.a.x;
                m_fx.y = (int)g.a.y;
                m_fx.score = pts;
                m_fx.life = 0.7f;
                e.audio.play_sfx(e.snd("sfx_eatghost"), 1.0f);
            } else if (g.state != GH_HOME) {
                m_phase = PH_DYING;                     // 被抓
                m_phaseT = 0;
                m_deathT = 0;
                m_lives--;
                e.audio.stop_bgm();
                m_bgm = nullptr;
                e.audio.play_sfx(e.snd("sfx_death"), 1.0f);
                return;
            }
        }
    }

    void update_dying(Engine& e, float dt) {
        m_deathT += dt;
        if (m_deathT < kDyingTime) return;
        if (m_lives <= 0) {
            m_phase = PH_OVER;
            m_phaseT = 0;
            m_high = m_score > m_high ? m_score : m_high;
            return;
        }
        reset_round(e);
    }

    void update_clear(Engine& e, float dt) {
        m_phaseT += dt;
        m_flash = (int)(m_phaseT * 4.0f) & 1;
        if (m_phaseT < kClearTime) return;
        m_level++;
        start_level(e, true);
    }

    void update_over(Engine& e, float dt) {
        m_phaseT += dt;
        if (m_phaseT < 1.0f) return;
        if (is_pressed(e.input, Button::A) || is_pressed(e.input, Button::B) ||
            is_pressed(e.input, Button::Start)) {
            m_score = 0;
            m_lives = kStartLives;
            m_level = 1;
            m_extraGiven = false;
            m_nextExtra = kExtraAt;
            start_level(e, true);
        }
    }

    void add_score(Engine& e, int pts) {
        m_score += pts;
        if (m_score > m_high) m_high = m_score;
        if (!m_extraGiven && m_score >= m_nextExtra) {
            m_extraGiven = true;
            m_lives++;
            e.audio.play_sfx(e.snd("sfx_life"), 1.0f);
        }
    }

    // ---- 绘制 ---------------------------------------------------------------
    void draw_maze(Display& d) {
        const Color edge = m_flash ? kWallFlash : kWallEdge;
        const Color body = m_flash ? kWallFlash : kWallBody;
        for (int r = 0; r < kMazeRows; ++r) {
            for (int c = 0; c < kMazeCols; ++c) {
                const int x = kMazeX + c * kTile, y = kMazeY + r * kTile;
                if (kMaze[r][c] == '#') {
                    d.fill_rect(x, y, kTile, kTile, body);
                    // 只在朝向走廊的那一侧画亮边 -> 看起来是"迷宫轮廓"
                    if (!maze_wall(c, r - 1)) d.fill_rect(x, y, kTile, 2, edge);
                    if (!maze_wall(c, r + 1)) d.fill_rect(x, y + kTile - 2, kTile, 2, edge);
                    if (!maze_wall(c - 1, r)) d.fill_rect(x, y, 2, kTile, edge);
                    if (!maze_wall(c + 1, r)) d.fill_rect(x + kTile - 2, y, 2, kTile, edge);
                } else {
                    d.fill_rect(x, y, kTile, kTile, kBg);
                    if (maze_door(c, r)) d.fill_rect(x, y + 5, kTile, 3, kWallEdge);
                }
            }
        }
    }

    void draw_pellets(Display& d) {
        const int blink = ((int)(m_animT * 5.0f) & 1);
        for (int r = 0; r < kMazeRows; ++r) {
            for (int c = 0; c < kMazeCols; ++c) {
                const int t = m_left[r][c];
                if (t == T_EMPTY) continue;
                const int cx = (int)tile_cx(c), cy = (int)tile_cy(r);
                if (t == T_DOT) {
                    // 吃的是键帽(小): 用精灵而不是两个点, 主题才统一
                    if (m_img.dot != nullptr) blit(m_img.dot, cx, cy);
                } else {
                    const Image* p = m_img.pellet[blink];
                    if (p != nullptr) blit(p, cx, cy);
                }
            }
        }
        if (m_fruitT > 0) {
            const int idx = clampv(m_level - 1, 0, 2);
            const Image* f = m_img.fruit[idx];
            if (f != nullptr) blit(f, (int)tile_cx(kFruitCol), (int)tile_cy(kFruitRow));
        }
    }

    void draw_actors(Display& d) {
        // 追兵(眼睛状态只剩眼睛)
        for (const Ghost& g : m_ghost) {
            if (g.state == GH_EATEN) {
                const Image* im = m_img.eyes[g.a.dir & 3];
                if (im != nullptr) blit(im, (int)g.a.x, (int)g.a.y);
                continue;
            }
            const Image* im = nullptr;
            if (g.state == GH_SCARED) {
                const bool blink = (m_frightT < 2.0f) && (((int)(m_animT * 8.0f) & 1) != 0);
                im = blink ? m_img.scared[1] : m_img.scared[0];
            } else {
                im = m_img.ghost[g.id][(int)(m_animT * 8.0f + g.id) & 1];
            }
            if (im != nullptr) blit(im, (int)g.a.x, (int)g.a.y);
        }
        // 玩家: 两张帧交替张合
        const Image* pm = m_img.muncher[m_player.dir & 3][(int)(m_animT * 12.0f) & 1];
        if (pm != nullptr) blit(pm, (int)m_player.x, (int)m_player.y);
    }

    void draw_dying(Display& d) {
        // 先转圈, 再缩小消失(精灵整体缩到 1/4)
        const float t = m_deathT;
        if (t < 1.1f) {
            const int dir = ((int)(t / 0.13f)) & 3;
            const Image* im = m_img.muncher[dir][(int)(t * 14.0f) & 1];
            if (im != nullptr) blit(im, (int)m_player.x, (int)m_player.y);
        } else {
            const float k = 1.0f - (t - 1.1f) / 0.8f;          // 1 -> 0
            if (k > 0.05f) {
                const int den = 4;
                const int num = clampv((int)(k * den + 0.5f), 1, den);
                const Image* im = m_img.muncher[m_player.dir & 3][0];
                if (im != nullptr) blit_s(im, (int)m_player.x, (int)m_player.y, num, den);
            }
        }
    }

    void draw_float_score(Display& d) {
        if (m_fx.life <= 0) return;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", m_fx.score);
        const int w = (int)strlen(buf) * 6;
        d.text(m_fx.x - w / 2, m_fx.y - 10, buf, kAccent, 1);
    }

    void draw_hud(Display& d) {
        d.fill_rect(0, 0, kScrW, kHudH, rgb565(6, 8, 16));
        d.fill_rect(0, kHudH - 1, kScrW, 1, rgb565(30, 44, 78));

        char buf[32];
        snprintf(buf, sizeof(buf), "%d", m_score);
        d.text(6, 5, "SCORE", kDimCol, 1);
        d.text(42, 5, buf, kTextCol, 1);

        snprintf(buf, sizeof(buf), "%d", m_high);
        d.text(140, 5, "HIGH", kDimCol, 1);
        d.text(170, 5, buf, kTextCol, 1);

        snprintf(buf, sizeof(buf), "LV %d", m_level);
        d.text(kScrW - 122, 5, buf, kAccent, 1);

        // 命数: 用小精灵头像
        for (int i = 0; i < m_lives - 1 && i < 5; ++i) {
            const Image* im = m_img.muncher[DIR_R][0];
            if (im != nullptr) blit(im, kScrW - 96 + i * 15, kHudH / 2);
        }
        // 当前关的水果
        const Image* f = m_img.fruit[clampv(m_level - 1, 0, 2)];
        if (f != nullptr) blit(f, kScrW - 20, kHudH / 2);
    }

    void draw_banner(Display& d, const char* s) {
        const int scale = 2;
        const int tw = (int)strlen(s) * 6 * scale;
        const int y = kMazeY + kMazeH / 2 - 9;
        d.fill_rect(0, y, kScrW, 20, rgb565(8, 10, 20));
        d.text((kScrW - tw) / 2, y + 3, s, rgb565(250, 214, 92), scale);
    }

    // ---- 贴图(认 4bpp 索引图, 索引 0 = 透明) --------------------------------
    static inline int pix_at(const Image& im, int x, int y) {
        if (im.pal != nullptr) {
            const size_t i = (size_t)y * im.w + (size_t)x;
            const uint8_t b = im.data[i >> 1];
            const uint8_t v = (i & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)(b >> 4);
            return (v == 0) ? -1 : (int)im.pal[v];
        }
        const uint8_t* p = im.data + ((size_t)y * im.w + (size_t)x) * 2;
        const uint16_t c = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
        return (c == engine::kColorMagenta) ? -1 : (int)c;
    }

    void blit(const Image* im, int cx, int cy) {
        if (im == nullptr || im->data == nullptr) return;
        const int x0 = cx - im->w / 2, y0 = cy - im->h / 2;
        for (int y = 0; y < im->h; ++y) {
            const int dy = y0 + y;
            if (dy < 0 || dy >= kScrH) continue;
            for (int x = 0; x < im->w; ++x) {
                const int c = pix_at(*im, x, y);
                if (c < 0) continue;
                const int dx = x0 + x;
                if (dx < 0 || dx >= kScrW) continue;
                m_e->display.set_pixel(dx, dy, (uint16_t)c);
            }
        }
    }

    // 按比例缩放贴图(死亡动画往小缩)
    void blit_s(const Image* im, int cx, int cy, int num, int den) {
        if (im == nullptr || im->data == nullptr) return;
        const int w = im->w * num / den, h = im->h * num / den;
        if (w <= 0 || h <= 0) return;
        const int x0 = cx - w / 2, y0 = cy - h / 2;
        for (int y = 0; y < h; ++y) {
            const int sy = y * den / num;
            const int dy = y0 + y;
            if (dy < 0 || dy >= kScrH) continue;
            for (int x = 0; x < w; ++x) {
                const int c = pix_at(*im, x * den / num, sy);
                if (c < 0) continue;
                const int dx = x0 + x;
                if (dx < 0 || dx >= kScrW) continue;
                m_e->display.set_pixel(dx, dy, (uint16_t)c);
            }
        }
    }
};

} // namespace

engine::Game* make_keychase() { return new KeyChaseGame(); }

} // namespace games
