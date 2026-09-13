# STARFALL —— 用游戏脚本写的示例(演示 VM 能力)
# 玩法: 左右移动接住掉落的星星, 漏掉扣一条命。
# 打包: py -3 tools\build_game.py starfall

var ship_x = 100
var score = 0
var lives = 3
var star_x = 100
var star_y = 0
var star_vy = 2
var t = 0
var W = 428        # 默认值, on start 里会用 screen_w() 覆盖
var H = 142        # 别写死分辨率: 模拟器与实机都是 428x142

on start {
    W = screen_w()
    H = screen_h()
    ship_x = (W - 36) / 2
    star_x = rnd(W - 24)
    star_y = 0
    star_vy = 2
    score = 0
    lives = 3
    bgm("bgm")
}

on update {
    t = t + 1

    # 左右移动, 限制在画面内
    if held(LEFT)  { ship_x -= 4 }
    if held(RIGHT) { ship_x += 4 }
    if ship_x < 4 { ship_x = 4 }
    if ship_x > W - 40 { ship_x = W - 40 }

    # 星星下落
    star_y = star_y + star_vy

    # 接住判定(飞船贴着底边)
    if star_y > H - 34 {
        if star_y < H - 6 {
            if star_x > ship_x - 16 {
                if star_x < ship_x + 36 {
                    score = score + 10
                    sfx("coin")
                    star_y = 0
                    star_x = rnd(W - 24)
                    if star_vy < 6 { star_vy = star_vy + 1 }
                }
            }
        }
    }

    # 漏接
    if star_y > H - 4 {
        star_y = 0
        star_x = rnd(W - 24)
        lives = lives - 1
        sfx("miss")
        if lives <= 0 {
            lives = 3
            score = 0
            star_vy = 2
        }
    }
}

on render {
    clear(RGB(6, 8, 26))

    # 背景星点(用帧计数做滚动)
    frect(20, t % H, 2, 2, RGB(70, 80, 120))
    frect(110, (t * 2) % H, 2, 2, RGB(70, 80, 120))
    frect(230, (t * 3) % H, 2, 2, RGB(70, 80, 120))
    frect(380, (t + 60) % H, 2, 2, RGB(70, 80, 120))

    # 掉落的星星
    img("star", star_x, star_y)

    # 飞船贴着底边
    frect(ship_x, H - 14, 36, 8, RGB(120, 225, 235))
    frect(ship_x + 12, H - 20, 12, 6, RGB(205, 245, 240))

    # HUD
    text(4, 4, "SCORE", RGB(230, 245, 238), 1)
    textnum(44, 4, score, RGB(250, 200, 70), 1)
    text(110, 4, "LIVES", RGB(160, 200, 210), 1)
    textnum(152, 4, lives, RGB(235, 90, 90), 1)
}
