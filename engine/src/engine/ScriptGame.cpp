// .gbn 游戏包解析 + 字节码 VM。纯 C++17, 平台无关。
// 格式与指令集见 docs/SCRIPT.md; 必须与 tools/gs_compiler.py 保持一致。
#include "ScriptGame.h"
#include "Engine.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace engine {
namespace {

// ---- 字节码(与 tools/gs_compiler.py 对应) ----
enum : uint8_t {
    OP_HALT = 0x00, OP_PUSH = 0x01, OP_LOADG = 0x02, OP_STOREG = 0x03, OP_POP = 0x04,
    OP_LDMEM = 0x05, OP_STMEM = 0x06, OP_LDIDX = 0x07, OP_STIDX = 0x08,
    OP_ADD = 0x10, OP_SUB = 0x11, OP_MUL = 0x12, OP_DIV = 0x13, OP_MOD = 0x14,
    OP_NEG = 0x15, OP_LNOT = 0x16,
    OP_LT = 0x17, OP_LE = 0x18, OP_GT = 0x19, OP_GE = 0x1A, OP_EQ = 0x1B, OP_NE = 0x1C,
    OP_LAND = 0x1D, OP_LOR = 0x1E,
    OP_AND = 0x1F,
    OP_JMP = 0x20, OP_JZ = 0x21, OP_JNZ = 0x22,
    OP_OR = 0x23, OP_XOR = 0x24, OP_SHL = 0x25, OP_SHR = 0x26,
    OP_CALLN = 0x30,
    OP_CALL_USER = 0x31,          // 调用脚本函数: 地址是 u16(与 CALLN 的 u8 不同)
    OP_RET = 0x32,                // 返回; 返回地址存在独立的调用栈里
    OP_IMG = 0x40, OP_TEXT = 0x41, OP_SFX = 0x42, OP_BGM = 0x43,
    OP_IMGF = 0x44, OP_IMGV = 0x45, OP_IMGW = 0x46, OP_IMGH = 0x47,
    OP_SCHR = 0x48,               // chr(strs[i], pos): 弹 pos, idx -> 压字符码
    // 浮点: 同一个 32 位 cell 存 IEEE-754 位模式, 类型由编译器静态推导,
    // 所以运行时不需要 tag(整型指令和浮点指令不能混用, 编译器负责插 ITOF)。
    OP_FADD = 0x50, OP_FSUB = 0x51, OP_FMUL = 0x52, OP_FDIV = 0x53, OP_FNEG = 0x54,
    OP_FLT = 0x55, OP_FLE = 0x56, OP_FGT = 0x57, OP_FGE = 0x58, OP_FEQ = 0x59, OP_FNE = 0x5A,
    OP_ITOF = 0x5B, OP_FTOI = 0x5C, OP_FLAND = 0x5D, OP_FLOR = 0x5E, OP_FLNOT = 0x5F,
};

enum : uint8_t {
    FN_HELD = 0, FN_PRESSED = 1, FN_RELEASED = 2, FN_RND = 3, FN_RGB = 4,
    FN_DT_MS = 5, FN_FRAME = 6, FN_EXIT = 7, FN_CLEAR = 8, FN_PX = 9,
    FN_RECT = 10, FN_FRECT = 11, FN_SCREEN_W = 12, FN_SCREEN_H = 13, FN_TEXTNUM = 14,
    FN_ABS = 15, FN_IMIN = 16, FN_IMAX = 17,
    FN_ITOF = 18, FN_FTOI = 19, FN_FABS = 20, FN_DTF = 21,
    FN_FMIN = 22, FN_FMAX = 23, FN_FSQRT = 24,
};

constexpr uint32_t kHeaderSize = 36;
constexpr int      kMaxStack = 256;
// globals_ 既要装标量变量, 也是数组的内存池(见 docs/SCRIPT.md)
constexpr uint16_t kMaxGlobals = 2048;

inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// 32 位 cell <-> float: 两边都是位模式搬运, 没有任何转换开销
inline float   as_f(int32_t v) { float f; memcpy(&f, &v, sizeof(f)); return f; }
inline int32_t as_i(float f)   { int32_t v; memcpy(&v, &f, sizeof(v)); return v; }
// FTOI: 截断; NaN/越界一律夹到 int32 量程内(直接 (int32_t)f 是 UB)
inline int32_t f_to_i(float f) {
    if (f != f) return 0;
    if (f >= 2147483000.0f) return 2147483000;
    if (f <= -2147483000.0f) return -2147483000;
    return (int32_t)f;
}

// 取路径的 basename(去掉目录前缀), 这样包内素材与目录无关
std::string base_name(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

} // namespace

// ===========================================================================
// GamePackage
// ===========================================================================
bool GamePackage::parse(const uint8_t* data, size_t size) {
    code_ = nullptr;
    strings_.clear();
    texts_.clear();
    assets_.clear();
    if (data == nullptr || size < kHeaderSize) return false;
    if (memcmp(data, "GBN1", 4) != 0) return false;
    const uint16_t version = rd16(data + 4);
    if (version != 1) return false;

    globals_  = rd16(data + 6);
    init_pc_  = rd32(data + 8);
    entry_[0] = rd32(data + 12);
    entry_[1] = rd32(data + 16);
    entry_[2] = rd32(data + 20);
    code_size_ = rd32(data + 24);
    string_count_ = rd32(data + 28);
    asset_count_  = rd32(data + 32);

    if (globals_ > kMaxGlobals) return false;
    if (size < kHeaderSize + code_size_) return false;
    code_ = data + kHeaderSize;
    if (code_size_ == 0) return false;

    size_t p = kHeaderSize + code_size_;

    // 字符串表
    str_base_ = data;
    for (uint32_t i = 0; i < string_count_; ++i) {
        if (p + 2 > size) return false;
        const uint16_t len = rd16(data + p);
        p += 2;
        if (p + len > size) return false;
        strings_.emplace_back((uint32_t)p, (uint32_t)len);
        texts_.emplace_back((const char*)(data + p), (size_t)len);
        p += len;
    }

    // 素材表
    for (uint32_t i = 0; i < asset_count_; ++i) {
        if (p + 2 > size) return false;
        const uint16_t name_len = rd16(data + p);
        p += 2;
        if (p + name_len + 4 > size) return false;
        AssetRef ref;
        ref.name.assign((const char*)(data + p), name_len);
        p += name_len;
        ref.size = rd32(data + p);
        p += 4;
        if (p + ref.size > size) return false;
        ref.offset = (uint32_t)p;
        p += ref.size;
        assets_.push_back(std::move(ref));
    }
    return true;
}

bool GamePackage::load_from(FileSystem& fs, const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!fs.read(path, bytes)) return false;
    blob_ = std::move(bytes);
    return parse(blob_.data(), blob_.size());
}

const char* GamePackage::string_at(uint32_t index) const {
    // ⚠⚠ 包里存的字符串**没有 NUL 结尾**(只有 u16 长度前缀)。
    // 把裸指针交出去, 下游的 asset_name() 会一直抄到下一个字符串/素材名表里,
    // 拼出一个乱七八精的资源名 —— 表现是"图片全部静默消失", 但自检又会因为
    // 其它项都正常而看着像通过。踩过。
    if (index >= texts_.size()) return "";
    return texts_[index].c_str();
}

const uint8_t* GamePackage::find_asset(const std::string& name, uint32_t& out_size) const {
    for (const AssetRef& a : assets_) {
        if (a.name == name) {
            out_size = a.size;
            return (code_ != nullptr) ? (code_ - kHeaderSize + a.offset) : nullptr;
        }
    }
    return nullptr;
}

bool GamePackage::read(const std::string& path, std::vector<uint8_t>& out) {
    const std::string want = base_name(path);
    uint32_t len = 0;
    const uint8_t* p = find_asset(want, len);
    if (p == nullptr) return false;
    out.assign(p, p + len);
    return true;
}

// ===========================================================================
// ScriptGame
// ===========================================================================
bool ScriptGame::init(const GamePackage* pkg, const char* name) {
    if (pkg == nullptr || !pkg->loaded()) return false;
    pkg_ = pkg;
    if (name != nullptr && name[0] != '\0') name_ = name;
    globals_.assign(pkg->global_count(), 0);
    // 贴图句柄缓存: 按字符串下标存, 一个下标一次解析
    img_cache_.assign(pkg->string_count(), nullptr);
    img_done_.assign(pkg->string_count(), 0);
    frame_ = 0;
    quit_  = false;
    return true;
}

const char* ScriptGame::asset_name(const char* raw, const char* ext) {
    if (raw == nullptr) raw = "";
    size_t i = 0;
    for (; raw[i] != '\0' && i < sizeof(nbuf_) - 8; ++i) nbuf_[i] = raw[i];
    // 已经带扩展名就不再追加
    if (i >= 4) {
        const char* tail = raw + i - 4;
        if (tail[0] == '.' && (ext == nullptr || strcmp(tail + 1, ext) == 0)) {
            nbuf_[i] = '\0';
            return nbuf_;
        }
    }
    if (ext != nullptr) {
        nbuf_[i++] = '.';
        for (const char* e = ext; *e != '\0' && i < sizeof(nbuf_) - 1; ++e) nbuf_[i++] = *e;
    }
    nbuf_[i] = '\0';
    return nbuf_;
}

void ScriptGame::on_start(Engine& e) {
    if (pkg_ == nullptr) return;
    if (pkg_->entry_init() != GamePackage::kNone) run(e, pkg_->entry_init());
    if (pkg_->entry_start() != GamePackage::kNone) run(e, pkg_->entry_start());
}

void ScriptGame::on_update(Engine& e, float dt) {
    dt_ms_ = dt * 1000.0f;
    dt_s_  = dt;
    ++frame_;
    if (pkg_ != nullptr && pkg_->entry_update() != GamePackage::kNone) {
        run(e, pkg_->entry_update());
    }
}

const Image* ScriptGame::cached_image(Engine& e, uint32_t strIdx) {
    if (pkg_ == nullptr || strIdx >= img_cache_.size()) return nullptr;
    if (img_done_[strIdx] != 0) return img_cache_[strIdx];
    img_done_[strIdx] = 1;                     // 先标记: 找不到也缓存住
    const char* raw = pkg_->string_at(strIdx);
    const char* nm = asset_name(raw, "img");
    img_cache_[strIdx] = e.img(nm);
    if (img_cache_[strIdx] == nullptr) {
        // 静默画不出来最难查 —— 脚本里 img("xxx") 写错/卡带里没这张图都走这里。
        // ⚠ 必须 fflush: stderr 在模拟器/固件里常被重定向成块缓冲, 进程被 kill 时
        //   没冲的日志会直接丢掉, 看上去就像"什么都没发生"。
        fprintf(stderr, "[script] 贴图找不到: \"%s\" (脚本里写的: \"%s\", "
                        "包内素材 %u 个)\n",
                nm, raw, (unsigned)pkg_->asset_count());
        fflush(stderr);
    }
    return img_cache_[strIdx];
}

void ScriptGame::draw_indexed(Engine& e, uint32_t strIdx, int x, int y, bool flip) {
    const Image* im = cached_image(e, strIdx);
    if (im != nullptr) e.display.draw_image_alpha(x, y, *im, kColorMagenta, flip);
}

void ScriptGame::on_render(Engine& e) {
    if (pkg_ != nullptr && pkg_->entry_render() != GamePackage::kNone) {
        run(e, pkg_->entry_render());
    }
}

int32_t ScriptGame::builtin_num(Engine& e, uint8_t fn, const int32_t* a, int argc) {
    switch (fn) {
        case FN_HELD:
            return is_held(e.input, (Button)(uint8_t)a[0]) ? 1 : 0;
        case FN_PRESSED:
            return is_pressed(e.input, (Button)(uint8_t)a[0]) ? 1 : 0;
        case FN_RELEASED:
            return is_released(e.input, (Button)(uint8_t)a[0]) ? 1 : 0;
        case FN_RND:
            return (a[0] > 0) ? (int32_t)(rand() % a[0]) : 0;
        case FN_RGB:
            return (int32_t)rgb565((uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2]);
        case FN_DT_MS:
            return (int32_t)dt_ms_;
        case FN_FRAME:
            return (int32_t)frame_;
        case FN_EXIT:
            quit_ = true;
            return 0;
        case FN_CLEAR:
            e.display.clear((Color)(uint16_t)a[0]);
            return 0;
        case FN_PX:
            e.display.set_pixel(a[0], a[1], (Color)(uint16_t)a[2]);
            return 0;
        case FN_RECT:
            e.display.draw_rect(a[0], a[1], a[2], a[3], (Color)(uint16_t)a[4]);
            return 0;
        case FN_FRECT:
            e.display.fill_rect(a[0], a[1], a[2], a[3], (Color)(uint16_t)a[4]);
            return 0;
        case FN_SCREEN_W:
            return (int32_t)e.display.width();
        case FN_SCREEN_H:
            return (int32_t)e.display.height();
        case FN_TEXTNUM: {
            // textnum(x, y, value, color, scale): 脚本没有字符串拼接, 靠它显示分数
            char buf[16];
            snprintf(buf, sizeof(buf), "%ld", (long)a[2]);
            const int scale = (a[4] < 1) ? 1 : (a[4] > 8 ? 8 : a[4]);
            e.display.text(a[0], a[1], buf, (Color)(uint16_t)a[3], scale);
            return 0;
        }
        // ---- 整数小工具 ----
        case FN_ABS:  return (a[0] < 0) ? -a[0] : a[0];
        case FN_IMIN: return (a[0] < a[1]) ? a[0] : a[1];
        case FN_IMAX: return (a[0] > a[1]) ? a[0] : a[1];
        // ---- 浮点: 返回值按位塞回 int32 ----
        case FN_ITOF: return as_i((float)a[0]);
        case FN_FTOI: return f_to_i(as_f(a[0]));
        case FN_FABS: { const float v = as_f(a[0]); return as_i(v < 0.0f ? -v : v); }
        case FN_DTF:  return as_i(dt_s_);
        case FN_FMIN: {
            const float x = as_f(a[0]), y = as_f(a[1]);
            return as_i(x < y ? x : y);
        }
        case FN_FMAX: {
            const float x = as_f(a[0]), y = as_f(a[1]);
            return as_i(x > y ? x : y);
        }
        case FN_FSQRT: {
            const float v = as_f(a[0]);
            return as_i(v > 0.0f ? sqrtf(v) : 0.0f);
        }
        default:
            return 0;
    }
}

int32_t ScriptGame::run(Engine& e, uint32_t pc) {
    if (pkg_ == nullptr) return 0;
    const uint8_t*   code = pkg_->code();
    const uint32_t   size = pkg_->code_size();
    const int32_t    nglob = (int32_t)globals_.size();
    int32_t* const   g = globals_.empty() ? nullptr : globals_.data();
    int sp = 0;
    // 函数调用栈: 只存返回地址 —— 参数/局部都在 globals_ 里(每个函数一块固定槽位)
    uint32_t callstack_[64];
    int csp = 0;

    for (;;) {
        if (pc >= size) break;
        const uint8_t op = code[pc++];
        switch (op) {
            case OP_HALT:
                return 0;
            case OP_PUSH: {
                if (pc + 4 > size) return 0;
                const int32_t v = (int32_t)rd32(code + pc);
                pc += 4;
                if (sp < kMaxStack) stack_[sp++] = v;
                break;
            }
            case OP_LOADG: {
                if (pc + 1 > size) return 0;
                const uint8_t idx = code[pc++];
                if (sp < kMaxStack) stack_[sp++] = (g != nullptr && idx < nglob) ? g[idx] : 0;
                break;
            }
            case OP_STOREG: {
                if (pc + 1 > size) return 0;
                const uint8_t idx = code[pc++];
                const int32_t v = (sp > 0) ? stack_[--sp] : 0;
                if (g != nullptr && idx < nglob) g[idx] = v;
                break;
            }
            case OP_POP:
                if (sp > 0) --sp;
                break;

            // ---- 内存/数组 ----
            // 数组就是 globals_ 的一段连续区间, u16 寻址(标量还是走 LOADG/STOREG 的 u8)
            case OP_LDMEM: {
                if (pc + 2 > size) return 0;
                const uint32_t ad = rd16(code + pc);
                pc += 2;
                if (sp < kMaxStack) stack_[sp++] = (g != nullptr && ad < (uint32_t)nglob) ? g[ad] : 0;
                break;
            }
            case OP_STMEM: {
                if (pc + 2 > size) return 0;
                const uint32_t ad = rd16(code + pc);
                pc += 2;
                const int32_t v = (sp > 0) ? stack_[--sp] : 0;
                if (g != nullptr && ad < (uint32_t)nglob) g[ad] = v;
                break;
            }
            case OP_LDIDX: {                       // 栈: [i] -> 压 mem[base+i]
                if (pc + 2 > size) return 0;
                const uint32_t base = rd16(code + pc);
                pc += 2;
                const int32_t i = (sp > 0) ? stack_[--sp] : 0;
                // 越界(含 i<0 / 回绕)一律读 0, 不能崩也不能读到别的数组
                const bool ok = (g != nullptr) && i >= 0 &&
                                ((uint32_t)i + base) < (uint32_t)nglob;
                if (sp < kMaxStack) stack_[sp++] = ok ? g[base + (uint32_t)i] : 0;
                break;
            }
            case OP_STIDX: {                       // 栈: [v, i] -> mem[base+i] = v
                if (pc + 2 > size) return 0;
                const uint32_t base = rd16(code + pc);
                pc += 2;
                const int32_t i = (sp > 0) ? stack_[--sp] : 0;
                const int32_t v = (sp > 0) ? stack_[--sp] : 0;
                if (g != nullptr && i >= 0 && ((uint32_t)i + base) < (uint32_t)nglob) {
                    g[base + (uint32_t)i] = v;
                }
                break;
            }

            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
            case OP_LT: case OP_LE: case OP_GT: case OP_GE: case OP_EQ: case OP_NE:
            case OP_LAND: case OP_LOR:
            case OP_AND: case OP_OR: case OP_XOR: case OP_SHL: case OP_SHR: {
                if (sp < 2) return 0;
                const int32_t b = stack_[--sp];
                const int32_t a2 = stack_[sp - 1];
                int32_t r = 0;
                switch (op) {
                    case OP_ADD: r = a2 + b; break;
                    case OP_SUB: r = a2 - b; break;
                    case OP_MUL: r = a2 * b; break;
                    case OP_DIV: r = (b == 0) ? 0 : a2 / b; break;   // 除零 -> 0, 不崩
                    case OP_MOD: r = (b == 0) ? 0 : a2 % b; break;
                    case OP_LT:  r = (a2 <  b); break;
                    case OP_LE:  r = (a2 <= b); break;
                    case OP_GT:  r = (a2 >  b); break;
                    case OP_GE:  r = (a2 >= b); break;
                    case OP_EQ:  r = (a2 == b); break;
                    case OP_NE:  r = (a2 != b); break;
                    case OP_LAND: r = (a2 != 0 && b != 0); break;
                    case OP_LOR:  r = (a2 != 0 || b != 0); break;
                    // 位运算: 迷宫里把 279 格压成 8bit/格 的整数数组就靠这几个
                    case OP_AND: r = a2 & b; break;
                    case OP_OR:  r = a2 | b; break;
                    case OP_XOR: r = a2 ^ b; break;
                    case OP_SHL: r = (int32_t)((uint32_t)a2 << (b & 31)); break;
                    case OP_SHR: r = (int32_t)(a2 >> (b & 31)); break;   // 算术右移
                    default: break;
                }
                stack_[sp - 1] = r;
                break;
            }
            case OP_NEG:
                if (sp > 0) stack_[sp - 1] = -stack_[sp - 1];
                break;
            case OP_LNOT:
                if (sp > 0) stack_[sp - 1] = (stack_[sp - 1] == 0) ? 1 : 0;
                break;

            // ---- 浮点(位模式即 IEEE-754; 类型由编译器静态保证) ----
            case OP_FADD: case OP_FSUB: case OP_FMUL: case OP_FDIV:
            case OP_FLT: case OP_FLE: case OP_FGT: case OP_FGE: case OP_FEQ: case OP_FNE:
            case OP_FLAND: case OP_FLOR: {
                if (sp < 2) return 0;
                const float b = as_f(stack_[--sp]);
                const float a2 = as_f(stack_[sp - 1]);
                int32_t r = 0;
                switch (op) {
                    case OP_FADD: r = as_i(a2 + b); break;
                    case OP_FSUB: r = as_i(a2 - b); break;
                    case OP_FMUL: r = as_i(a2 * b); break;
                    case OP_FDIV: r = as_i(b == 0.0f ? 0.0f : a2 / b); break;
                    case OP_FLT:  r = (a2 <  b); break;
                    case OP_FLE:  r = (a2 <= b); break;
                    case OP_FGT:  r = (a2 >  b); break;
                    case OP_FGE:  r = (a2 >= b); break;
                    case OP_FEQ:  r = (a2 == b); break;
                    case OP_FNE:  r = (a2 != b); break;
                    case OP_FLAND: r = (a2 != 0.0f && b != 0.0f); break;
                    case OP_FLOR:  r = (a2 != 0.0f || b != 0.0f); break;
                    default: break;
                }
                stack_[sp - 1] = r;
                break;
            }
            case OP_FNEG:                       // 浮点取负只翻符号位(不能用整型 NEG)
                if (sp > 0) stack_[sp - 1] ^= (int32_t)0x80000000;
                break;
            case OP_FLNOT:
                if (sp > 0) stack_[sp - 1] = (as_f(stack_[sp - 1]) == 0.0f) ? 1 : 0;
                break;
            case OP_ITOF:
                if (sp > 0) stack_[sp - 1] = as_i((float)stack_[sp - 1]);
                break;
            case OP_FTOI:
                if (sp > 0) stack_[sp - 1] = f_to_i(as_f(stack_[sp - 1]));
                break;

            case OP_JMP:
                if (pc + 2 > size) return 0;
                pc = rd16(code + pc);
                break;
            case OP_JZ:
                if (pc + 2 > size) return 0;
                if ((sp > 0 ? stack_[--sp] : 0) == 0) { pc = rd16(code + pc); }
                else { pc += 2; }
                break;
            case OP_JNZ:
                if (pc + 2 > size) return 0;
                if ((sp > 0 ? stack_[--sp] : 0) != 0) { pc = rd16(code + pc); }
                else { pc += 2; }
                break;

            case OP_CALLN: {
                if (pc + 2 > size) return 0;
                const uint8_t fn = code[pc++];
                const int argc = code[pc++];
                int32_t args[8] = {0};
                const int n = (argc > 8) ? 8 : argc;
                for (int i = n - 1; i >= 0; --i) {      // argc==0 时循环不执行
                    args[i] = (sp > 0) ? stack_[--sp] : 0;
                }
                const int32_t r = builtin_num(e, fn, args, argc);
                if (sp < kMaxStack) stack_[sp++] = r;
                break;
            }

            // ---- 脚本函数调用 ----
            case OP_CALL_USER: {
                if (pc + 2 > size) return 0;
                const uint32_t target = rd16(code + pc);
                if (csp >= (int)(sizeof(callstack_) / sizeof(callstack_[0]))) {
                    fprintf(stderr, "[script] 函数调用嵌套太深(脚本不支持递归)\n");
                    fflush(stderr);
                    return 0;
                }
                callstack_[csp++] = pc + 2;
                pc = target;
                break;
            }
            case OP_RET:
                if (csp <= 0) return 0;              // 没有对应 CALL -> 包坏了
                pc = callstack_[--csp];
                break;

            case OP_IMG: {
                if (pc + 2 > size) return 0;
                const uint32_t si = rd16(code + pc);
                pc += 2;
                const int32_t x = (sp > 0) ? stack_[--sp] : 0;
                const int32_t y = (sp > 0) ? stack_[--sp] : 0;
                draw_indexed(e, si, x, y, false);
                break;
            }
            // IMGF: 名字写死, 翻转来自栈(左/右朝向共用一张图) 栈: [flip, y, x]
            case OP_IMGF: {
                if (pc + 2 > size) return 0;
                const uint32_t si = rd16(code + pc);
                pc += 2;
                const int32_t x = (sp > 0) ? stack_[--sp] : 0;
                const int32_t y = (sp > 0) ? stack_[--sp] : 0;
                const int32_t f = (sp > 0) ? stack_[--sp] : 0;
                draw_indexed(e, si, x, y, f != 0);
                break;
            }
            // IMGV: 名字来自字符串数组, 下标是运行时的 —— 动画帧靠它 栈: [flip, idx, y, x]
            case OP_IMGV: {
                if (pc + 4 > size) return 0;
                const uint32_t base = rd16(code + pc);
                const uint32_t cnt  = rd16(code + pc + 2);
                pc += 4;
                const int32_t x = (sp > 0) ? stack_[--sp] : 0;
                const int32_t y = (sp > 0) ? stack_[--sp] : 0;
                const int32_t i = (sp > 0) ? stack_[--sp] : 0;
                const int32_t f = (sp > 0) ? stack_[--sp] : 0;
                if (i >= 0 && (uint32_t)i < cnt) {
                    draw_indexed(e, base + (uint32_t)i, x, y, f != 0);
                }
                break;
            }
            // IMGW/IMGH: 压贴图宽/高(摆位、判定框要对齐时要用)
            case OP_IMGW:
            case OP_IMGH: {
                if (pc + 2 > size) return 0;
                const uint32_t si = rd16(code + pc);
                pc += 2;
                const Image* im = cached_image(e, si);
                int32_t v = 0;
                if (im != nullptr) v = (op == OP_IMGW) ? (int32_t)im->w : (int32_t)im->h;
                if (sp < kMaxStack) stack_[sp++] = v;
                break;
            }
            // SCHR: 读字符串表里某个字符串的第 pos 个字符(0 = 越界)。
            // 用途: 把字符画(迷宫/关卡图)直接写成字符串表里的字符串, 源码里看得懂。
            // 栈: [idx, pos]
            case OP_SCHR: {
                if (pc + 4 > size) return 0;
                const uint32_t base = rd16(code + pc);
                const uint32_t cnt  = rd16(code + pc + 2);
                pc += 4;
                const int32_t pos = (sp > 0) ? stack_[--sp] : 0;
                const int32_t idx = (sp > 0) ? stack_[--sp] : 0;
                int32_t code = 0;
                if (idx >= 0 && (uint32_t)idx < cnt && pos >= 0) {
                    const char* s = pkg_->string_at(base + (uint32_t)idx);
                    if ((size_t)pos < strlen(s)) code = (int32_t)(unsigned char)s[pos];
                }
                if (sp < kMaxStack) stack_[sp++] = code;
                break;
            }
            case OP_TEXT: {
                if (pc + 3 > size) return 0;
                const char* s = pkg_->string_at(rd16(code + pc));
                const uint8_t scale = code[pc + 2];
                pc += 3;
                const int32_t x = (sp > 0) ? stack_[--sp] : 0;
                const int32_t y = (sp > 0) ? stack_[--sp] : 0;
                const int32_t c = (sp > 0) ? stack_[--sp] : 0;
                e.display.text(x, y, s, (Color)(uint16_t)c, scale == 0 ? 1 : scale);
                break;
            }
            case OP_SFX: {
                if (pc + 2 > size) return 0;
                const char* nm = asset_name(pkg_->string_at(rd16(code + pc)), "snd");
                pc += 2;
                const Clip* c = e.snd(nm);
                if (c != nullptr) e.audio.play_sfx(c);
                break;
            }
            case OP_BGM: {
                if (pc + 2 > size) return 0;
                const char* nm = asset_name(pkg_->string_at(rd16(code + pc)), "snd");
                pc += 2;
                const Clip* c = e.snd(nm);
                if (c != nullptr) e.audio.play_bgm(c, 0.9f);
                break;
            }
            default:
                // 未知指令: 停机而不是乱走(坏包/版本不匹配时更安全)。
                // 但**必须吭声** —— 静默停机会让"少了某条指令"表现成"画面少点东西",
                // 极难查。VM v2 加 IMGW 时就踩过: 漏实现导致 on start 半路停掉,
                // 而自检里的 imgw() 断言根本没执行, 看上去却是 PASS。
                fprintf(stderr, "[script] 未知指令 0x%02X @ %u (包/VM 版本不匹配?)\n",
                        (unsigned)op, (unsigned)(pc - 1));
                fflush(stderr);
                return 0;
        }
    }
    return 0;
}

} // namespace engine
