#ifndef STRUCTS_H
#define STRUCTS_H

#include <Qt>

// 单个音符结构
struct Note {
    int column;      // 轨道 0-3
    int time;        // 开始时间 (ms)
    int endTime;     // 结束时间 (如果是普通 Note，这里等于 time)
    bool isHold;     // 是否是长条
    bool isHit;      // 是否被打击
    bool isHolding;  // 是否正在按住中 (且头部已击中)
    bool isMissed;   // 是否已错过
};

// ... (JudgmentWindow 和 GameConfig 保持不变) ...
struct JudgmentWindow {
    int perfect = 40;
    int great = 80;
    int good = 120;
    int miss = 150;
};

struct GameConfig {
    double scrollSpeed = 0.9;
    int keyMapping[4] = { Qt::Key_D, Qt::Key_F, Qt::Key_J, Qt::Key_K };
    JudgmentWindow judgeWindow;
    int gameWidth = 500;
};

#endif // STRUCTS_H
