#include "GameWidget.h"
#include <QPainter>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QKeyEvent>
#include <cmath>
#include <QSettings>

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
    if (!m_isPlaying) return 0;
    // 返回：从开始播放到现在经过的毫秒数
    // QElapsedTimer 的精度是纳秒级的，非常平滑
    return m_visualTimer.elapsed();
}

void GameWidget::updateConfig(const GameConfig &config) {
    m_config = config;
    saveSettings();
}

void GameWidget::resetGame() {
    m_player->stop();
    m_isPlaying = false;

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
        int lastNoteTime = m_notes.empty() ? 0 : m_notes.back().endTime;
        m_songDuration = lastNoteTime + 3000; // 多给3秒

        // 连接 duration 信号以获取准确时长
        connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur){
            m_songDuration = dur;
            // 再次更新一下 UI 信息
            emit songLoaded(m_currentTitle, m_currentArtist, m_songDuration);
        });

        m_player->play();
        m_visualTimer.restart();
        m_isPlaying = true;

        // === 发射信号：通知主窗口歌曲加载完毕 ===
        emit songLoaded(m_currentTitle, m_currentArtist, m_songDuration);
    }
}

void GameWidget::gameLoop() {
    if (!m_isPlaying) {
        update();
        return;
    }

    qint64 audioTime = m_player->position();
    qint64 currentTime = getSmoothTime();

    // === 修复 2: 进度条数字限制 ===
    // 如果当前时间超过总时长，强制显示为总时长
    qint64 displayTime = currentTime;
    if (m_songDuration > 0 && displayTime > m_songDuration) {
        displayTime = m_songDuration;

        // 可选：如果音乐真的停了，也可以在这里设置 m_isPlaying = false;
        if (m_player->playbackState() == QMediaPlayer::StoppedState) {
            m_isPlaying = false;
        }
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
    p.setRenderHint(QPainter::Antialiasing); // 抗锯齿
    p.fillRect(rect(), Qt::black);

    int w = width();
    int h = height();
    double colWidth = w / 4.0;
    double judgmentY = h * 0.85;

    if (!m_isPlaying) {
        // 没播放时显示提示
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, "Load .osu to play");
        return;
    }

    // 绘制轨道 (代码不变)
    for (int i = 0; i < 4; ++i) {
        double x = i * colWidth;
        if (m_keysPressed[i]) {
            p.fillRect(QRectF(x, 0, colWidth, h), QColor(255, 255, 255, 40));
            p.fillRect(QRectF(x, judgmentY, colWidth, h - judgmentY), QColor(255, 255, 255, 180));
        }
        p.setPen(QColor(60, 60, 60));
        p.drawLine(x, 0, x, h);
    }
    p.setPen(QPen(Qt::red, 2));
    p.drawLine(0, judgmentY, w, judgmentY);

    if (!m_isPlaying) return;

    // === 核心修改：使用平滑时间进行渲染 ===
    qint64 smoothTime = getSmoothTime();

    int noteHeight = 30;
    QColor colors[4] = {QColor(240,240,240), QColor(255,215,0), QColor(240,240,240), QColor(255,215,0)};

    for (const auto &note : m_notes) {
        // 1. 如果完全结束了 (Missed 或者 正常结束且没在按)，就不画
        if (note.isMissed) continue;
        if (note.isHit && !note.isHold) continue; // 普通Note打完消失
        if (note.isHit && note.isHold && !note.isHolding) continue; // 长条打完(正常松手)消失

        double x = note.column * colWidth;

        if (note.isHold) {
            // === 长条绘制逻辑 ===
            double diffEnd = note.endTime - smoothTime;
            double yTail = judgmentY - (diffEnd * m_config.scrollSpeed);

            double yHead;

            // 关键：如果正在 Holding，头部锁定在判定线上
            if (note.isHolding) {
                yHead = judgmentY;
            } else {
                // 还没打，或者Miss了，头部正常下落
                double diffHead = note.time - smoothTime;
                yHead = judgmentY - (diffHead * m_config.scrollSpeed);
            }

            // 视口优化
            if (yTail > h || yHead < -500) continue;

            // 绘制长条身体
            double bodyH = yHead - yTail;
            if (bodyH > 0) {
                QColor bodyColor = colors[note.column];
                bodyColor.setAlpha(180);
                p.setBrush(bodyColor);
                p.setPen(Qt::NoPen);
                // 长条本体
                p.drawRect(QRectF(x + 10, yTail, colWidth - 20, bodyH));
            }

            // 绘制头部 (只有还没打的时候才画头，正在按住时头已经被“吃”了，不需要画)
            if (!note.isHolding) {
                p.setBrush(colors[note.column]);
                p.drawRect(QRectF(x + 2, yHead - noteHeight, colWidth - 4, noteHeight));
            }

            // 绘制尾部 (横杠)
            p.setBrush(colors[note.column]);
            p.drawRect(QRectF(x + 2, yTail, colWidth - 4, 5));

        } else {
            // === 普通 Note 绘制 ===
            double diff = note.time - smoothTime;
            double y = judgmentY - (diff * m_config.scrollSpeed);
            if (y > h + 50 || y < -100) continue;
            p.setBrush(colors[note.column]);
            p.setPen(Qt::NoPen);
            p.drawRect(QRectF(x + 2, y - noteHeight, colWidth - 4, noteHeight));
        }
    }

    // 绘制 HUD
    // 1. 中间显示分数 (Score)
    QFont fontScore = p.font();
    fontScore.setFamily("Arial");
    fontScore.setPointSize(28);
    fontScore.setBold(true);
    p.setFont(fontScore);
    p.setPen(Qt::white);

    QString scoreText = QString("%1").arg(m_score, 7, 10, QChar('0'));

    // 绘制在顶部中央 (y=50)
    QRect scoreRect(0, 10, w, 50);
    p.drawText(scoreRect, Qt::AlignCenter, scoreText);

    // 2. 绘制评级 (Grade) 在分数下面
    QFont fontGrade = fontScore;
    fontGrade.setPointSize(40);
    fontGrade.setItalic(true);
    p.setFont(fontGrade);

    QString grade = getGrade();
    QColor gradeColor = Qt::gray;
    if (grade == "S") gradeColor = QColor(255, 215, 0); // 金色
    else if (grade == "A") gradeColor = Qt::green;
    else if (grade == "B") gradeColor = Qt::cyan;

    p.setPen(gradeColor);
    // 绘制在分数正下方
    p.drawText(QRect(0, 60, w, 60), Qt::AlignCenter, grade);

    // 3. 绘制 Combo (在屏幕中心)
    if (m_combo > 0) {
        QFont fontCombo = p.font();
        fontCombo.setPointSize(40);
        fontCombo.setBold(true);
        p.setFont(fontCombo);
        p.setPen(QColor(255, 255, 255, 60)); // 更淡一点，防止遮挡
        p.drawText(rect(), Qt::AlignCenter, QString::number(m_combo));
    }

    QFont font = p.font();
    if (m_feedbackTimer > 0) {
        m_feedbackTimer--; // 这里其实应该用时间差来减，不过60fps衰减也行
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
    m_config.judgeWindow.miss = settings.value("missWindow", 150).toInt();
    m_config.gameWidth = settings.value("gameWidth", 500).toInt();

    m_config.keyMapping[0] = settings.value("key1", (int)Qt::Key_D).toInt();
    m_config.keyMapping[1] = settings.value("key2", (int)Qt::Key_F).toInt();
    m_config.keyMapping[2] = settings.value("key3", (int)Qt::Key_J).toInt();
    m_config.keyMapping[3] = settings.value("key4", (int)Qt::Key_K).toInt();
}

void GameWidget::saveSettings() {
    QSettings settings("MugDiffusion", "OsuQuickReader");

    settings.setValue("scrollSpeed", m_config.scrollSpeed);
    settings.setValue("missWindow", m_config.judgeWindow.miss);
    settings.setValue("gameWidth", m_config.gameWidth);
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
                m_lastJudgmentText = "BAD";
                m_lastJudgmentColor = Qt::darkRed;
                m_countGood++; // 算作 Good 或 Bad
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
