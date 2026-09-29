# 贪吃蛇 Snake

基于 **Qt 6 (C++)** 的经典贪吃蛇小游戏，卡通画风，支持难度档位、障碍物、最高分记录与音效。

## 特性

- 三档难度（简单 / 普通 / 困难），速度曲线、食物数、障碍物数各不相同
- 困难档带随机障碍物，撞上即死
- 穿墙玩法：蛇从边界穿出后从对面绕回
- 最高分按难度分别记录，本地持久化（QSettings / ini）
- 结束统计面板：得分、最高纪录、存活时长、难度
- 音效（吃食物 / 死亡）+ 循环背景音乐
- 卡通蛇造型：连续管状身体 + 圆头大眼

## 操作

| 按键 | 作用 |
| --- | --- |
| ← / → | 准备 / 暂停 / 结束时切换难度；跑动中为转向 |
| ↑ / ↓ | 转向 |
| 空格 | 开始 / 暂停 / 继续 / 结束后回难度选择 |
| R | 重新开始本难度 |
| Esc | 退出游戏 |

## 难度

| 档位 | 初速 | 同时食物数 | 障碍物 |
| --- | --- | --- | --- |
| 简单 | 慢 | 3 | 无 |
| 普通 | 中 | 2 | 无 |
| 困难 | 快 | 2 | 6 |

## 环境要求

- Qt 6.5+（Widgets、Multimedia 模块）
- CMake 3.16+
- 编译器：MinGW / GCC / MSVC 均可

## 构建与运行

```bash
cmake -S . -B build
cmake --build build
./build/Snake          # Windows 下为 build/Snake.exe
```

也可以直接用 Qt Creator 打开 `CMakeLists.txt` 运行。

## 目录结构

```
Snake/
├── CMakeLists.txt      # 构建配置
├── main.cpp            # 程序入口
├── snakewindow.h       # 游戏主窗口声明
├── snakewindow.cpp     # 游戏逻辑 + QPainter 渲染
├── resources.qrc       # 资源清单（背景图 / 图标 / 音效 / 音乐）
└── src/                # 资源文件（图片、音频）
```

## 技术栈

- Qt 6 Widgets：界面与 QPainter 自绘渲染
- Qt Multimedia：QSoundEffect（短音效）+ QMediaPlayer（背景音乐）
- QSettings：最高分本地持久化
