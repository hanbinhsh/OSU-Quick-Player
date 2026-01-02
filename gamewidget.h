#ifndef GAMEWIDGET_H
#define GAMEWIDGET_H

#include <QOpenGLWidget> // 替换 QWidget
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QTimer>
#include <QElapsedTimer> // 必须引用
#include <vector>
#include "Structs.h"

// 继承 QOpenGLWidget 以获得硬件加速
class GameWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit GameWidget(QWidget *parent = nullptr);
    void loadBeatmap(const QString &filePath);
    void updateConfig(const GameConfig &config);
    GameConfig getConfig() const { return m_config; }
    int getScore() const { return m_score; }

protected:
    void paintEvent(QPaintEvent *event) override; // 依然使用 paintEvent，Qt会自动用OpenGL处理
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool focusNextPrevChild(bool next) override;

signals:
    // === 新增信号：通知 UI 更新 ===
    void statsChanged(int perfect, int great, int good, int miss, int combo, int maxCombo, int score, double acc);
    void songLoaded(QString title, QString artist, qint64 duration);
    void progressChanged(qint64 current, qint64 total);

private slots:
    void gameLoop();

private:
    void checkHit(int column);
    void checkRelease(int column);
    void resetGame();
    qint64 getSmoothTime() const;

    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    QTimer *m_timer;

    // === 核心修改：视觉时间同步器 ===
    QElapsedTimer m_visualTimer; // 高精度计时器
    qint64 m_visualTimeOffset = 0; // 用于暂停/继续时的偏移补偿

    std::vector<Note> m_notes;
    GameConfig m_config;

    bool m_isPlaying = false;
    bool m_keysPressed[4] = {false};

    int m_score = 0;
    int m_combo = 0;
    int m_maxCombo = 0;
    int m_totalHits = 0;
    double m_totalAccWeight = 0;

    int m_countPerfect = 0;
    int m_countGreat = 0;
    int m_countGood = 0;
    int m_countMiss = 0;

    QString m_lastJudgmentText;
    QColor m_lastJudgmentColor;
    int m_feedbackTimer = 0;

    QString m_currentTitle;
    QString m_currentArtist;
    qint64 m_songDuration = 0;

    void calculateScore(int weight); // 新增：统一算分函数
    QString getGrade() const;        // 新增：获取评级字符

    double m_currentRawScore = 0; // 当前累积的权重分 (Perfect=300, Miss=0)
    double m_maxPossibleScore = 1; // 理论最大权重分 (总Note数 * 300)

    void saveSettings();
    void loadSettings();
};

#endif // GAMEWIDGET_H
