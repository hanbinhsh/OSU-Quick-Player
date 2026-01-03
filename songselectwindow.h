#ifndef SONGSELECTWINDOW_H
#define SONGSELECTWINDOW_H

#include <QDialog>
#include <QMap>
#include <QList>
#include <QListWidgetItem>
#include <QJsonObject>
#include "Structs.h"

namespace Ui {
class SongSelectWindow;
}

class SongSelectWindow : public QDialog {
    Q_OBJECT

public:
    explicit SongSelectWindow(GameConfig &config, QWidget *parent = nullptr);
    ~SongSelectWindow();

    QString getSelectedBeatmapPath() const;

private slots:
    void onScanClicked();
    void onFolderClicked(QListWidgetItem *item);
    void onSongClicked(QListWidgetItem *item);
    void onPlayClicked();

private:
    void scanSongs(const QString &folder);
    void loadHistory(const QString &hash);

    // 辅助结构：用于表格排序
    struct RecordData {
        QJsonObject json;
        QDateTime date;
    };

    Ui::SongSelectWindow *ui;
    GameConfig &m_config;

    // 数据结构：文件夹名 -> 该文件夹下的歌曲列表
    QMap<QString, QList<BeatmapInfo>> m_folderMap;

    // 当前选中的谱面指针
    const BeatmapInfo *m_selectedMap = nullptr;
};

#endif // SONGSELECTWINDOW_H
