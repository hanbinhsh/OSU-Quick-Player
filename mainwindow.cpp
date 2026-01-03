#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "SettingsDialog.h"
#include <QFileDialog>
#include <QVBoxLayout>
#include "SongSelectWindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 1. 创建游戏控件
    m_gameWidget = new GameWidget(this);

    // 2. 将 GameWidget 放入 UI 设计器中预留的 gameContainer
    // 我们的 gameContainer 在 UI 里是一个空的 QWidget
    if (ui->gameContainer->layout() == nullptr) {
        QVBoxLayout* layout = new QVBoxLayout(ui->gameContainer);
        layout->setContentsMargins(0, 0, 0, 0); // 无边距，铺满
        layout->addWidget(m_gameWidget);
    }

    GameConfig savedConfig = m_gameWidget->getConfig();
    // 应用宽度到容器
    ui->gameContainer->setFixedWidth(savedConfig.gameWidth);

    // 3. 连接信号槽
    // 游戏逻辑 -> 界面更新
    connect(m_gameWidget, &GameWidget::statsChanged, this, &MainWindow::updateStats);
    connect(m_gameWidget, &GameWidget::songLoaded, this, &MainWindow::updateSongInfo);
    connect(m_gameWidget, &GameWidget::progressChanged, this, &MainWindow::updateProgress);

    // 按钮 -> 功能
    connect(ui->btnOpen, &QPushButton::clicked, this, &MainWindow::onOpenTriggered);
    connect(ui->btnSettings, &QPushButton::clicked, this, &MainWindow::onSettingsTriggered);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::onOpenTriggered);
    connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::onSettingsTriggered);

    // 设置默认窗口标题
    setWindowTitle("MugDiffusion Player");
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::onOpenTriggered() {
    // 传入当前配置 (包含 songFolder)
    // 注意：getConfig 返回的是拷贝，我们需要传入引用或者在 MainWindow 维护一份 Config
    // 简单起见，我们从 GameWidget 获取 Config，修改完再 Update 回去

    GameConfig config = m_gameWidget->getConfig();
    SongSelectWindow dlg(config, this); // 传入引用

    if (dlg.exec() == QDialog::Accepted) {
        // 如果用户在选歌界面改了文件夹，这里会通过引用更新 config
        m_gameWidget->updateConfig(config);

        QString path = dlg.getSelectedBeatmapPath();
        if (!path.isEmpty()) {
            m_gameWidget->loadBeatmap(path);
            m_gameWidget->setFocus();
        }
    }
}

void MainWindow::onSettingsTriggered() {
    SettingsDialog dlg(m_gameWidget->getConfig(), this);
    if (dlg.exec() == QDialog::Accepted) {
        GameConfig config = dlg.getConfig();
        m_gameWidget->updateConfig(config);
        ui->gameContainer->setFixedWidth(config.gameWidth);
        m_gameWidget->setFocus();
    }
}

void MainWindow::updateStats(int perfect, int great, int good, int miss, int combo, int maxCombo, int score, double acc) {
    // 直接操作 ui 指针里的控件
    ui->lblPerfect->setText(QString::number(perfect));
    ui->lblGreat->setText(QString::number(great));
    ui->lblGood->setText(QString::number(good));
    ui->lblMiss->setText(QString::number(miss));
    ui->lblCombo->setText(QString::number(combo));
    ui->lblScore->setText(QString("%1").arg(score, 7, 10, QChar('0')));
    ui->lblAcc->setText(QString::number(acc, 'f', 2) + "%");
    ui->lblMaxCombo->setText(QString::number(maxCombo));
}

void MainWindow::updateSongInfo(QString title, QString artist, qint64 duration) {
    ui->lblTitle->setText(title);
    ui->lblArtist->setText(artist);
    ui->progressBar->setMaximum(duration);
}

void MainWindow::updateProgress(qint64 current, qint64 total) {
    if (current > total) current = total;

    ui->progressBar->setMaximum(total);
    ui->progressBar->setValue(current);

    // 辅助 lambda：将毫秒转为 mm:ss
    auto formatTime = [](qint64 ms) {
        qint64 s = ms / 1000;
        qint64 m = s / 60;
        s = s % 60;
        return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    };

    ui->lblTime->setText(QString("%1 / %2").arg(formatTime(current)).arg(formatTime(total)));
}
