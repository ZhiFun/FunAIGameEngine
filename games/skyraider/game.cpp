// ============================================================================
// SKY RAIDER —— FunAIGameEngine 示例游戏(横版飞行射击)
// 演示: 素材加载 / 标准输入 / 音乐+音效混播 / 逐帧像素绘制。
// ============================================================================
// ============================================================================
// SKY RAIDER —— FunAIGameEngine 示例游戏(横版飞行射击)
//
// 玩法与固件里的 src/game/ShooterGame.cpp 对齐(同一套设计), 但用引擎 API 重写:
//   · 5 条航道 + 自由横移(带加速/阻尼手感)
//   · 3 种杂兵(直飞 / 蛇形 / 炮台) + 小 Boss + 大 Boss
//   · 火力 1..4 级(每级弹道不同) / 追踪导弹 / 蓄力激光(B 释放, 穿透+震屏)
//   · 掉落强化(P 火力 / M 导弹 / S 加命) + 击杀攒激光槽
//   · 爆炸 / 火花 / 震屏 / 三层视差星空 / 渐变天幕
//   · 顶部 HUD 条(分数 / 命数 / 火力 / 激光槽) + Boss 血条 + 阶段覆盖层
//
// 按键: ←→ 横移   ↑↓ 换道   A 开火(按住)   B 释放激光   Enter 开始/暂停
// 分辨率一律从 Display 取, 不写死 428x142。
// ============================================================================
#include "games/skyraider/game.h"
#include "engine/Engine.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace engine;

namespace {

// ---------------------------------------------------------------------------
// 几何 / 上限
// ---------------------------------------------------------------------------
constexpr int kMaxHRes   = 256;      // 画面高度上限(放渐变表用)
constexpr int kHudH      = 14;       // 顶部 HUD 条高度
constexpr int kLanes     = 5;        // 航道数
constexpr int kLaserFull = 10;       // 击杀数攒满激光
constexpr int kMaxPBul   = 72;
constexpr int kMaxEBul   = 64;
constexpr int kMaxEnemy  = 20;
constexpr int kMaxPick   = 8;
constexpr int kMaxBoom   = 24;
constexpr int kMaxSpark  = 64;
constexpr int kMaxStars  = 96;

enum EnemyKind : uint8_t { E_DRONE, E_WEAVER, E_TURRET, E_MINI, E_BOSS };

// 阶段(覆盖层用)
static const uint8_t PH_TITLE = 0, PH_PLAY = 1, PH_PAUSE = 2, PH_CLEAR = 3, PH_OVER = 4;

inline Color col(uint8_t r, uint8_t g, uint8_t b) { return rgb565(r, g, b); }

// ---------------------------------------------------------------------------
// 字符画精灵('.' = 透明) —— 与固件版同一套造型, 保证两个平台观感一致
// ---------------------------------------------------------------------------
const char* const kShip0[11] = {      // 我方战机(13x11) 尾焰长
    "......W......",
    ".....WWW.....",
    "....WWWWW....",
    "....CCWCC....",
    "...CCCCCCC...",
    "..CCCCCCCCC..",
    ".CCCCCCCCCCC.",
    "CCCCCCCCCCCCC",
    ".BB.CCCCC.BB.",
    "....O...O....",
    "....O...O....",
};
const char* const kShip1[11] = {      // 尾焰短(帧动画)
    "......W......",
    ".....WWW.....",
    "....WWWWW....",
    "....CCWCC....",
    "...CCCCCCC...",
    "..CCCCCCCCC..",
    ".CCCCCCCCCCC.",
    "CCCCCCCCCCCCC",
    ".BB.CCCCC.BB.",
    "....O...O....",
    "....o...o....",
};
const char* const kDroneArt[9] = {    // 杂兵 1: 直飞(11x9)
    "...RRRRR...",
    "..RRRRRRR..",
    ".RRKRRRKRR.",
    ".RRRRRRRRR.",
    "RRRRRRRRRRR",
    "RRrYYYYYrRR",
    ".RRrRRRrRR.",
    "..R.R.R.R..",
    "...R...R...",
};
const char* const kWeaverArt[9] = {
    "P...........P",
    "PP.........PP",
    ".PPPPPPPPPPP.",
    "..PPKKKKKPP..",
    "...PPPPPPP...",
    "....PPPPP....",
    ".....PPP.....",
    "......P......",
    ".............",
};
const char* const kTurretArt[11] = {
    "....GGGGG....",
    "..GGGGGGGGG..",
    ".GGgggggggGG.",
    ".GGgRRRRRgGG.",
    "GGGgRRRRRgGGG",
    "GGGgggggggGGG",
    "GGGGGGGGGGGGG",
    ".GGGGGGGGGGG.",
    "..GG.GGG.GG..",
    "...GGGGGGG...",
    "....GG.GG....",
};

Color char_color(char c) {
    switch (c) {
        case 'C': return col(80, 233, 255);    // 亮青(我方机身)
        case 'c': return col(26, 126, 156);
        case 'W': return col(255, 255, 255);
        case 'B': return col(74, 108, 255);
        case 'b': return col(34, 48, 122);
        case 'P': return col(176, 112, 224);
        case 'p': return col(80, 40, 120);
        case 'G': return col(154, 166, 180);
        case 'g': return col(74, 86, 102);
        case 'R': return col(224, 80, 76);
        case 'r': return col(140, 32, 32);
        case 'O': return col(255, 160, 64);
        case 'o': return col(255, 96, 32);
        case 'Y': return col(255, 224, 64);
        case 'M': return col(255, 96, 192);
        case 'S': return col(154, 232, 180);
        case 'K': return col(16, 16, 24);
        default:  return 0;
    }
}

// ---------------------------------------------------------------------------
// 实体
// ---------------------------------------------------------------------------
struct Star    { float x = 0, y = 0; uint8_t layer = 0; };
struct PBullet { float x = 0, y = 0, vx = 0, vy = 0; int dmg = 1, r = 2; bool homing = false, alive = false; };
struct EBullet { float x = 0, y = 0, vx = 0, vy = 0; int r = 2; bool alive = false; };
struct Enemy {
    bool    alive = false;
    uint8_t kind = E_DRONE;
    float   x = 0, y = 0, baseY = 0, vx = 0;
    float   t = 0, fireCd = 0, hitFlash = 0;
    int     hp = 1, hpMax = 1, score = 0, r = 7;
};
struct Pickup { float x = 0, y = 0, t = 0; uint8_t kind = 0; bool alive = false; };
struct Boom   { float x = 0, y = 0, t = 0, dur = 0, r = 0; uint8_t big = 0; bool alive = false; };
struct Spark  { float x = 0, y = 0, vx = 0, vy = 0, t = 0; uint8_t kind = 0; bool alive = false; };

// 关卡时间轴(第 1 关)
struct StageEvent { float t; uint8_t kind; int8_t lane; uint8_t count; int8_t laneStep; };
const StageEvent kStage1[] = {
    {  2.0f, E_DRONE,  2, 3,  0 },   // 开局: 中路 3 架直飞
    {  5.5f, E_DRONE,  1, 3,  0 },
    {  8.5f, E_DRONE,  3, 3,  0 },
    { 11.5f, E_WEAVER, 0, 4,  1 },   // 蛇形队: 从上往下铺
    { 16.0f, E_DRONE,  4, 4,  0 },
    { 19.5f, E_TURRET, 2, 2,  2 },   // 炮台(三向弹)
    { 24.0f, E_WEAVER, 4, 5, -1 },
    { 29.0f, E_DRONE,  0, 4,  1 },
    { 33.0f, E_MINI,   2, 1,  0 },   // —— 小 Boss
    { 46.0f, E_DRONE,  1, 4,  1 },
    { 50.0f, E_TURRET, 3, 2, -2 },
    { 55.0f, E_WEAVER, 0, 6,  1 },
    { 61.0f, E_DRONE,  4, 5, -1 },
    { 66.0f, E_BOSS,   2, 1,  0 },   // —— 大 Boss
};
static const StageEvent kStage2[] = {
    {  1.8f, E_DRONE,  0, 4,  1 },
    {  5.0f, E_WEAVER, 4, 4, -1 },
    {  8.5f, E_TURRET, 1, 2,  3 },
    { 12.0f, E_DRONE,  3, 5, -1 },
    { 16.0f, E_WEAVER, 2, 5,  1 },
    { 20.0f, E_TURRET, 4, 3, -2 },
    { 25.0f, E_MINI,   2, 1,  0 },
    { 37.0f, E_DRONE,  1, 5,  1 },
    { 41.0f, E_WEAVER, 3, 6, -1 },
    { 45.5f, E_TURRET, 0, 3,  2 },
    { 50.0f, E_BOSS,   2, 1,  0 },
};
static const StageEvent kStage3[] = {
    {  1.6f, E_WEAVER, 0, 5,  1 },
    {  5.0f, E_TURRET, 2, 3,  2 },
    {  9.0f, E_DRONE,  4, 6, -1 },
    { 13.0f, E_TURRET, 0, 3,  2 },
    { 17.0f, E_WEAVER, 2, 6,  1 },
    { 22.0f, E_MINI,   1, 1,  0 },
    { 30.0f, E_MINI,   3, 1,  0 },
    { 43.0f, E_DRONE,  0, 6,  1 },
    { 47.0f, E_TURRET, 3, 4, -2 },
    { 52.0f, E_WEAVER, 1, 7,  1 },
    { 57.0f, E_BOSS,   2, 1,  0 },
};
static const StageEvent kStage4[] = {
    {  1.5f, E_TURRET, 1, 3,  2 },
    {  5.0f, E_DRONE,  3, 6, -1 },
    {  9.0f, E_WEAVER, 0, 6,  1 },
    { 13.5f, E_TURRET, 3, 3, -2 },
    { 18.0f, E_MINI,   2, 1,  0 },
    { 26.0f, E_DRONE,  4, 7, -1 },
    { 30.0f, E_WEAVER, 1, 7,  1 },
    { 35.0f, E_TURRET, 0, 4,  2 },
    { 40.0f, E_MINI,   2, 1,  0 },
    { 50.0f, E_TURRET, 2, 4,  2 },
    { 55.0f, E_BOSS,   2, 1,  0 },
};
static const StageEvent kStage5[] = {
    {  1.4f, E_WEAVER, 4, 6, -1 },
    {  5.0f, E_TURRET, 0, 4,  2 },
    {  9.5f, E_DRONE,  2, 7,  1 },
    { 14.0f, E_MINI,   1, 1,  0 },
    { 22.0f, E_TURRET, 3, 4, -2 },
    { 27.0f, E_WEAVER, 0, 8,  1 },
    { 32.0f, E_MINI,   3, 1,  0 },
    { 41.0f, E_DRONE,  2, 8, -1 },
    { 46.0f, E_TURRET, 1, 5,  2 },
    { 52.0f, E_WEAVER, 4, 8, -1 },
    { 58.0f, E_BOSS,   2, 1,  0 },
};
static const StageEvent kStage6[] = {
    {  1.3f, E_TURRET, 2, 4,  2 },
    {  5.5f, E_WEAVER, 0, 7,  1 },
    { 10.0f, E_MINI,   1, 1,  0 },
    { 18.0f, E_DRONE,  4, 8, -1 },
    { 22.0f, E_MINI,   3, 1,  0 },
    { 30.0f, E_TURRET, 1, 5,  2 },
    { 35.0f, E_WEAVER, 3, 9, -1 },
    { 40.0f, E_MINI,   2, 1,  0 },
    { 49.0f, E_TURRET, 0, 6,  2 },
    { 54.0f, E_DRONE,  2, 9, -1 },
    { 59.0f, E_BOSS,   2, 1,  0 },
};

// 关卡定义: 波次 + 小/大 Boss 血量 + 敌人速度倍率
struct StageDef { const StageEvent* ev; uint8_t count; int miniHp; int bossHp; float speed; };
static const StageDef kStages[6] = {
    { kStage1, (uint8_t)(sizeof(kStage1) / sizeof(kStage1[0])),  70,  260, 1.00f },
    { kStage2, (uint8_t)(sizeof(kStage2) / sizeof(kStage2[0])), 100,  340, 1.10f },
    { kStage3, (uint8_t)(sizeof(kStage3) / sizeof(kStage3[0])), 140,  430, 1.20f },
    { kStage4, (uint8_t)(sizeof(kStage4) / sizeof(kStage4[0])), 190,  540, 1.32f },
    { kStage5, (uint8_t)(sizeof(kStage5) / sizeof(kStage5[0])), 250,  670, 1.44f },
    { kStage6, (uint8_t)(sizeof(kStage6) / sizeof(kStage6[0])), 320,  820, 1.58f },
};
static constexpr int kStageCount = (int)(sizeof(kStages) / sizeof(kStages[0]));

// 伪随机(自实现, 便于复现)
uint32_t g_rng = 0x1F2E3D4Cu;
inline uint32_t rnd_u32() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }
inline int   rnd_range(int n) { return (n > 0) ? (int)(rnd_u32() % (uint32_t)n) : 0; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------------------

class SkyRaider : public Game {
    // ---- 素材 ----
    const Image* imgPlayer = nullptr;
    const Image* imgEnemy  = nullptr;
    const Image* imgBullet = nullptr;
    const Image* uiLifePip = nullptr;
    const Image* uiTargetLock = nullptr;
    const Image* uiMissionFrame = nullptr;
    const Image* uiWarningPlate = nullptr;
    const Clip*  sndShoot = nullptr;
    const Clip*  sndBoom  = nullptr;
    const Clip*  bgmClip  = nullptr;
    Engine*      eng_     = nullptr;   // 供敌机/受击等深处回调放音效

    // ---- 画面 ----
    Display* d_ = nullptr;
    int W = 428, H = 142;
    int laneY[kLanes] = { 26, 48, 70, 92, 114 };
    Color sky_[kMaxHRes];
    int ox_ = 0, oy_ = 0;            // 震屏偏移

    // ---- 对象池 ----
    Star    stars[kMaxStars];
    PBullet pbul[kMaxPBul];
    EBullet ebul[kMaxEBul];
    Enemy   enemies[kMaxEnemy];
    Pickup  pickups[kMaxPick];
    Boom    booms[kMaxBoom];
    Spark   sparks[kMaxSpark];

    // ---- 玩家 ----
    bool  alive = true;
    float px = 60, py = 70, vx = 0;
    int   lane = 2;
    int   lives = 3, power = 1, missiles = 0, laserCharge = 0, score = 0;
    float fireCd = 0, missileCd = 0, invuln = 1.5f, specialT = 0;
    int   laneDir = 0;
    float laneRepeat = 0;

    // ---- 关卡 / 阶段 ----
    uint8_t phase = PH_TITLE;
    float stateT = 0, stageT = 0, shake = 0, bossDownT = 0;
    int   evIdx = 0;
    int   pendingLeft = 0, pendingSpawned = 0, pendingLane = 0, pendingStep = 0;
    uint8_t pendingKind = 0;
    float pendingGap = 0;
    bool  bossBusy = false, laserCueDone = false;

    // -----------------------------------------------------------------------
    // 绘制原语(自动叠加震屏偏移)
    // -----------------------------------------------------------------------
    void pset(int x, int y, Color c) { d_->set_pixel(x + ox_, y + oy_, c); }
    void frect(int x, int y, int w, int h, Color c) { d_->fill_rect(x + ox_, y + oy_, w, h, c); }
    void disc(int cx, int cy, int r, Color c) {
        if (r <= 0) { pset(cx, cy, c); return; }
        const int r2 = r * r;
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                if (x * x + y * y <= r2) pset(cx + x, cy + y, c);
    }
    void sprite(int sx, int sy, const char* const* rows, int n) {
        for (int y = 0; y < n; ++y) {
            const char* row = rows[y];
            for (int x = 0; row[x] != '\0'; ++x) {
                if (row[x] == '.') continue;
                pset(sx + x, sy + y, char_color(row[x]));
            }
        }
    }
    // 取一个像素: 调色板非空 = 4bpp 索引图(索引 0 透明), 否则 RGB565(品红 = 透明)。
    // 素材从 16bpp 改成 4bpp 后, 所有自己写像素循环的地方都得认两种格式。
    static inline int img_pixel(const Image& im, int x, int y) {
        if (im.pal != nullptr) {
            const size_t i = (size_t)y * im.w + (size_t)x;
            const uint8_t b = im.data[i >> 1];
            const uint8_t v = (i & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)(b >> 4);
            return (v == 0) ? -1 : (int)im.pal[v];
        }
        const uint8_t* p = im.data + ((size_t)y * im.w + (size_t)x) * 2;
        const uint16_t c = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
        return (c == kColorMagenta) ? -1 : (int)c;
    }
    // 贴图(自己实现, 为了支持震屏偏移 + 命中闪白 + 品红透明键)
    void blit(int x, int y, const Image* im, bool white = false) {
        if (im == nullptr || im->data == nullptr) return;
        for (int yy = 0; yy < im->h; ++yy) {
            for (int xx = 0; xx < im->w; ++xx) {
                const int c = img_pixel(*im, xx, yy);
                if (c < 0) continue;
                pset(x + xx, y + yy, white ? col(255, 255, 255) : (Color)c);
            }
        }
    }
    void blit_center(float cx, float cy, const Image* im, bool white = false) {
        if (im == nullptr) return;
        blit((int)cx - (int)im->w / 2, (int)cy - (int)im->h / 2, im, white);
    }

    // ---- 高清精灵素材(由 tools/gen_skyraider_hd.py 生成) ----
    const Image* sprShip[3] = { nullptr, nullptr, nullptr };   // 三帧尾焰
    const Image* sprDrone = nullptr;
    const Image* sprWeaver = nullptr;
    const Image* sprTurret = nullptr;
    const Image* sprBossSmall = nullptr;
    const Image* sprBossLarge = nullptr;
    const Image* sprPick[3] = { nullptr, nullptr, nullptr };   // P / M / S
    const Image* sprBul[3] = { nullptr, nullptr, nullptr };    // 火力 1-2 / 3 / 4
    const Image* sprBoom[3] = { nullptr, nullptr, nullptr };   // 小 / 中 / 大
    const Image* bgNebula = nullptr;
    const Image* bgPlanet = nullptr;
    const Image* hudFrame = nullptr;

    // 扩充音效(bgmClip/sndShoot/sndBoom 已在上面声明)
    const Clip*  sndHit = nullptr;
    const Clip*  sndBoomBig = nullptr;
    const Clip*  sndPlayerHit = nullptr;
    const Clip*  sndPick = nullptr;
    const Clip*  sndLife = nullptr;
    const Clip*  sndLaser = nullptr;
    const Clip*  sndMissile = nullptr;
    const Clip*  sndBossWarn = nullptr;
    const Clip*  sndClear = nullptr;
    const Clip*  sndOver = nullptr;
    const Clip*  bgmBoss = nullptr;

    // 关卡进度
    int   stageIdx = 0;
    float bgScroll = 0.0f;      // 行星视差滚动
    float planetY = 0.0f;
    float stageBannerT = 0.0f;  // 开场 "STAGE N" 横幅
    void text_at(int x, int y, const char* s, Color c) {
        const int sx = ox_, sy = oy_;
        ox_ = oy_ = 0;
        d_->text(x, y, s, c, 1);
        ox_ = sx; oy_ = sy;
    }

    // -----------------------------------------------------------------------
    // 对象池分配 / 生成
    // -----------------------------------------------------------------------
    PBullet* pbul_alloc() { for (auto& b : pbul) if (!b.alive) return &b; return nullptr; }
    EBullet* ebul_alloc() { for (auto& b : ebul) if (!b.alive) return &b; return nullptr; }

    void pbul_spawn(float x, float y, float vx, float vy, int dmg, int r, bool homing) {
        PBullet* b = pbul_alloc();
        if (!b) return;
        b->alive = true; b->x = x; b->y = y; b->vx = vx; b->vy = vy;
        b->dmg = dmg; b->r = r; b->homing = homing;
    }
    void ebul_dir(float x, float y, float ang, float sp, int r) {
        EBullet* b = ebul_alloc();
        if (!b) return;
        b->alive = true; b->x = x; b->y = y;
        b->vx = std::cos(ang) * sp; b->vy = std::sin(ang) * sp; b->r = r;
    }
    void ebul_aimed(float x, float y, float sp) {
        ebul_dir(x, y, std::atan2(py - y, px - x), sp, 2);
    }
    void boom_spawn(float x, float y, uint8_t big) {
        for (auto& b : booms) {
            if (b.alive) continue;
            b.alive = true; b.x = x; b.y = y; b.t = 0; b.big = big;
            b.dur = (big >= 3) ? 1.1f : (big == 2 ? 0.55f : 0.30f);
            b.r   = (big >= 3) ? 30.0f : (big == 2 ? 16.0f : 9.0f);
            return;
        }
    }
    void spark_spawn(float x, float y, float vx, float vy, uint8_t kind, float dur) {
        for (auto& s : sparks) {
            if (s.alive) continue;
            s.alive = true; s.x = x; s.y = y; s.vx = vx; s.vy = vy; s.t = dur; s.kind = kind;
            return;
        }
    }
    void pickup_spawn(float x, float y, uint8_t kind) {
        for (auto& p : pickups) {
            if (p.alive) continue;
            p.alive = true; p.x = x; p.y = y; p.t = 0; p.kind = kind;
            return;
        }
    }
    void enemy_spawn(uint8_t kind, int lan) {
        Enemy* e = nullptr;
        for (auto& en : enemies) if (!en.alive) { e = &en; break; }
        if (!e) return;

        *e = Enemy();
        lan = lan < 0 ? 0 : (lan >= kLanes ? kLanes - 1 : lan);
        e->alive = true;
        e->kind  = kind;
        e->y = e->baseY = (float)laneY[lan];
        e->x = (float)(W + 18);
        const float sp = kStages[stageIdx].speed;      // 关卡越靠后敌人越快
        switch (kind) {
            case E_DRONE:  e->hp = e->hpMax = 2;  e->vx = -62.0f * sp; e->r = 7;  e->score = 100;   break;
            case E_WEAVER: e->hp = e->hpMax = 3;  e->vx = -54.0f * sp; e->r = 7;  e->score = 150;   break;
            case E_TURRET: e->hp = e->hpMax = 9;  e->vx = -34.0f * sp; e->r = 8;  e->score = 300;   break;
            case E_MINI:   e->hp = e->hpMax = kStages[stageIdx].miniHp;
                           e->vx = -48.0f; e->r = 15; e->score = 2000 * (stageIdx + 1);
                           bossBusy = true; break;
            default:       e->hp = e->hpMax = kStages[stageIdx].bossHp;
                           e->vx = -48.0f; e->r = 26; e->score = 10000 * (stageIdx + 1);
                           bossBusy = true; break;
        }
        e->fireCd = 1.1f + (float)lan * 0.27f;
        if (kind == E_MINI || kind == E_BOSS) sfx(sndBossWarn, 0.55f);   // Boss 出场警告
    }

    // -----------------------------------------------------------------------
    // 伤害 / 死亡 / 受击
    // -----------------------------------------------------------------------
    void enemy_damage(Enemy& e, int dmg) {
        if (!e.alive || dmg <= 0) return;
        const bool bossKind = (e.kind == E_MINI || e.kind == E_BOSS);
        const int  hpBefore = e.hp;
        e.hp -= dmg;
        e.hitFlash = 0.07f;
        if (e.hp > 0) {
            if (bossKind && e.hpMax > 0 && (e.hp * 10 / e.hpMax) != (hpBefore * 10 / e.hpMax))
                sfx(sndBoom, 0.30f);          // Boss 每掉 10% 一下闷响
            else
                sfx(sndShoot, 0.14f);         // 普通命中
            return;
        }

        e.alive = false;
        const bool boss = (e.kind == E_MINI || e.kind == E_BOSS);
        if (boss) bossBusy = false;
        score += e.score;

        if (e.kind == E_BOSS) {
            boom_spawn(e.x, e.y, 3);
            for (int k = 0; k < 6; ++k)
                boom_spawn(e.x + (float)(rnd_range(70) - 35), e.y + (float)(rnd_range(50) - 25), 1);
            shake = 1.0f;
            bossDownT = 2.2f;
            sfx(sndBoom, 1.0f);
        } else if (e.kind == E_MINI) {
            boom_spawn(e.x, e.y, 2);
            boom_spawn(e.x + 14, e.y - 10, 1);
            boom_spawn(e.x + 14, e.y + 10, 1);
            shake = 0.8f;
            pickup_spawn(e.x, e.y - 10, 0);     // 小 Boss 必掉: 火力 + 导弹
            pickup_spawn(e.x, e.y + 10, 1);
            sfx(sndBoom, 0.8f);
        } else {
            boom_spawn(e.x, e.y, 1);
            if (rnd_range(100) < 32) pickup_spawn(e.x, e.y, (uint8_t)rnd_range(3));
            sfx(sndBoom, 0.45f);
        }
        if (laserCharge < kLaserFull) laserCharge++;
    }

    void player_hit() {
        if (!alive || invuln > 0 || phase != PH_PLAY) return;
        boom_spawn(px, py, 2);
        shake = 1.0f;
        sfx(sndBoom, 0.9f);
        lives--;
        if (power > 1) power--;
        if (missiles > 0) missiles--;
        if (lives < 0) { alive = false; enter_phase(PH_OVER); return; }
        invuln = 2.0f;
        lane = 2;                       // 复活回中间道
        py = (float)laneY[2];
        px = 60.0f; vx = 0.0f;
    }

    // -----------------------------------------------------------------------
    // 玩家
    // -----------------------------------------------------------------------
    void lane_apply(int dir) {
        int l = lane + dir;
        if (l < 0) l = 0;
        if (l >= kLanes) l = kLanes - 1;
        lane = l;
    }

    void player_update(Engine& e, float dt) {
        if (!alive) return;
        if (invuln > 0) invuln -= dt;
        const PadState& in = e.input;

        // 横向: 加速度 + 阻尼(比匀速手感好)
        float dir = 0;
        if (is_held(in, Button::Left))  dir -= 1;
        if (is_held(in, Button::Right)) dir += 1;
        if (dir != 0) vx += dir * 1100.0f * dt;
        vx *= (dir != 0) ? 0.88f : 0.80f;
        px += vx * dt;
        const float xmin = 18.0f;
        const float xmax = (float)W - 128.0f;      // 右侧留出反应距离(固件版同设计)
        if (px < xmin) { px = xmin; vx = 0; }
        if (px > xmax) { px = xmax; vx = 0; }

        // 换道: 按住先立刻跳一格, 之后按 repeat 间隔连跳
        int h = 0;
        if (is_held(in, Button::Up))        h = -1;
        else if (is_held(in, Button::Down)) h = 1;
        if (h == 0) { laneDir = 0; laneRepeat = 0; }
        else if (h != laneDir) { laneDir = h; laneRepeat = 0.30f; lane_apply(h); }
        else if (laneRepeat > 0.0f) {
            laneRepeat -= dt;
            if (laneRepeat <= 0.0f) { laneRepeat = 0.10f; lane_apply(h); }
        }

        // 平滑走到目标航道
        const float targetY = (float)laneY[lane];
        const float step = 165.0f * dt;
        const float dy = targetY - py;
        if (std::fabs(dy) <= step) py = targetY;
        else py += (dy > 0.0f) ? step : -step;
    }

    void fire_update(Engine& e, float dt) {
        if (!alive) return;

        // ---- 主炮: 按住 A, 火力等级决定弹道 ----
        if (is_held(e.input, Button::A)) {
            fireCd -= dt;
            if (fireCd <= 0.0f) {
                const float bx = px + 12.0f, by = py;
                switch (power) {
                    case 1:
                        fireCd = 0.165f;
                        pbul_spawn(bx, by, 280.0f, 0.0f, 1, 2, false);
                        break;
                    case 2:
                        fireCd = 0.145f;
                        pbul_spawn(bx, by - 4.0f, 280.0f, 0.0f, 1, 2, false);
                        pbul_spawn(bx, by + 4.0f, 280.0f, 0.0f, 1, 2, false);
                        break;
                    case 3:
                        fireCd = 0.130f;
                        pbul_spawn(bx, by, 300.0f, 0.0f, 1, 3, false);
                        pbul_spawn(bx - 2.0f, by - 4.0f, 280.0f, -55.0f, 1, 2, false);
                        pbul_spawn(bx - 2.0f, by + 4.0f, 280.0f, 55.0f, 1, 2, false);
                        break;
                    default:
                        fireCd = 0.110f;
                        pbul_spawn(bx, by - 3.0f, 320.0f, 0.0f, 2, 3, false);
                        pbul_spawn(bx, by + 3.0f, 320.0f, 0.0f, 2, 3, false);
                        pbul_spawn(bx - 2.0f, by - 6.0f, 300.0f, -70.0f, 2, 2, false);
                        pbul_spawn(bx - 2.0f, by + 6.0f, 300.0f, 70.0f, 2, 2, false);
                        break;
                }
                play(e, sndShoot, 0.50f);
            }
        } else {
            fireCd = 0.0f;
        }

        // ---- 追踪导弹: 有存货就自动打 ----
        if (missiles > 0) {
            missileCd -= dt;
            if (missileCd <= 0.0f) {
                missileCd = 1.25f - 0.22f * (float)(missiles - 1);
                const float oy[3] = { -8.0f, 8.0f, 0.0f };
                for (int k = 0; k < missiles && k < 3; ++k)
                    pbul_spawn(px - 2.0f, py + oy[k], -120.0f, 0.0f, 3, 3, true);
                play(e, sndShoot, 0.30f);
            }
        }

        // ---- 蓄力激光: 槽满按 B 释放, 持续 1.5s 穿透 ----
        if (is_pressed(e.input, Button::B) && specialT <= 0.0f && laserCharge >= kLaserFull) {
            laserCharge = 0;
            specialT = 1.5f;
            shake = 1.0f;
            play(e, sndBoom, 0.5f);
        }
        if (specialT > 0.0f) {
            specialT -= dt;
            shake = 1.0f;
            for (auto& en : enemies) {
                if (!en.alive) continue;
                if (en.x + (float)en.r < px) continue;
                if (std::fabs(en.y - py) > (float)en.r + 6.0f) continue;
                enemy_damage(en, (int)(52.0f * dt) + 1);
            }
        }
        laserCueDone = (laserCharge >= kLaserFull);
    }

    // -----------------------------------------------------------------------
    // 子弹
    // -----------------------------------------------------------------------
    void pbul_update(float dt) {
        for (auto& b : pbul) {
            if (!b.alive) continue;

            if (b.homing) {
                const Enemy* tgt = nullptr;
                float best = 1e9f;
                for (auto& en : enemies) {
                    if (!en.alive) continue;
                    const float dx = en.x - b.x, dy = en.y - b.y, d2 = dx * dx + dy * dy;
                    if (d2 < best) { best = d2; tgt = &en; }
                }
                if (tgt) {
                    const float want = std::atan2(tgt->y - b.y, tgt->x - b.x);
                    const float cur  = std::atan2(b.vy, b.vx);
                    float dd = want - cur;
                    while (dd >  3.1415926f) dd -= 6.2831853f;
                    while (dd < -3.1415926f) dd += 6.2831853f;
                    const float na = cur + clampf(dd, -5.0f * dt, 5.0f * dt);
                    b.vx = std::cos(na) * 300.0f;
                    b.vy = std::sin(na) * 300.0f;
                }
                if ((rnd_u32() & 1u) == 0u)
                    spark_spawn(b.x, b.y, 30.0f, (float)(rnd_range(20) - 10), 1, 0.22f);
            }

            b.x += b.vx * dt;
            b.y += b.vy * dt;
            if (b.x > (float)W + 12.0f || b.x < -14.0f || b.y < -12.0f || b.y > (float)H + 12.0f) {
                b.alive = false;
                continue;
            }

            for (auto& en : enemies) {
                if (!en.alive) continue;
                if (std::fabs(en.x - b.x) < (float)en.r + (float)b.r &&
                    std::fabs(en.y - b.y) < (float)en.r + (float)b.r) {
                    b.alive = false;
                    enemy_damage(en, b.dmg);
                    break;
                }
            }
        }
    }

    void ebul_update(float dt) {
        const int laneHalf = 6;
        for (auto& b : ebul) {
            if (!b.alive) continue;
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            if (b.x < -12.0f || b.x > (float)W + 12.0f || b.y < -12.0f || b.y > (float)H + 12.0f) {
                b.alive = false;
                continue;
            }
            if (alive && invuln <= 0.0f &&
                std::fabs(b.x - px) < (float)b.r + 7.0f &&
                std::fabs(b.y - py) < (float)b.r + laneHalf) {
                b.alive = false;
                player_hit();
            }
        }
    }

    // -----------------------------------------------------------------------
    // 敌机
    // -----------------------------------------------------------------------
    void enemy_update(Engine& e, float dt) {
        const float PI = 3.1415926f;
        for (auto& en : enemies) {
            if (!en.alive) continue;
            en.t += dt;
            if (en.hitFlash > 0.0f) en.hitFlash -= dt;

            switch (en.kind) {
                case E_DRONE:
                    en.x += en.vx * dt;
                    break;

                case E_WEAVER:
                    en.x += en.vx * dt;
                    en.y = en.baseY + std::sin(en.t * 3.4f) * 22.0f;
                    break;

                case E_TURRET:
                    // 进场后减速站定, 周期性打三向弹
                    en.x += (en.x > (float)W - 90) ? en.vx * dt : en.vx * 0.35f * dt;
                    en.fireCd -= dt;
                    if (en.fireCd <= 0.0f && en.x < (float)W - 40 && en.x > 10.0f) {
                        en.fireCd = 1.9f;
                        ebul_dir(en.x - 6, en.y, PI,          150.0f, 2);
                        ebul_dir(en.x - 6, en.y, PI * 0.82f,  150.0f, 2);
                        ebul_dir(en.x - 6, en.y, PI * 1.18f,  150.0f, 2);
                    }
                    break;

                default: {   // E_MINI / E_BOSS
                    const bool big = (en.kind == E_BOSS);
                    const float stopX = (float)W - (big ? 74.0f : 62.0f);
                    if (en.x > stopX) {
                        en.x += en.vx * dt;
                    } else {
                        en.x += std::cos(en.t * 0.9f) * 22.0f * dt;
                        en.y = en.baseY + std::sin(en.t * 1.15f) * (big ? 34.0f : 26.0f);
                        en.y = clampf(en.y, (float)(laneY[0] - 6), (float)(laneY[kLanes - 1] + 6));
                        en.fireCd -= dt;
                        if (en.fireCd <= 0.0f) {
                            en.fireCd = big ? 0.85f : 1.25f;
                            ebul_aimed(en.x - 8, en.y, big ? 175.0f : 150.0f);
                            if (big) for (int k = -1; k <= 1; ++k) ebul_dir(en.x - 8, en.y, PI + k * 0.30f, 140.0f, 2);
                        }
                        // 半血开始冒烟
                        if (en.hp * 2 <= en.hpMax && ((int)(en.t * 6.0f) & 1) == 0)
                            spark_spawn(en.x, en.y - 4.0f, 20.0f, -12.0f, 1, 0.3f);
                    }
                    break;
                }
            }

            if (en.x < -30.0f) {
                en.alive = false;
                if (en.kind == E_MINI || en.kind == E_BOSS) bossBusy = false;
                continue;
            }

            // 撞玩家
            if (alive && invuln <= 0.0f &&
                std::fabs(en.x - px) < (float)en.r + 8.0f &&
                std::fabs(en.y - py) < (float)en.r + 6.0f) {
                player_hit();
            }
        }
    }

    // -----------------------------------------------------------------------
    // 关卡时间轴
    // -----------------------------------------------------------------------
    void stage_update(float dt) {
        if (bossBusy) return;               // Boss 在场时暂停时间轴
        stageT += dt;

        // 已排队的怪按固定间距放出来
        if (pendingLeft > 0) {
            pendingGap -= dt;
            if (pendingGap <= 0.0f) {
                enemy_spawn(pendingKind, pendingLane);
                pendingLeft--;
                pendingSpawned++;
                if (pendingStep != 0) {
                    pendingLane += pendingStep;
                    if (pendingLane < 0) pendingLane = 0;
                    if (pendingLane >= kLanes) pendingLane = kLanes - 1;
                }
                pendingGap = 0.34f;
            }
            return;
        }

        const StageDef& st = kStages[stageIdx];
        if (evIdx >= (int)st.count) return;
        const StageEvent& ev = st.ev[evIdx];
        if (stageT < ev.t) return;
        evIdx++;
        pendingKind    = ev.kind;
        pendingLane    = ev.lane;
        pendingStep    = ev.laneStep;
        pendingLeft    = ev.count;
        pendingSpawned = 0;
        pendingGap     = 0.0f;
    }

    // 当前关波次是否都放完了
    bool stage_waves_done() const { return evIdx >= (int)kStages[stageIdx].count && pendingLeft == 0; }

    bool any_enemy_alive() const {
        for (const auto& en : enemies) if (en.alive) return true;
        return false;
    }

    // -----------------------------------------------------------------------
    // 掉落物 / 特效 / 星空
    // -----------------------------------------------------------------------
    void pickup_apply(uint8_t kind) {
        if (kind == 0)      { if (power < 4) power++; }
        else if (kind == 1) { if (missiles < 3) missiles++; }
        else                { if (lives < 9) lives++; }
    }

    void pickup_update(Engine& e, float dt) {
        for (auto& p : pickups) {
            if (!p.alive) continue;
            p.t += dt;
            p.x -= 34.0f * dt;
            if (p.x < -10.0f) { p.alive = false; continue; }
            if (alive && std::fabs(p.x - px) < 12.0f && std::fabs(p.y - py) < 12.0f) {
                p.alive = false;
                pickup_apply(p.kind);
                play(e, sndShoot, 0.5f);
            }
        }
    }

    void fx_update(float dt) {
        for (auto& b : booms) {
            if (!b.alive) continue;
            b.t += dt;
            if (b.t >= b.dur) b.alive = false;
        }
        for (auto& s : sparks) {
            if (!s.alive) continue;
            s.t -= dt;
            if (s.t <= 0.0f) { s.alive = false; continue; }
            s.x += s.vx * dt;
            s.y += s.vy * dt;
        }
        if (shake > 0.0f) {
            shake -= dt * 3.2f;
            if (shake < 0.0f) shake = 0.0f;
            const int amp = (int)(3.0f * shake) + 1;
            ox_ = rnd_range(2 * amp + 1) - amp;
            oy_ = rnd_range(2 * amp + 1) - amp;
        } else {
            ox_ = 0; oy_ = 0;
        }
    }

    void stars_init() {
        for (auto& s : stars) {
            s.x = (float)rnd_range(W);
            s.y = (float)(kHudH + rnd_range(H - kHudH));
            s.layer = (uint8_t)rnd_range(3);
        }
    }
    void stars_update(float dt) {
        for (auto& s : stars) {
            const float sp = (s.layer == 0) ? 20.0f : (s.layer == 1 ? 44.0f : 78.0f);
            s.x -= sp * dt;
            if (s.x < 1.0f) {
                s.x = (float)(W - 1);
                s.y = (float)(kHudH + rnd_range(H - kHudH));
            }
        }
    }

    // -----------------------------------------------------------------------
    // 分层绘制
    // -----------------------------------------------------------------------
    void draw_stars() {
        const Color c0 = col(42, 58, 90), c1 = col(96, 112, 140), c2 = col(176, 192, 216);
        for (auto& s : stars) {
            pset((int)s.x, (int)s.y, s.layer == 0 ? c0 : (s.layer == 1 ? c1 : c2));
            if (s.layer == 2) pset((int)s.x + 1, (int)s.y, c1);   // 快层拖尾
        }
    }

    void draw_lane_guides() {
        for (int l = 0; l < kLanes; ++l) {
            const Color c = (l == lane) ? col(80, 233, 255) : col(40, 58, 88);
            frect(4, laneY[l] - 1, 3, 3, c);
        }
    }

    void draw_pickup(const Pickup& p) {
        const int k = (p.kind < 3) ? (int)p.kind : 2;
        // 闪烁: 偶数帧画精灵, 奇数帧画高亮环(比素色块醒目)
        if (((int)(p.t * 8.0f) & 1) == 0) {
            blit_center(p.x, p.y, sprPick[k]);
        } else {
            disc((int)p.x, (int)p.y, 8, col(255, 255, 255));
            blit_center(p.x, p.y, sprPick[k]);
        }
    }

    void draw_boss(const Enemy& en) {
        // Boss 用大尺寸精灵(48x36 / 76x56), 受击整张闪白
        const Image* im = (en.kind == E_BOSS) ? sprBossLarge : sprBossSmall;
        if (im != nullptr) {
            blit_center(en.x, en.y, im, en.hitFlash > 0.0f);
            return;
        }
        // 素材缺失时的程序化兜底
        const bool big = (en.kind == E_BOSS);
        const bool flash = en.hitFlash > 0.0f;
        const int x = (int)en.x, y = (int)en.y;
        const int w = big ? 44 : 30, h = big ? 30 : 22;
        const Color hull = flash ? col(255, 255, 255) : (big ? col(176, 48, 56) : col(192, 112, 40));
        frect(x - w / 2, y - h / 2, w, h, hull);
        disc(x - w / 4, y, big ? 5 : 4, col(16, 24, 32));
        disc(x - w / 4, y, big ? 3 : 2, flash ? col(255, 255, 255) : col(80, 233, 255));
    }

    void draw_enemy(const Enemy& en) {
        const Image* im = nullptr;
        switch (en.kind) {
            case E_DRONE:  im = sprDrone;  break;
            case E_WEAVER: im = sprWeaver; break;
            case E_TURRET: im = sprTurret; break;
            default:       draw_boss(en); return;
        }
        // 受击闪白: 整张精灵改画白色(FC 里最经典的受击反馈)
        blit_center(en.x, en.y, im, en.hitFlash > 0.0f);
    }

    void draw_pbullet(const PBullet& b) {
        const int x = (int)b.x, y = (int)b.y;
        if (b.homing) {                          // 追踪导弹仍是程序化(带烟尾)
            frect(x - 3, y - 2, 7, 5, col(255, 128, 32));
            disc(x - 3, y, 1, col(255, 240, 192));
            return;
        }
        const Image* im = (imgBullet != nullptr) ? imgBullet : nullptr;
        const int idx = (power >= 4) ? 2 : (power == 3 ? 1 : 0);
        if (sprBul[idx] != nullptr) {
            blit_center(b.x, b.y, sprBul[idx]);
            return;
        }
        (void)im;
        frect(x - 3, y - 1, 7, 3, col(255, 224, 64));
    }

    void draw_laser() {
        const int y = (int)py;
        const int x0 = (int)px + 6;
        frect(x0, y - 5, W - x0, 11, col(24, 96, 120));      // 外光
        frect(x0, y - 3, W - x0, 7, col(80, 233, 255));      // 中间
        frect(x0, y - 1, W - x0, 3, col(255, 255, 255));     // 白芯
    }

    void draw_ship() {
        // 高清战机: 三帧尾焰循环(FC/GBA 常见的 2-3 帧推进器动画)
        const int f = ((int)(stateT * 18.0f)) % 3;
        blit_center(px, py, sprShip[f]);
        if (specialT > 0.0f) disc((int)px + 8, (int)py, 3, col(80, 233, 255));
    }

    void draw_stage_banner() {
        if (stageBannerT <= 0.0f || phase != PH_PLAY) return;
        const int cx = W / 2;
        const int cy = kHudH + (H - kHudH) / 3;
        const int sx = ox_, sy = oy_; ox_ = oy_ = 0;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "STAGE %d/%d", stageIdx + 1, kStageCount);
        frect(cx - 62, cy - 8, 124, 18, col(8, 14, 30));
        frect(cx - 61, cy - 7, 122, 16, col(24, 40, 72));
        frect(cx - 61, cy - 7, 122, 1, col(58, 110, 168));
        d_->text(cx - 36, cy - 2, buf, col(249, 192, 67), 1);
        ox_ = sx; oy_ = sy;
    }

    void draw_booms() {
        for (auto& b : booms) {
            if (!b.alive) continue;
            const float k = b.t / b.dur;
            // 爆炸用三档精灵, 并随进度跳帧(比纯色圆更有冲击力)
            const int idx = (b.big >= 3) ? 2 : (b.big == 2 ? 1 : 0);
            if (sprBoom[idx] != nullptr && ((int)(b.t * 30.0f) % 2) == 0) {
                blit_center(b.x, b.y, sprBoom[idx]);
                continue;
            }
            const int r = (int)(b.r * (0.35f + 0.65f * k));
            if (k < 0.45f) {
                disc((int)b.x, (int)b.y, r, col(255, 240, 160));
                disc((int)b.x, (int)b.y, r * 2 / 3, col(255, 255, 255));
            } else {
                disc((int)b.x, (int)b.y, r / 2, col(255, 128, 32));
                disc((int)b.x, (int)b.y, r / 3, col(255, 192, 96));
            }
        }
    }

    void draw_sparks() {
        for (auto& s : sparks) {
            if (!s.alive) continue;
            if (s.kind == 0) disc((int)s.x, (int)s.y, 2, col(255, 232, 160));
            else             pset((int)s.x, (int)s.y, col(144, 144, 144));   // 导弹烟
        }
    }

    void draw_target_lock() {
        const Enemy* best = nullptr;
        float bd = 1e9f;
        for (auto& en : enemies) {
            if (!en.alive) continue;
            const float dx = en.x - px, dy = en.y - py, d2 = dx * dx + dy * dy;
            if (d2 < bd) { bd = d2; best = &en; }
        }
        if (best == nullptr) return;
        // 自绘锁定框(四角括号, 不用 ui_target_lock 素材)
        const int s = best->r + 8;
        const int x0 = (int)best->x - s, y0 = (int)best->y - s;
        const int side = s * 2, len = 5;
        const Color c = col(80, 233, 255);
        frect(x0, y0, len, 1, c);                  frect(x0, y0, 1, len, c);
        frect(x0 + side - len, y0, len, 1, c);     frect(x0 + side - 1, y0, 1, len, c);
        frect(x0, y0 + side - 1, len, 1, c);       frect(x0, y0 + side - len, 1, len, c);
        frect(x0 + side - len, y0 + side - 1, len, 1, c);
        frect(x0 + side - 1, y0 + side - len, 1, len, c);
    }

    void draw_boss_bar() {
        const Enemy* boss = nullptr;
        for (auto& en : enemies) {
            if (en.alive && (en.kind == E_MINI || en.kind == E_BOSS)) { boss = &en; break; }
        }
        if (!boss) return;
        const int sx = ox_, sy = oy_; ox_ = oy_ = 0;
        const int w = W - 140, x0 = 70, y0 = kHudH + 4;
        frect(x0 - 1, y0 - 1, w + 2, 7, col(32, 40, 56));
        frect(x0, y0, w + 1, 5, col(80, 24, 32));
        const int fw = (int)((float)w * ((float)boss->hp / (float)boss->hpMax));
        if (fw > 0) frect(x0, y0, fw + 1, 5, boss->kind == E_BOSS ? col(255, 64, 48) : col(255, 160, 64));
        ox_ = sx; oy_ = sy;
    }

    void draw_hud() {
        char buf[48];
        const int sx = ox_, sy = oy_; ox_ = oy_ = 0;
        frect(0, 0, W, kHudH, col(8, 14, 30));
        frect(0, kHudH - 1, W, 1, col(58, 110, 168));

        std::snprintf(buf, sizeof(buf), "SCORE %06d", score);
        d_->text(4, 3, buf, col(140, 232, 255), 1);

        const bool ready = (laserCharge >= kLaserFull);
        if (ready) std::snprintf(buf, sizeof(buf), "PWR %d MSL %d LAS READY(B)", power, missiles);
        else       std::snprintf(buf, sizeof(buf), "PWR %d MSL %d LAS %d/%d", power, missiles, laserCharge, kLaserFull);
        frect(120, 2, 152, 9, col(10, 18, 36));       // 垫底色, 避免和上下条打架
        d_->text(122, 3, buf, ready ? col(154, 232, 180) : col(140, 200, 230), 1);

        // 命数: 自绘小机图标 + 数字(不依赖 ui_life_pip 素材)
        for (int i = 0; i < lives && i < 3; ++i) {
            const int bx = W - 20 - i * 13;
            frect(bx, 5, 9, 5, col(200, 40, 40));
            frect(bx + 2, 3, 5, 2, col(255, 96, 96));
            frect(bx + 4, 10, 1, 2, col(255, 160, 96));
        }
        std::snprintf(buf, sizeof(buf), "LIFE x%d", lives > 0 ? lives : 0);
        d_->text(W - 122, 3, buf, col(235, 90, 90), 1);
        ox_ = sx; oy_ = sy;
    }

    void draw_overlay() {
        if (phase == PH_PLAY) return;
        const int sx = ox_, sy = oy_; ox_ = oy_ = 0;

        const int cx = W / 2;
        const int cy = (H - kHudH) / 2 + kHudH;
        frect(0, kHudH, W, H - kHudH, col(4, 8, 20));
        draw_stars();

        if (phase == PH_TITLE) {
            if (uiMissionFrame) blit_center((float)cx, (float)cy - 8.0f, uiMissionFrame);
            else frect(cx - 90, cy - 34, 180, 52, col(24, 40, 72));
            d_->text(cx - 33, cy - 24, "SKY RAIDER", col(140, 232, 255), 2);
            d_->text(cx - 66, cy + 6, "ARROWS MOVE   A SHOOT", col(180, 200, 220), 1);
            d_->text(cx - 66, cy + 18, "ENTER START   B RELEASE LASER", col(180, 200, 220), 1);
        } else if (phase == PH_PAUSE) {
            d_->text(cx - 18, cy - 12, "PAUSED", col(255, 224, 64), 2);
            d_->text(cx - 54, cy + 10, "ENTER  RESUME", col(180, 200, 220), 1);
        } else if (phase == PH_CLEAR) {
            d_->text(cx - 60, cy - 14, "STAGE 1 CLEAR!", col(154, 232, 180), 2);
            char buf[48];
            std::snprintf(buf, sizeof(buf), "SCORE %06d", score);
            d_->text(cx - 30, cy + 10, buf, col(255, 224, 64), 1);
            d_->text(cx - 54, cy + 22, "ENTER  PLAY AGAIN", col(180, 200, 220), 1);
        } else {
            if (uiWarningPlate) blit_center((float)cx, (float)cy - 12.0f, uiWarningPlate);
            d_->text(cx - 45, cy - 20, "GAME OVER", col(255, 96, 96), 2);
            char buf[48];
            std::snprintf(buf, sizeof(buf), "FINAL SCORE %06d", score);
            d_->text(cx - 48, cy + 6, buf, col(255, 224, 64), 1);
            d_->text(cx - 45, cy + 18, "ENTER  RETRY", col(180, 200, 220), 1);
        }
        ox_ = sx; oy_ = sy;
    }

    // -----------------------------------------------------------------------
    // 阶段切换 / 重开
    // -----------------------------------------------------------------------
    void reset() {
        score = 0; stageT = 0; evIdx = 0; stateT = 0; shake = 0; bossDownT = 0;
        stageIdx = 0; bgScroll = (float)(W + 40); planetY = 0.0f; stageBannerT = 2.6f;
        pendingLeft = pendingSpawned = 0; pendingGap = 0; bossBusy = false;
        specialT = 0; laserCueDone = false; ox_ = oy_ = 0;
        for (auto& b : pbul)  b.alive = false;
        for (auto& b : ebul)  b.alive = false;
        for (auto& en : enemies) en.alive = false;
        for (auto& p : pickups) p.alive = false;
        for (auto& b : booms)  b.alive = false;
        for (auto& s : sparks) s.alive = false;
        alive = true;
        lives = 3; power = 1; missiles = 0; laserCharge = 0;
        lane = 2; px = 60.0f; py = (float)laneY[2]; vx = 0; fireCd = 0; missileCd = 0;
        invuln = 1.5f; laneDir = 0; laneRepeat = 0;
    }

    void enter_phase(uint8_t p) {
        phase = p;
        stateT = 0;
        if (p == PH_OVER) { alive = false; sfx(sndOver, 0.8f); }
        if (p == PH_CLEAR) sfx(sndClear, 0.9f);
        // 开始/重开时确保 BGM 在放(播放中重复设置会从头循环, 符合预期)
        // Boss 曲缺失时回退到关卡曲(卡带裁剪掉 bgm_boss 也能正常工作)
        if ((p == PH_PLAY || p == PH_TITLE) && eng_ != nullptr && bgmClip != nullptr) {
            const Clip* use = ((p == PH_TITLE) || !bossBusy || bgmBoss == nullptr) ? bgmClip : bgmBoss;
            eng_->audio.play_bgm(use, 0.9f);
        }
    }

    void play(Engine& e, const Clip* c, float vol) {
        if (c) e.audio.play_sfx(c, vol);
    }
    // 无 Engine 引用的回调深处(爆炸/受击/拾取)用这个
    void sfx(const Clip* c, float vol) {
        if (eng_ != nullptr && c != nullptr) eng_->audio.play_sfx(c, vol);
    }

public:
    const char* name() const override { return "skyraider"; }

    void on_start(Engine& e) override {
        imgPlayer      = e.img("skyraider/player");
        imgEnemy       = e.img("skyraider/enemy");
        imgBullet      = e.img("skyraider/bullet");
        uiLifePip      = e.img("skyraider/ui_life_pip");
        uiTargetLock   = e.img("skyraider/ui_target_lock");
        uiMissionFrame = e.img("skyraider/ui_mission_frame");
        uiWarningPlate = e.img("skyraider/ui_warning_plate");
        eng_           = &e;
        sndShoot       = e.snd("skyraider/sfx_shoot");
        sndBoom        = e.snd("skyraider/sfx_explode");
        bgmClip        = e.snd("skyraider/bgm_stage");
        e.audio.play_bgm(bgmClip, 0.9f);

        // ---- 高清精灵 ----
        sprShip[0]     = e.img("skyraider/ship_f0");
        sprShip[1]     = e.img("skyraider/ship_f1");
        sprShip[2]     = e.img("skyraider/ship_f2");
        sprDrone       = e.img("skyraider/ene_drone");
        sprWeaver      = e.img("skyraider/ene_weaver");
        sprTurret      = e.img("skyraider/ene_turret");
        sprBossSmall   = e.img("skyraider/boss_small");
        sprBossLarge   = e.img("skyraider/boss_large");
        sprPick[0]     = e.img("skyraider/pick_power");
        sprPick[1]     = e.img("skyraider/pick_missile");
        sprPick[2]     = e.img("skyraider/pick_life");
        sprBul[0]      = e.img("skyraider/bul_a");
        sprBul[1]      = e.img("skyraider/bul_b");
        sprBul[2]      = e.img("skyraider/bul_c");
        sprBoom[0]     = e.img("skyraider/boom_s");
        sprBoom[1]     = e.img("skyraider/boom_m");
        sprBoom[2]     = e.img("skyraider/boom_l");
        bgNebula       = e.img("skyraider/bg_nebula");
        bgPlanet       = e.img("skyraider/bg_planet");
        hudFrame       = e.img("skyraider/hud_frame");

        // ---- 新增音效 ----
        sndHit        = e.snd("skyraider/sfx_hit");
        sndBoomBig    = e.snd("skyraider/sfx_explode_big");
        sndPlayerHit  = e.snd("skyraider/sfx_player_hit");
        sndPick       = e.snd("skyraider/sfx_pick");
        sndLife       = e.snd("skyraider/sfx_life");
        sndLaser      = e.snd("skyraider/sfx_laser");
        sndMissile    = e.snd("skyraider/sfx_missile");
        sndBossWarn   = e.snd("skyraider/sfx_boss_warn");
        sndClear      = e.snd("skyraider/sfx_clear");
        sndOver       = e.snd("skyraider/sfx_over");
        bgmBoss       = e.snd("skyraider/bgm_boss");
        // 素材解析自检: 三个都非 0 说明音频素材没问题, 静音只可能出在输出后端
        fprintf(stderr, "[skyraider] audio: bgm=%u帧 shoot=%u帧 boom=%u帧\n",
                bgmClip ? bgmClip->frames : 0u,
                sndShoot ? sndShoot->frames : 0u,
                sndBoom ? sndBoom->frames : 0u);
        fflush(stderr);

        // 按实际画面尺寸自适应(模拟器/实机都不写死)
        d_ = &e.display;
        W  = (int)e.display.width();
        H  = (int)e.display.height();
        if (H > kMaxHRes) H = kMaxHRes;

        // 5 条航道在 HUD 之下均匀铺开
        const int span = (H - 28) - 26;
        for (int i = 0; i < kLanes; ++i) laneY[i] = 26 + span * i / (kLanes - 1);

        // 渐变天幕(上深下亮)
        const Color top = col(6, 11, 30), bot = col(22, 48, 94);
        for (int y = 0; y < kMaxHRes; ++y) {
            const float t = (y <= kHudH || H <= kHudH + 1)
                          ? 0.0f : clampf((float)(y - kHudH) / (float)(H - kHudH), 0.0f, 1.0f);
            sky_[y] = col((uint8_t)(6 + (22 - 6) * t),
                          (uint8_t)(11 + (48 - 11) * t),
                          (uint8_t)(30 + (94 - 30) * t));
        }
        (void)top; (void)bot;
        stars_init();
        reset();
        enter_phase(PH_TITLE);
    }

    void on_update(Engine& e, float dt) override {
        stateT += dt;
        // 背景滚动(行星从右往左循环)
        bgScroll -= dt * 14.0f;
        if (bgScroll < -90.0f) bgScroll = (float)W + 40.0f;
        if (stageBannerT > 0.0f) stageBannerT -= dt;
        const PadState& in = e.input;
        const bool confirm = is_pressed(in, Button::Start) || is_pressed(in, Button::A);

        switch (phase) {
            case PH_TITLE:
                if (confirm) { reset(); enter_phase(PH_PLAY); }
                return;
            case PH_PAUSE:
                if (confirm) enter_phase(PH_PLAY);
                return;
            case PH_CLEAR:
            case PH_OVER:
                if (stateT > 0.8f && confirm) { reset(); enter_phase(PH_PLAY); }
                return;
            default:
                break;
        }

        if (is_pressed(in, Button::Start)) { enter_phase(PH_PAUSE); return; }

        player_update(e, dt);
        fire_update(e, dt);
        pbul_update(dt);
        ebul_update(dt);
        enemy_update(e, dt);
        stage_update(dt);
        pickup_update(e, dt);
        fx_update(dt);
        stars_update(dt);

        // 大 Boss 炸完 -> 过关/进下一关
        if (bossDownT > 0.0f) {
            bossDownT -= dt;
            if (bossDownT <= 0.0f) {
                if (stageIdx + 1 < kStageCount) {
                    stageIdx++;                     // 下一关: 重排波次, 保留分数/火力
                    stageT = 0; evIdx = 0;
                    pendingLeft = pendingSpawned = 0; pendingGap = 0;
                    bossBusy = false; bossDownT = 0;
                    stageBannerT = 2.6f;
                    laserCharge = 0;
                    sfx(sndClear, 0.7f);
                    if (bgmClip != nullptr && eng_ != nullptr) eng_->audio.play_bgm(bgmClip, 0.9f);
                } else {
                    enter_phase(PH_CLEAR);          // 六关全通
                }
            }
        }
    }

    void on_render(Engine& e) override {
        if (d_ == nullptr) return;
        Display& d = *d_;

        // 背景: 星云底图(静态) + 行星视差 + 程序化星点(近层)
        if (bgNebula != nullptr) d.draw_image(0, 0, *bgNebula);
        else for (int y = 0; y < H; ++y) d.fill_rect(0, y, W, 1, sky_[y]);
        if (bgPlanet != nullptr) {
            planetY = (float)(H - 82);
            d.draw_image_alpha((int)bgScroll, (int)planetY, *bgPlanet, kColorMagenta);
        }
        draw_stars();
        draw_lane_guides();

        // 实体(拾取物 -> 敌机 -> 子弹 -> 激光 -> 自机 -> 特效)
        for (auto& p : pickups) if (p.alive) draw_pickup(p);
        for (auto& en : enemies) if (en.alive) draw_enemy(en);
        for (auto& b : pbul) if (b.alive) draw_pbullet(b);
        for (auto& b : ebul) {
            if (!b.alive) continue;
            disc((int)b.x, (int)b.y, b.r, col(255, 208, 32));
            pset((int)b.x, (int)b.y, col(255, 255, 255));
        }
        draw_target_lock();
        if (specialT > 0.0f) draw_laser();
        if (alive && (invuln <= 0.0f || ((int)(invuln * 14.0f) & 1) == 0)) draw_ship();
        draw_booms();
        draw_sparks();

        // UI
        draw_boss_bar();
        draw_hud();
        draw_stage_banner();
        draw_overlay();
    }
};

} // namespace

namespace games {
engine::Game* make_skyraider() { return new SkyRaider(); }
}
