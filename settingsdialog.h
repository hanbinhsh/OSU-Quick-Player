#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include "Structs.h"

namespace Ui { class SettingsDialog; }

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(GameConfig currentConfig, QWidget *parent = nullptr);
    ~SettingsDialog();
    GameConfig getConfig() const;

private slots:
    void onKeyButtonClicked();

private:
    Ui::SettingsDialog *ui;
    GameConfig m_config;
    void updateButtonLabels();
    bool eventFilter(QObject *watched, QEvent *event) override;
    QPushButton* m_waitingButton = nullptr; // 正在等待输入的按钮
};
#endif
