// 本文件由 tools/gen_keychase.py 生成 —— 改迷宫请改生成器里的 MAZE, 别手改这里。
#pragma once

namespace games { namespace keychase {

constexpr int kMazeCols = 30;
constexpr int kMazeRows = 9;

// 瓦片: '#' 墙  '.' 豆  ' ' 空地  '-' 鬼屋门(只有鬼能过)
constexpr char kMaze[kMazeRows][kMazeCols + 1] = {
    "##############################",
    "#o............##............o#",
    "####.####.###.######.####.####",
    "#...........#-####...........#",
    "  ..........#    #..........  ",
    "#...........######...........#",
    "######.####.######.#####.#####",
    "#o............ .............o#",
    "##############################",
};

constexpr int kDotCount   = 122;
constexpr int kPowerCount = 4;
constexpr int kStartCol   = 14;
constexpr int kStartRow   = 7;

constexpr int kHomeCount  = 4;
constexpr int kHomeCol[kHomeCount] = {13, 14, 15, 16};
constexpr int kHomeRow[kHomeCount] = {4, 4, 4, 4};

constexpr int kScatterCol[4] = {1, 28, 28, 1};
constexpr int kScatterRow[4] = {1, 1, 7, 7};

constexpr int kDoorCol   = 13;
constexpr int kDoorRow   = 3;
constexpr int kFruitCol  = 15;
constexpr int kFruitRow  = 7;

}} // namespace games::keychase
