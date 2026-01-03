#ifndef STRUCTS_H
#define STRUCTS_H

#include <Qt>
#include <QString>
#include <QDateTime>

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

// 游玩记录结构 (用于保存到 JSON)
struct PlayRecord {
    QString songHash; // 唯一标识 (Artist + Title + Version)
    int score;
    double acc;
    int combo;
    int maxCombo;
    QString grade;
    int countPerfect;
    int countGreat;
    int countGood;
    int countMiss;

    // 记录当时的判定难度，方便回看
    JudgmentWindow usedJudgeWindow;
    QDateTime timestamp;
};

struct GameConfig {
    double scrollSpeed = 0.9;
    int gameWidth = 500;
    int audioOffset = 0;
    QString songFolder = "";

    int preGameDelay = 2000; // 默认 2000ms (2秒)
    int keyMapping[4] = { Qt::Key_D, Qt::Key_F, Qt::Key_J, Qt::Key_K };
    JudgmentWindow judgeWindow;
};

struct BeatmapInfo {
    QString filePath;
    QString title;
    QString artist;
    QString version; // 难度名
    QString audioFilename;
    // 生成一个唯一ID用于关联成绩
    QString getHash() const { return artist + title + version; }
};

#endif // STRUCTS_H
