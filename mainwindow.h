#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "GameWidget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onOpenTriggered();
    void onSettingsTriggered();

    // 槽函数：接收 GameWidget 发来的数据更新界面
    void updateStats(int perfect, int great, int good, int miss, int combo, int maxCombo, int score, double acc);
    void updateSongInfo(QString title, QString artist, qint64 duration);
    void updateProgress(qint64 current, qint64 total);

private:
    Ui::MainWindow *ui;
    GameWidget *m_gameWidget; // 我们将在代码中把这个塞进 ui->gameContainer
};
#endif // MAINWINDOW_H
