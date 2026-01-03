#include "GameWidget.h"
#include <QPainter>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QKeyEvent>
#include <cmath>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDebug>
#include <QPainterPath>

GameWidget::GameWidget(QWidget *parent) : QOpenGLWidget(parent) { // 构造函数改为 QOpenGLWidget
    setFocusPolicy(Qt::StrongFocus);

    // 初始化音频
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

    // 游戏循环定时器 (~60 FPS)
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &GameWidget::gameLoop);
    m_timer->start(4);

    loadSettings();
}

qint64 GameWidget::getSmoothTime() const {
    if (m_preGameCountingDown) {
        // 在倒计时期间，实际游戏时间应该是负数，或者从0开始，这样Note才会在屏幕上方
        // smoothTime = 经过的时间 - 延迟时间 - 额外偏移
        return m_visualTimer.elapsed() - (m_preGameStartTime + m_config.preGameDelay) - m_config.audioOffset;
    }
    // 游戏开始后，就按照正常逻辑
    return m_visualTimer.elapsed() - m_preGameStartTime - m_config.audioOffset;
}

void GameWidget::updateConfig(const GameConfig &config) {
    m_config = config;
    saveSettings();
}

void GameWidget::resetGame() {
    m_player->stop();
    m_isPlaying = false;

    m_preGameCountingDown = false;
    m_preGameStartTime = 0;

    m_score = 0; m_combo = 0; m_maxCombo = 0;
    m_totalHits = 0; m_totalAccWeight = 0;

    // 重置详细统计
    m_countPerfect = 0;
    m_countGreat = 0;
    m_countGood = 0;
    m_countMiss = 0;

    m_lastJudgmentText = "";

    for(auto &note : m_notes) {
        note.isHit = false; note.isMissed = false;
        note.isHolding = false;
    }

    // 通知 UI 清零
    emit statsChanged(0, 0, 0, 0, 0, 0, 0, 100.0);
    emit progressChanged(0, 1);
}

void GameWidget::loadBeatmap(const QString &filePath) {
    resetGame();
    m_notes.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    bool inHitObjects = false;
    QString audioFilename;
    QDir dir = QFileInfo(filePath).absoluteDir();

    m_currentTitle = "Unknown Title";
    m_currentArtist = "Unknown Artist";

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();

        // 解析歌曲信息
        if (line.startsWith("Title:")) m_currentTitle = line.mid(6).trimmed();
        else if (line.startsWith("TitleUnicode:")) m_currentTitle = line.mid(13).trimmed(); // 优先用 Unicode
        else if (line.startsWith("Artist:")) m_currentArtist = line.mid(7).trimmed();
        else if (line.startsWith("ArtistUnicode:")) m_currentArtist = line.mid(14).trimmed();
        else if (line.startsWith("Version:")) m_currentVersion = line.mid(8).trimmed();

        if (line.startsWith("AudioFilename:")) {
            audioFilename = line.split(":").last().trimmed();
        } else if (line == "[HitObjects]") {
            inHitObjects = true;
            continue;
        }

        if (inHitObjects && !line.isEmpty()) {
            QStringList parts = line.split(',');
            if (parts.size() > 2) {
                double x = parts[0].toDouble();
                int time = parts[2].toInt();
                int type = parts[3].toInt(); // 读取 Type 字段

                int col = std::floor(x * 4 / 512);
                col = std::max(0, std::min(col, 3));

                // === 解析长条逻辑 ===
                bool isHold = (type & 128) != 0; // Bit 7 set = Hold Note
                int endTime = time;

                if (isHold && parts.size() > 5) {
                    // 长条格式: x,y,time,type,hitSound,endTime:extras
                    QString extraPart = parts[5];
                    endTime = extraPart.split(':').first().toInt();
                }

                // 推入 vector
                m_notes.push_back({col, time, endTime, isHold, false, false});
            }
        }
    }

    std::sort(m_notes.begin(), m_notes.end(), [](const Note& a, const Note& b){
        return a.time < b.time;
    });

    int totalJudgments = 0;
    for (const auto& note : m_notes) {
        totalJudgments++; // 头部
        if (note.isHold) totalJudgments++; // 尾部
    }

    m_maxPossibleScore = (totalJudgments == 0) ? 1 : totalJudgments * 300.0;
    m_currentRawScore = 0;
    m_score = 0;

    // 音频加载逻辑
    QString audioPath = dir.filePath(audioFilename);
    if (!QFile::exists(audioPath)) {
        QStringList filters; filters << "*.mp3" << "*.ogg" << "*.wav";
        dir.setNameFilters(filters);
        if (!dir.entryList().isEmpty()) audioPath = dir.filePath(dir.entryList().first());
    }
    if (QFile::exists(audioPath)) {
        m_player->setSource(QUrl::fromLocalFile(audioPath));

        // 1. 获取最后一个 Note 的时间
        int lastNoteTime = m_notes.empty() ? 0 : m_notes.back().endTime;

        // 2. 先设置一个保底时长 (最后 Note + 3秒)
        m_songDuration = lastNoteTime + 3000;

        // 3. 连接 duration 信号
        disconnect(m_player, &QMediaPlayer::durationChanged, nullptr, nullptr);
        connect(m_player, &QMediaPlayer::durationChanged, this, [this, lastNoteTime](qint64 dur){
            if (dur > 0) {
                // 取 音频时长 和 谱面结束+3s 的最大值
                m_songDuration = std::max((qint64)lastNoteTime + 3000, dur);
                qDebug() << "Duration Updated:" << m_songDuration;
                emit songLoaded(m_currentTitle, m_currentArtist, m_songDuration);
            }
        });

        m_visualTimer.restart(); // 视觉计时器开始跑，用于倒计时
        m_preGameCountingDown = true; // 标记进入倒计时状态
        m_preGameStartTime = m_visualTimer.elapsed(); // 记录倒计时开始时刻
        m_isPlaying = false; // 游戏本身还没开始，只是在倒计时

        qDebug() << "Pre-game countdown started for" << m_config.preGameDelay << "ms.";
        // === 发射信号：通知主窗口歌曲加载完毕 ===
        emit songLoaded(m_currentTitle, m_currentArtist, m_songDuration);
        qDebug() << "Game Started. Initial Duration:" << m_songDuration;
    }
}

void GameWidget::gameLoop() {
    // 1. 处理倒计时状态
    if (m_preGameCountingDown) {
        qint64 elapsedSinceCountdownStart = m_visualTimer.elapsed() - m_preGameStartTime;
        if (elapsedSinceCountdownStart >= m_config.preGameDelay) {
            // 倒计时结束，真正开始游戏！
            m_preGameCountingDown = false;
            m_isPlaying = true; // 游戏正式开始
            m_player->play(); // 播放音乐

            // 重启 visualTimer 以确保 getSmoothTime 能够从 0 准确开始
            m_visualTimer.restart();
            // 这里的 m_preGameStartTime 现在代表的是游戏真正开始的时刻 (0ms)
            m_preGameStartTime = m_visualTimer.elapsed();

            qDebug() << "Game started after delay. Playing music.";
        }
        update(); // 倒计时期间也要刷新画面
        return; // 倒计时期间不执行后续的游戏逻辑
    }

    if (!m_isPlaying) {
        update();
        return;
    }

    qint64 audioTime = m_player->position();
    qint64 currentTime = getSmoothTime();

    bool timeIsUp = (m_songDuration > 0 && currentTime > m_songDuration + 1000);
    bool playerStopped = (currentTime > 1000 && m_player->playbackState() == QMediaPlayer::StoppedState);


    if (timeIsUp || playerStopped) {
        qDebug() << "Game Over Triggered! Time:" << currentTime << "Duration:" << m_songDuration;
        saveRecord();
        m_isPlaying = false;
        m_player->stop();
        update();
        return; // 结束本帧
    }

    qint64 displayTime = currentTime;
    if (m_songDuration > 0 && displayTime > m_songDuration) {
        displayTime = m_songDuration;
    }
    if (m_songDuration > 0) {
        emit progressChanged(displayTime, m_songDuration);
    }

    bool statsUpdated = false; // 标记本帧是否有状态改变

    // 遍历所有 Note 进行状态检查
    for (auto &note : m_notes) {
        if (note.isMissed) continue;

        // 1. 检查头部 Miss
        if (!note.isHit) {
            if (currentTime > note.time + m_config.judgeWindow.miss) {
                note.isMissed = true;
                m_combo = 0;
                m_countMiss++;
                m_totalHits++;
                m_lastJudgmentText = "MISS";
                m_lastJudgmentColor = Qt::red;
                m_feedbackTimer = 20;
                calculateScore(0);

                statsUpdated = true; // === 标记需要更新 UI ===
            }
        }
        // 2. 检查长条 Over-hold
        else if (note.isHold && note.isHolding) {
            if (currentTime > note.endTime + m_config.judgeWindow.miss) {
                note.isHolding = false;
                note.isMissed = true;
                m_combo = 0;
                m_countMiss++;
                m_totalHits++;
                m_lastJudgmentText = "MISS (Overhold)";
                m_lastJudgmentColor = Qt::red;
                m_feedbackTimer = 20;
                calculateScore(0);

                statsUpdated = true; // === 标记需要更新 UI ===
            }
        }
    }

    // === 修复 1: 如果检测到 Miss，立即更新左侧面板 ===
    if (statsUpdated) {
        double acc = (m_totalHits == 0) ? 100.0 : (m_totalAccWeight / m_totalHits) * 100.0;
        // 修改：带上 m_maxCombo
        emit statsChanged(m_countPerfect, m_countGreat, m_countGood, m_countMiss, m_combo, m_maxCombo, m_score, acc);
    }

    update();
}

// 核心：键盘按下逻辑
void GameWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        event->accept(); // 吞掉事件
        return;
    }

    if (event->isAutoRepeat()) return;

    int colTriggered = -1;
    for (int i = 0; i < 4; ++i) {
        if (event->key() == m_config.keyMapping[i]) {
            m_keysPressed[i] = true;
            colTriggered = i;
            break;
        }
    }

    if (colTriggered != -1 && m_isPlaying) {
        checkHit(colTriggered);
    }
    update();
}

void GameWidget::keyReleaseEvent(QKeyEvent *event) {
    if (event->isAutoRepeat()) return;

    int colTriggered = -1;
    for (int i = 0; i < 4; ++i) {
        if (event->key() == m_config.keyMapping[i]) {
            m_keysPressed[i] = false;
            colTriggered = i;
            break;
        }
    }

    // === 新增：松手判定 ===
    if (colTriggered != -1 && m_isPlaying) {
        checkRelease(colTriggered); // 检测长条尾部
    }
    update();
}

void GameWidget::checkHit(int col) {
    qint64 currentTime = getSmoothTime();

    Note* target = nullptr;
    int minDiff = 10000;

    for (auto &note : m_notes) {
        // 找最近的、没打过的、没 Miss 的
        if (note.column == col && !note.isHit && !note.isMissed) {
            int diff = std::abs(note.time - (int)currentTime);
            if (diff <= m_config.judgeWindow.miss) {
                if (diff < minDiff) {
                    minDiff = diff;
                    target = &note;
                }
            }
        }
    }

    if (target) {
        target->isHit = true; // 头部被击中

        if (target->isHold) {
            target->isHolding = true; //如果是长条，标记为“正在按住”
        }

        m_combo++;

        if (m_combo > m_maxCombo) m_maxCombo = m_combo;
        m_totalHits++;

        int weight = 0;
        if (minDiff <= m_config.judgeWindow.perfect) {
            weight = 300;
            m_countPerfect++;
            m_lastJudgmentText = "PERFECT";
            m_lastJudgmentColor = QColor(0, 255, 255);
            m_totalAccWeight += 1.0;
        } else if (minDiff <= m_config.judgeWindow.great) {
            weight = 200;
            m_countGreat++;
            m_lastJudgmentText = "GREAT";
            m_lastJudgmentColor = Qt::green;
            m_totalAccWeight += 0.8;
        } else if (minDiff <= m_config.judgeWindow.good) {
            weight = 50;
            m_countGood++;
            m_lastJudgmentText = "GOOD";
            m_lastJudgmentColor = Qt::blue;
            m_totalAccWeight += 0.5;
        } else {
            weight = 0;
            m_combo = 0;
            m_countMiss++; // Bad 视为断连但给了0分
            m_lastJudgmentText = "BAD";
            m_lastJudgmentColor = Qt::darkRed;
        }
        calculateScore(weight);
        m_feedbackTimer = 30;
    }

    double acc = (m_totalHits == 0) ? 100.0 : (m_totalAccWeight / m_totalHits) * 100.0;
    emit statsChanged(m_countPerfect, m_countGreat, m_countGood, m_countMiss, m_combo, m_maxCombo, m_score, acc);
}

void GameWidget::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::black);

    int w = width();
    int h = height();
    double colWidth = w / 4.0;
    double judgmentY = h * 0.85;

    // ==========================================
    // 1. 始终绘制轨道和判定线 (作为背景)
    // ==========================================
    for (int i = 0; i < 4; ++i) {
        double x = i * colWidth;
        // 按键高亮
        if (m_keysPressed[i]) {
            p.fillRect(QRectF(x, 0, colWidth, h), QColor(255, 255, 255, 40));
            p.fillRect(QRectF(x, judgmentY, colWidth, h - judgmentY), QColor(255, 255, 255, 180));
        }
        // 轨道线
        p.setPen(QColor(60, 60, 60));
        p.drawLine(x, 0, x, h);
    }
    // 判定线
    p.setPen(QPen(Qt::red, 2));
    p.drawLine(0, judgmentY, w, judgmentY);

    // ==========================================
    // 2. 待机状态判断 (没播放 且 没在倒计时)
    // ==========================================
    if (!m_isPlaying && !m_preGameCountingDown) {
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, "Load .osu to play");
        return; // 只有完全空闲时才不画 Note
    }

    // ==========================================
    // 3. 计算时间 (核心)
    // ==========================================
    // 如果是倒计时期间，getSmoothTime() 会返回负数 (例如 -2000 到 0)
    // 这样 Note 就会根据计算绘制在屏幕上方，并随着时间推移自然下落
    qint64 smoothTime = getSmoothTime();

    // ==========================================
    // 4. 绘制 Note (即使在倒计时期间也绘制)
    // ==========================================
    int noteHeight = 30;
    QColor colors[4] = {QColor(240,240,240), QColor(255,215,0), QColor(240,240,240), QColor(255,215,0)};

    for (const auto &note : m_notes) {
        if (note.isMissed) continue;
        if (note.isHit && !note.isHold) continue;
        if (note.isHit && note.isHold && !note.isHolding) continue;

        double x = note.column * colWidth;

        // 计算头部 Y 坐标
        // 在倒计时期间，smoothTime 是负数。
        // 假设 diff = note.time - (-2000) = note.time + 2000 (很大)
        // y = judgmentY - (diff * speed) (很小，甚至负无穷，即在屏幕上方)
        // 随着 smoothTime 趋向 0，y 会慢慢变大，产生“下坠”效果。

        if (note.isHold) {
            double diffEnd = note.endTime - smoothTime;
            double yTail = judgmentY - (diffEnd * m_config.scrollSpeed);
            double yHead;

            if (note.isHolding) {
                yHead = judgmentY;
            } else {
                double diffHead = note.time - smoothTime;
                yHead = judgmentY - (diffHead * m_config.scrollSpeed);
            }

            // 宽松的视口优化：只要有一部分在屏幕附近就画
            // yHead < -1000 表示还在屏幕上方很远的地方，暂时不画，节省性能
            if (yTail > h || yHead < -2000) continue;

            double bodyH = yHead - yTail;
            if (bodyH > 0) {
                QColor bodyColor = colors[note.column];
                bodyColor.setAlpha(180);
                p.setBrush(bodyColor);
                p.setPen(Qt::NoPen);
                p.drawRect(QRectF(x + 10, yTail, colWidth - 20, bodyH));
            }

            if (!note.isHolding) {
                p.setBrush(colors[note.column]);
                p.drawRect(QRectF(x + 2, yHead - noteHeight, colWidth - 4, noteHeight));
            }
            p.setBrush(colors[note.column]);
            p.drawRect(QRectF(x + 2, yTail, colWidth - 4, 5));

        } else {
            // 普通 Note
            double diff = note.time - smoothTime;
            double y = judgmentY - (diff * m_config.scrollSpeed);

            // 视口优化
            if (y > h + 50 || y < -2000) continue;

            p.setBrush(colors[note.column]);
            p.setPen(Qt::NoPen);
            p.drawRect(QRectF(x + 2, y - noteHeight, colWidth - 4, noteHeight));
        }
    }

    // ==========================================
    // 5. 绘制 HUD (分数、Combo、评级)
    // ==========================================
    // 仅在 HUD 区域绘制，避免遮挡倒计时太严重
    QFont fontScore = p.font();
    fontScore.setFamily("Arial");
    fontScore.setPointSize(28);
    fontScore.setBold(true);
    p.setFont(fontScore);
    p.setPen(Qt::white);
    QString scoreText = QString("%1").arg(m_score, 7, 10, QChar('0'));
    p.drawText(QRect(0, 10, w, 50), Qt::AlignCenter, scoreText);

    QFont fontGrade = fontScore;
    fontGrade.setPointSize(40);
    fontGrade.setItalic(true);
    p.setFont(fontGrade);
    QString grade = getGrade();
    QColor gradeColor = Qt::gray;
    if (grade == "S") gradeColor = QColor(255, 215, 0);
    else if (grade == "A") gradeColor = Qt::green;
    else if (grade == "B") gradeColor = Qt::cyan;
    p.setPen(gradeColor);
    p.drawText(QRect(0, 60, w, 60), Qt::AlignCenter, grade);

    if (m_combo > 0) {
        QFont fontCombo = p.font();
        fontCombo.setPointSize(40);
        fontCombo.setBold(true);
        p.setFont(fontCombo);
        p.setPen(QColor(255, 255, 255, 60));
        p.drawText(rect(), Qt::AlignCenter, QString::number(m_combo));
    }

    // ==========================================
    // 6. 绘制倒计时 (画在最顶层)
    // ==========================================
    if (m_preGameCountingDown) {
        qint64 timeLeft = m_config.preGameDelay - (m_visualTimer.elapsed() - m_preGameStartTime);
        int secondsLeft = (timeLeft / 1000) + 1;

        QFont countdownFont = p.font();
        countdownFont.setFamily("Arial");
        countdownFont.setPointSize(80);
        countdownFont.setBold(true);
        p.setFont(countdownFont);

        // 绘制带描边的文字，更清晰
        QPainterPath path;
        path.addText(w/2 - 40, h/2 + 40, countdownFont, QString::number(secondsLeft));

        p.setBrush(QColor(255, 255, 0)); // 黄色填充
        p.setPen(QPen(Qt::black, 3));    // 黑色描边
        p.drawPath(path);
    }

    // 绘制判定结果文字
    else if (m_feedbackTimer > 0) {
        m_feedbackTimer--;
        QFont font = p.font();
        font.setPointSize(24); font.setBold(true); p.setFont(font);
        p.setPen(m_lastJudgmentColor);
        int textY = judgmentY - 100 - (30 - m_feedbackTimer);
        p.drawText(QRect(0, textY, w, 50), Qt::AlignCenter, m_lastJudgmentText);
    }
}

void GameWidget::loadSettings() {
    // QSettings 会自动在注册表(Win)或.ini(Mac/Linux)中读写
    QSettings settings("MugDiffusion", "OsuQuickReader");

    m_config.scrollSpeed = settings.value("scrollSpeed", 0.9).toDouble();
    m_config.gameWidth = settings.value("gameWidth", 500).toInt();

    m_config.songFolder = settings.value("songFolder", "").toString();
    m_config.audioOffset = settings.value("audioOffset", 0).toInt();

    m_config.judgeWindow.perfect = settings.value("judge_perfect", 40).toInt();
    m_config.judgeWindow.great = settings.value("judge_great", 80).toInt();
    m_config.judgeWindow.good = settings.value("judge_good", 120).toInt();
    m_config.judgeWindow.miss = settings.value("judge_miss", 150).toInt();

    m_config.keyMapping[0] = settings.value("key1", (int)Qt::Key_D).toInt();
    m_config.keyMapping[1] = settings.value("key2", (int)Qt::Key_F).toInt();
    m_config.keyMapping[2] = settings.value("key3", (int)Qt::Key_J).toInt();
    m_config.keyMapping[3] = settings.value("key4", (int)Qt::Key_K).toInt();
}

void GameWidget::saveSettings() {
    QSettings settings("MugDiffusion", "OsuQuickReader");

    settings.setValue("scrollSpeed", m_config.scrollSpeed);
    settings.setValue("gameWidth", m_config.gameWidth);

    settings.setValue("songFolder", m_config.songFolder);
    settings.setValue("audioOffset", m_config.audioOffset);

    settings.setValue("judge_perfect", m_config.judgeWindow.perfect);
    settings.setValue("judge_great", m_config.judgeWindow.great);
    settings.setValue("judge_good", m_config.judgeWindow.good);
    settings.setValue("judge_miss", m_config.judgeWindow.miss);

    settings.setValue("key1", m_config.keyMapping[0]);
    settings.setValue("key2", m_config.keyMapping[1]);
    settings.setValue("key3", m_config.keyMapping[2]);
    settings.setValue("key4", m_config.keyMapping[3]);
}

void GameWidget::checkRelease(int col) {
    qint64 currentTime = getSmoothTime();

    // 寻找该列正在被按住的长条
    for (auto &note : m_notes) {
        if (note.column == col && note.isHold && note.isHolding && !note.isMissed) {

            // 计算松手时间与结束时间的差值
            int diff = std::abs(note.endTime - (int)currentTime);

            // === 1. 松手太早 (Early Release) ===
            // 如果还没进入 Miss 窗口就松手了
            if ((int)currentTime < note.endTime - m_config.judgeWindow.miss) {
                note.isHolding = false;
                note.isMissed = true; // 标记为 Miss

                m_combo = 0;
                m_countMiss++; // 增加 Miss 计数
                m_totalHits++;
                m_lastJudgmentText = "MISS (Early)";
                m_lastJudgmentColor = Qt::red;
                m_feedbackTimer = 20;

                // === 修复：必须在这里更新 UI 和分数状态 ===
                calculateScore(0); // 触发一次状态更新

                // 立即告诉 UI 更新，否则玩家会觉得没反应
                double acc = (m_totalHits == 0) ? 100.0 : (m_totalAccWeight / m_totalHits) * 100.0;
                emit statsChanged(m_countPerfect, m_countGreat, m_countGood, m_countMiss, m_combo, m_maxCombo, m_score, acc);

                return;
            }

            // === 2. 正常松手 (Hit) ===
            note.isHolding = false; // 结束按住
            // 只要没被上面那个 if 拦截，说明松手时间是在允许范围内的（包括稍微晚一点）

            m_combo++;
            if (m_combo > m_maxCombo) m_maxCombo = m_combo;
            m_totalHits++;

            int weight = 0;
            if (diff <= m_config.judgeWindow.perfect) {
                weight = 300;
                m_lastJudgmentText = "PERFECT";
                m_lastJudgmentColor = QColor(0, 255, 255);
                m_countPerfect++;       // 别忘了加计数
                m_totalAccWeight += 1.0;
            } else if (diff <= m_config.judgeWindow.good) {
                // 只要在 Good 范围内都给 Great 的反馈，让长条手感更宽松
                // 或者严格按照区间分级
                weight = 200;
                m_lastJudgmentText = "GREAT";
                m_lastJudgmentColor = Qt::green;
                m_countGreat++;
                m_totalAccWeight += 0.8;
            } else {
                // 勉强在 Miss 窗口边缘松手
                weight = 50;
                m_lastJudgmentText = "GOOD";
                m_lastJudgmentColor = Qt::blue;
                m_countGood++;
                m_totalAccWeight += 0.5;
            }

            calculateScore(weight);
            m_feedbackTimer = 20;

            double acc = (m_totalHits == 0) ? 100.0 : (m_totalAccWeight / m_totalHits) * 100.0;
            emit statsChanged(m_countPerfect, m_countGreat, m_countGood, m_countMiss, m_combo, m_maxCombo, m_score, acc);
            return;
        }
    }
}

void GameWidget::calculateScore(int weight) {
    m_currentRawScore += weight;

    // 归一化到 1,000,000
    // 实时分数 = (当前获得权重 / 理论总权重) * 1,000,000
    m_score = (int)((m_currentRawScore / m_maxPossibleScore) * 1000000.0);
}

QString GameWidget::getGrade() const {
    if (m_score >= 970000) return "S";
    if (m_score >= 900000) return "A";
    if (m_score >= 800000) return "B";
    return "C";
}

bool GameWidget::focusNextPrevChild(bool next) {
    // 返回 false 表示：我不处理焦点切换，请把按键事件交给我自己处理
    // 这样 Tab 键就会进入 keyPressEvent，而不会跳到其他按钮上
    return false;
}

void GameWidget::saveRecord() {
    // 1. 构建记录对象
    QJsonObject recordObj;
    // 唯一标识：Artist + Title + Version
    QString mapHash = m_currentArtist + m_currentTitle + m_currentVersion;
    recordObj["hash"] = mapHash;
    recordObj["score"] = m_score;
    recordObj["acc"] = (m_totalHits == 0) ? 0.0 : (m_totalAccWeight / m_totalHits) * 100.0;
    recordObj["combo"] = m_maxCombo;
    recordObj["grade"] = getGrade();
    recordObj["perfect"] = m_countPerfect;
    recordObj["great"] = m_countGreat;
    recordObj["good"] = m_countGood;
    recordObj["miss"] = m_countMiss;
    recordObj["date"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // 记录当时用的判定区间
    QJsonObject judgeObj;
    judgeObj["perfect"] = m_config.judgeWindow.perfect;
    judgeObj["miss"] = m_config.judgeWindow.miss;
    recordObj["judgment"] = judgeObj;

    // 2. 确定保存路径: ./records/
    QString dirPath = QCoreApplication::applicationDirPath() + "/records";
    QDir dir(dirPath);
    if (!dir.exists()) {
        bool ok = dir.mkpath(".");
        if (!ok) {
            qDebug() << "ERROR: Failed to create records directory at:" << dirPath;
            return;
        }
    }

    // 文件名使用 Hash 或者时间戳，这里用 追加模式存到一个大文件 或者 单文件
    // 为了方便读取历史，我们将所有记录存为一个 records.json 列表，或者每个谱面一个文件
    // 这里采用：每个谱面一个 .json 文件，文件名是 hash 的 md5 (为了避开文件名非法字符)

    QString safeName = QString(QCryptographicHash::hash(mapHash.toUtf8(), QCryptographicHash::Md5).toHex());
    QString filePath = dirPath + "/" + safeName + ".json";

    // 读取旧记录 (如果是列表)
    QJsonArray history;
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        history = doc.array();
        file.close();
    }

    // 添加新记录
    history.append(recordObj);

    // 排序？取最高分？这里保留所有历史

    // 写入
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(history);
        file.write(doc.toJson());
        file.close();

        // === 打印成功信息，方便你在 Qt Creator 的 Application Output 里看到 ===
        qDebug() << "========================================";
        qDebug() << "Record SAVED Successfully!";
        qDebug() << "Path:" << filePath;
        qDebug() << "Score:" << m_score;
        qDebug() << "========================================";
    } else {
        qDebug() << "ERROR: Could not open file for writing:" << filePath;
    }
}
