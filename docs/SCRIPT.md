# 游戏脚本 (`game.gs`) 与字节码规范

引擎的"游戏"不再编译进固件，而是由**脚本 + 素材**打包成单个 `.gbn`（game binary）文件，
键盘固件/模拟器在运行时加载并执行。VM 是平台无关的纯 C++17，不依赖 LVGL。

## 1. 语言

```
# 这是注释
var px = 40            # 全局变量(整数, 最多 64 个, 默认 0)
var score = 0

on start {
    bgm("bgm")         # 进入游戏时执行一次
}

on update {
    if held(LEFT)  { px -= 2 }
    if held(RIGHT) { px += 2 }
    if pressed(A)  { sfx("shoot"); score += 10 }
}

on render {
    clear(RGB(4, 6, 20))
    img("player", px, 40)
    text(4, 4, "SCORE", RGB(255, 255, 255), 1)
}
```

要点：

- **两种数值类型: `int`(32 位有符号) 与 `float`(IEEE-754 单精度)**。
  类型在编译期完全推导出来，运行时同一个 32 位 cell 直接存浮点位模式，没有任何装箱/标签开销。
  混合运算自动提升(`*3` 遇到浮点就转浮点)；要显式转换用 `itof()` / `ftoi()`。
  **不要用浮点做跨平台存档/哈希**，但做物理和计时是完全确定的。
- 字符串只能作为内置函数的字面量参数，不能赋值给变量；但可以用 `strs` 声明**字符串数组**，
  下标可以是运行时的整数（动画帧名靠它）。
- 数组用 `var m[64]` 声明，元素类型跟初值走（`var m[64] = 0` 是整数数组，
  `var m[8] = 0.0` 是浮点数组）。所有元素初始都清零。
- `on start` / `on update` / `on render` 三个代码块都可省略。
- 表达式：
  - 算术 `+ - * / %`（`%` 只能用于整数）
  - 比较 `== != < <= > >=`
  - 逻辑 `&& || !`
  - 位 `& | ^ << >>`（只能用于整数）
  - 括号、整数（含 `0x` 前缀）、浮点（如 `1.5`、`0.25`）
- 语句：`var` / `strs` 声明、赋值、`+= -= *= /= %= <<= >>= &= |= ^=` 复合赋值、
  `if / else if / else`、`while`、`break`、`continue`、表达式语句。
- `/` 在两个整数之间是整除；`%` 取模（负数行为同 C）。浮点除法用 `/` 即可（参与运算的是浮点就自动走 FDIV）。
- 优先级（低到高）：`||` `&&` `|` `^` `&` 比较 `<< >>` `+ -` `* / %`。

### 数组与内存

标量和数组共用一块内存池（`.gbn` 头里的 `globalCount`），**合计最多 2048 个字**。
越界读写不会崩 — 读出来是 0，写进去被丢弃（所以下标算错也不会破坏别的数组）。

```
var maze[64] = 0        # 整数数组, 全长 64, 初值 0
var pos[8] = 0.0        # 浮点数组

on update {
    maze[3] = maze[1] << 2 | 1     # 下标可以是表达式
    pos[0] += 0.25                 # 复合赋值(下标只求值一次)
}
```

传数组长度可以用 `bit` 运算自己压缩 —— 例：9x31 的迷宫用 8bit/格 塞进 35 个整数。

### 函数

```
fn gadd(a, b) {
    var t = a + b          # 局部变量, 和参数一起存在这个函数自己的内存块里
    return t
}

fn gscale(v = 0.0, k = 1.0) -> 0.0 {     # 浮点参数(带默认值) + 浮点返回
    return v * k
}

fn greset() {                            # 无返回值, 当语句用
    got[0] = 0
}
```

- **`fn` 必须写在调用它的代码之前**，也不能嵌套定义。
- **参数写 `名字 = 字面量` 就表示它是浮点**（那个字面量同时是默认值）；只写名字 = 整数。
  调用时少传的参数会用默认值补齐，没有默认值的参数必须传。
- **返回类型写 `-> 0`（整数）或 `-> 0.0`（浮点），省略就是整数。**
  这一点很要紧：忘了写 `-> 0.0` 时浮点结果会**静默截断**成整数（`2.5` 变 `2`）。
- `return` 可以提前返回；不写 `return` 的函数返回 0。
- 每个函数有自己的**固定内存块**（返回值 + 参数 + 局部变量），所以
  **不支持递归**（递归会把自己的参数盖掉），也不支持函数指针。
- 参数只能是数值，不能传数组。需要传数组就传下标，让函数自己访问全局数组。
- 名字解析：局部（含参数）→ 全局。局部可以遮蔽全局，包括按钮常量
  （函数里有个叫 `a` 的参数，那个函数里的 `a` 就是参数，不是 A 键；出了这个函数照旧是 A 键）。


### 按键常量

`UP DOWN LEFT RIGHT A B C D START SELECT L R X Y` → 0..13（对应 `engine::Button`）

### 内置函数

| 函数 | 说明 |
|---|---|
| `held(btn)` / `pressed(btn)` / `released(btn)` | 手柄状态，返回 0/1 |
| `rnd(n)` | 0..n-1 随机数 |
| `RGB(r,g,b)` | 组装 RGB565 颜色 |
| `dt_ms()` | 上一帧耗时（毫秒） |
| `frame()` | 帧计数 |
| `exit()` | 请求退出游戏 |
| `clear(color)` | 清屏 |
| `screen_w()` / `screen_h()` | 画面宽/高（游戏应据此自适应，不要写死分辨率） |
| `textnum(x, y, value, color, scale)` | 画整数（脚本没有字符串拼接，显示分数靠它） |
| `px(x, y, color)` | 画点 |
| `rect(x, y, w, h, color)` | 空心矩形 |
| `frect(x, y, w, h, color)` | 实心矩形 |
| `img("name", x, y)` | 画图（品红透明，名字同 `assets-src` 相对名） |
| `text(x, y, "str", color, scale)` | 画字（引擎内置 5x7 字体） |
| `sfx("name")` | 播放音效 |
| `bgm("name")` | 播放背景音乐（循环） |
| `imgw("name")` / `imgh("name")` | 贴图的宽 / 高（摆放和对齐用） |
| `chr(strs[i], pos)` / `chr("str", pos)` | 字符串里第 pos 个字符的**字符码**（0 = 越界）。
  把地图/关卡直接写成字符串表，源码里就是一张字符画 |
| `abs(x)` `imin(a,b)` `imax(a,b)` | 整数工具 |
| `itof(n)` / `ftoi(x)` | int <-> float（隐式提升之外的手动转换） |
| `fabs(x)` `fmin(a,b)` `fmax(a,b)` `fsqrt(x)` | 浮点工具 |
| `dtf()` | 上一帧耗时（**秒**，浮点；做物理用这个，不要用 `dt_ms()`） |

### 字符串数组（动态帧名）

`img()` 的名字本来必须是字面量（字节码里是一个字符串下标）。要让**运行时的整数**选帧，
用 `strs` 声明一张名字表，下标就可以是任意整数：

```
strs walk = { "keyspr_walk0", "keyspr_walk1", "keyspr_walk2", "keyspr_walk3" }

on render {
    img(walk[f], x, y)          # f 是运行时算出来的帧号
    img(walk[f], x, y, flip)    # 第 4 个参数是水平翻转(0/1)，角色朝左朝右共用一张图
}
```

图标名字本身写死时就不需要 `strs`，直接 `img("ship_f0", x, y)` 即可。

## 2. 字节码

栈式虚拟机，操作数为 32 位整数（浮点就是它的位模式），栈只在表达式求值中使用。

```
0x00 HALT
0x01 PUSH    i32           浮点字面量直接压它的 IEEE-754 位模式
0x02 LOADG   u8            读标量
0x03 STOREG  u8            写标量
0x04 POP                   丢弃表达式结果(空返回值的内置函数)
0x05 LDMEM   u16           压 mem[addr]           (标量下标 > 255 时编译器也用它)
0x06 STMEM   u16
0x07 LDIDX   u16 base      弹 i, 压 mem[base+i]
0x08 STIDX   u16 base      弹 i, 弹 v -> mem[base+i]=v
0x10 ADD   0x11 SUB   0x12 MUL   0x13 DIV   0x14 MOD
0x15 NEG   0x16 LNOT
0x17 LT    0x18 LE    0x19 GT    0x1A GE    0x1B EQ    0x1C NE
0x1D LAND  0x1E LOR   0x1F AND
0x20 JMP    u16(绝对地址)
0x21 JZ     u16            弹栈, 为 0 跳转
0x22 JNZ    u16
0x23 OR    0x24 XOR   0x25 SHL   0x26 SHR          (移位量先 &31, SHR 是算术右移)
0x30 CALLN  u8 fnId  u8 argc   数值内置函数, 结果压栈(无返回值压 0)
0x31 CALL   u16 addr          调脚本函数; 返回地址进**独立的调用栈**(不进值栈)
0x32 RET                      弹调用栈并跳回
0x40 IMG    u16 strIdx         弹 y, x
0x41 TEXT   u16 strIdx  u8 scale   弹 color, y, x
0x42 SFX    u16 strIdx
0x43 BGM    u16 strIdx
0x44 IMGF   u16 strIdx         弹 flip, y, x      (flip != 0 则水平翻转)
0x45 IMGV   u16 strBase u16 count  弹 flip, idx, y, x   名字 = strBase+idx
0x46 IMGW   u16 strIdx         压宽
0x47 IMGH   u16 strIdx         压高
0x48 SCHR   u16 strBase u16 count   弹 pos, idx -> 压字符码(0 = 越界)
0x50 FADD  0x51 FSUB  0x52 FMUL  0x53 FDIV  0x54 FNEG
0x55 FLT   0x56 FLE   0x57 FGT   0x58 FGE   0x59 FEQ   0x5A FNE
0x5B ITOF  0x5C FTOI  0x5D FLAND 0x5E FLOR  0x5F FLNOT
```

浮点指令与整型指令**不能混用**：同一个 32 位 cell，整型指令当补码、浮点指令当 IEEE-754。
类型错了不会报错，只会算出垃圾——这是编译器静态推导类型的全部理由。

数值内置函数 id：

```
0 HELD 1 PRESSED 2 RELEASED 3 RND 4 RGB 5 DT_MS 6 FRAME 7 EXIT
8 CLEAR 9 PX 10 RECT 11 FRECT 12 SCREEN_W 13 SCREEN_H 14 TEXTNUM
15 ABS 16 IMIN 17 IMAX 18 ITOF 19 FTOI 20 FABS 21 DTF
22 FMIN 23 FMAX 24 FSQRT
```

每个代码段的末尾由编译器补 `HALT`。

> 想在没有 Qt/硬件的环境里跑一跑：`py -3 tools\gs_run.py games-bin\xxx.gbn --frames 60 --png out.png`
> （`tools/gs_run.py` 是同一套字节码的参考实现，带软件帧缓冲，移植游戏时比开模拟器快得多。）

## 3. `.gbn` 包格式

```
偏移 0  : "GBN1"                    (4 字节 magic)
偏移 4  : u16 version   = 1
偏移 6  : u16 globalCount      标量 + 数组元素 + 1 个隐藏临时槽, 上限 2048
偏移 8  : u32 onInitPC   (0xFFFFFFFF = 无)
偏移 12 : u32 onStartPC  (0xFFFFFFFF = 无)
偏移 16 : u32 onUpdatePC (0xFFFFFFFF = 无)
偏移 20 : u32 onRenderPC (0xFFFFFFFF = 无)
偏移 24 : u32 codeSize
偏移 28 : u32 stringCount
偏移 32 : u32 assetCount
--- 之后依次 ---
code      : codeSize 字节
strings   : stringCount 项, 每项 u16 长度 + UTF-8 字节
assets    : assetCount 项, 每项
            u16 nameLen + name(UTF-8, 含扩展名, 如 "player.img")
            u32 size
            size 字节(就是 assets-packed 里 .img/.snd 文件的完整内容)
```

> 素材在包里**保持 `IMG1`/`SND1` 原样**, 所以运行时的 AssetStore 不需要改动; `GamePackage`
> 自身就是一个 `FileSystem`, 直接 `engine.assets.set_fs(&pkg)` 即可按名字取用。
