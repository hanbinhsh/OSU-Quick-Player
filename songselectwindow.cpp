#include "SongSelectWindow.h"
#include "ui_SongSelectWindow.h"
#include <QFileDialog>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QMessageBox>
#include <QDateTime>
#include <algorithm>

SongSelectWindow::SongSelectWindow(GameConfig &config, QWidget *parent)
    : QDialog(parent), ui(new Ui::SongSelectWindow), m_config(config) {
    ui->setupUi(this);

    // 设置表格列头 (详细信息)
    QStringList headers;
    headers << "Rank" << "Score" << "Acc" << "Combo" << "Perf" << "Gr" << "Go" << "Miss" << "Date";
    ui->tableHistory->setColumnCount(headers.size());
    ui->tableHistory->setHorizontalHeaderLabels(headers);
    // 调整列宽
    ui->tableHistory->setColumnWidth(0, 40); // Rank
    ui->tableHistory->setColumnWidth(1, 70); // Score
    ui->tableHistory->setColumnWidth(2, 60); // Acc
    ui->tableHistory->setColumnWidth(8, 110); // Date

    // 连接信号
    connect(ui->btnScan, &QPushButton::clicked, this, &SongSelectWindow::onScanClicked);
    connect(ui->listFolders, &QListWidget::itemClicked, this, &SongSelectWindow::onFolderClicked);
    connect(ui->listSongs, &QListWidget::itemClicked, this, &SongSelectWindow::onSongClicked);
    connect(ui->btnPlay, &QPushButton::clicked, this, &SongSelectWindow::onPlayClicked);

    // 自动扫描
    if (!m_config.songFolder.isEmpty()) {
        scanSongs(m_config.songFolder);
    }
}

SongSelectWindow::~SongSelectWindow() {
    delete ui;
}

void SongSelectWindow::onScanClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Song Folder", m_config.songFolder);
    if (!dir.isEmpty()) {
        m_config.songFolder = dir;
        scanSongs(dir);
    }
}

void SongSelectWindow::scanSongs(const QString &folder) {
    ui->listFolders->clear();
    ui->listSongs->clear();
    ui->tableHistory->setRowCount(0);
    m_folderMap.clear();
    m_selectedMap = nullptr;

    QDirIterator it(folder, QStringList() << "*.osu", QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo fileInfo(path);

        // 解析 .osu 文件
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            QTextStream in(&f);
            BeatmapInfo info;
            info.filePath = path;

            int lines = 0;
            while (!in.atEnd() && lines < 50) {
                QString line = in.readLine().trimmed();
                if (line.startsWith("Title:")) info.title = line.mid(6).trimmed();
                else if (line.startsWith("TitleUnicode:")) info.title = line.mid(13).trimmed();
                else if (line.startsWith("Artist:")) info.artist = line.mid(7).trimmed();
                else if (line.startsWith("ArtistUnicode:")) info.artist = line.mid(14).trimmed();
                else if (line.startsWith("Version:")) info.version = line.mid(8).trimmed();
                lines++;
            }
            f.close();

            if (!info.title.isEmpty()) {
                // 使用父文件夹名称作为分组 Key
                QString folderName = fileInfo.dir().dirName();
                m_folderMap[folderName].append(info);
            }
        }
    }

    // 填充左侧文件夹列表 (Map 自动按 Key 排序)
    for (auto it = m_folderMap.begin(); it != m_folderMap.end(); ++it) {
        // 对文件夹内的歌曲按难度/标题排序
        QList<BeatmapInfo> &maps = it.value();
        std::sort(maps.begin(), maps.end(), [](const BeatmapInfo& a, const BeatmapInfo& b) {
            return a.version < b.version;
        });

        ui->listFolders->addItem(it.key());
    }

    if (m_folderMap.isEmpty()) {
        ui->listFolders->addItem("No songs found.");
    }
}

// 点击文件夹 -> 填充中间的歌曲列表
void SongSelectWindow::onFolderClicked(QListWidgetItem *item) {
    ui->listSongs->clear();
    ui->tableHistory->setRowCount(0);
    m_selectedMap = nullptr;
    ui->lblBestScore->setText("Best: -");

    QString folderName = item->text();
    if (m_folderMap.contains(folderName)) {
        const QList<BeatmapInfo> &maps = m_folderMap[folderName];

        // 遍历该文件夹下的所有谱面
        for (int i = 0; i < maps.size(); ++i) {
            const BeatmapInfo &info = maps[i];
            QString label = QString("[%1] %2").arg(info.version, info.title);

            QListWidgetItem *songItem = new QListWidgetItem(label);
            // 存储该歌曲在 m_folderMap[folderName] 列表中的索引
            songItem->setData(Qt::UserRole, i);
            ui->listSongs->addItem(songItem);
        }
    }
}

// 点击歌曲 -> 填充右侧评分表
void SongSelectWindow::onSongClicked(QListWidgetItem *item) {
    // 找到当前选中的文件夹
    if (!ui->listFolders->currentItem()) return;
    QString folderName = ui->listFolders->currentItem()->text();

    // 获取歌曲索引
    int idx = item->data(Qt::UserRole).toInt();

    if (m_folderMap.contains(folderName) && idx < m_folderMap[folderName].size()) {
        // 获取实际数据的指针
        // 注意：这里取地址，因为 m_selectedMap 是指针
        m_selectedMap = &m_folderMap[folderName][idx];
        loadHistory(m_selectedMap->getHash());
    }
}

void SongSelectWindow::loadHistory(const QString &hash) {
    ui->tableHistory->setRowCount(0);
    ui->lblBestScore->setText("Best: 0");

    QString dirPath = QCoreApplication::applicationDirPath() + "/records";
    QString safeName = QString(QCryptographicHash::hash(hash.toUtf8(), QCryptographicHash::Md5).toHex());
    QString filePath = dirPath + "/" + safeName + ".json";

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonArray jsonHistory = doc.array();
    file.close();

    // 转换为结构体列表以便排序
    QList<RecordData> records;
    for (const auto &val : jsonHistory) {
        RecordData r;
        r.json = val.toObject();
        r.date = QDateTime::fromString(r.json["date"].toString(), Qt::ISODate);
        records.append(r);
    }

    // 按分数降序排序 (最高分在最前)
    std::sort(records.begin(), records.end(), [](const RecordData &a, const RecordData &b) {
        return a.json["score"].toInt() > b.json["score"].toInt();
    });

    if (!records.isEmpty()) {
        int best = records[0].json["score"].toInt();
        ui->lblBestScore->setText(QString("Best: %1").arg(best));
    }

    // 填充表格
    for (const auto &r : records) {
        int row = ui->tableHistory->rowCount();
        ui->tableHistory->insertRow(row);

        QJsonObject obj = r.json;

        // Rank (Grade)
        QTableWidgetItem *itGrade = new QTableWidgetItem(obj["grade"].toString());
        // 简单的颜色区分
        if (obj["grade"].toString() == "S") itGrade->setForeground(Qt::yellow);
        else if (obj["grade"].toString() == "A") itGrade->setForeground(Qt::green);
        ui->tableHistory->setItem(row, 0, itGrade);

        // Score
        ui->tableHistory->setItem(row, 1, new QTableWidgetItem(QString::number(obj["score"].toInt())));

        // Acc
        ui->tableHistory->setItem(row, 2, new QTableWidgetItem(QString::number(obj["acc"].toDouble(), 'f', 2) + "%"));

        // Max Combo
        ui->tableHistory->setItem(row, 3, new QTableWidgetItem(QString::number(obj["combo"].toInt())));

        // Detailed Judgments
        ui->tableHistory->setItem(row, 4, new QTableWidgetItem(QString::number(obj["perfect"].toInt())));
        ui->tableHistory->setItem(row, 5, new QTableWidgetItem(QString::number(obj["great"].toInt())));
        ui->tableHistory->setItem(row, 6, new QTableWidgetItem(QString::number(obj["good"].toInt())));
        ui->tableHistory->setItem(row, 7, new QTableWidgetItem(QString::number(obj["miss"].toInt())));

        // Date
        ui->tableHistory->setItem(row, 8, new QTableWidgetItem(r.date.toString("MM-dd hh:mm")));
    }
}

void SongSelectWindow::onPlayClicked() {
    if (m_selectedMap) {
        accept(); // 返回 Accepted，主窗口会读取 getSelectedBeatmapPath
    } else {
        QMessageBox::warning(this, "Info", "Please select a song first.");
    }
}

QString SongSelectWindow::getSelectedBeatmapPath() const {
    if (m_selectedMap) return m_selectedMap->filePath;
    return "";
}
