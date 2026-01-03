#include "SettingsDialog.h"
#include "ui_SettingsDialog.h"
#include <QKeyEvent>

SettingsDialog::SettingsDialog(GameConfig currentConfig, QWidget *parent)
    : QDialog(parent), ui(new Ui::SettingsDialog), m_config(currentConfig) {
    ui->setupUi(this);

    // 绑定数据到 UI
    ui->spinSpeed->setValue(m_config.scrollSpeed);
    ui->spinWidth->setValue(m_config.gameWidth);

    ui->spinOffset->setValue(m_config.audioOffset);
    ui->spinPreGameDelay->setValue(m_config.preGameDelay);

    ui->spinJ_Perfect->setValue(m_config.judgeWindow.perfect);
    ui->spinJ_Great->setValue(m_config.judgeWindow.great);
    ui->spinJ_Good->setValue(m_config.judgeWindow.good);
    ui->spinMissWindow->setValue(m_config.judgeWindow.miss);

    // 连接信号
    connect(ui->btnKey1, &QPushButton::clicked, this, &SettingsDialog::onKeyButtonClicked);
    connect(ui->btnKey2, &QPushButton::clicked, this, &SettingsDialog::onKeyButtonClicked);
    connect(ui->btnKey3, &QPushButton::clicked, this, &SettingsDialog::onKeyButtonClicked);
    connect(ui->btnKey4, &QPushButton::clicked, this, &SettingsDialog::onKeyButtonClicked);

    // === 核心修复方案 A ===
    // 我们直接给 Dialog 本身安装过滤器，这样无论焦点在哪个按钮上，Dialog 都能拦截按键
    this->installEventFilter(this);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateButtonLabels();
}

SettingsDialog::~SettingsDialog() { delete ui; }

void SettingsDialog::onKeyButtonClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (btn) {
        // 如果之前有正在等待的按钮，先把它恢复原状
        if (m_waitingButton && m_waitingButton != btn) {
            updateButtonLabels();
        }

        m_waitingButton = btn;
        btn->setText("Press Key...");

        // === 核心修复方案 B ===
        // 不要禁用按钮！禁用会导致焦点跑掉。
        // btn->setEnabled(false);  <-- 删除这行

        // 强制把焦点聚集在这个按钮上，或者取消其他按钮的焦点
        btn->setFocus();
    }
}

bool SettingsDialog::eventFilter(QObject *watched, QEvent *event) {
    // 监听键盘按下事件
    if (event->type() == QEvent::KeyPress && m_waitingButton) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(event);
        int key = ke->key();

        // 找到对应的索引
        int idx = -1;
        if (m_waitingButton == ui->btnKey1) idx = 0;
        else if (m_waitingButton == ui->btnKey2) idx = 1;
        else if (m_waitingButton == ui->btnKey3) idx = 2;
        else if (m_waitingButton == ui->btnKey4) idx = 3;

        if (idx != -1) {
            // 保存键值
            m_config.keyMapping[idx] = key;

            // 状态复位
            m_waitingButton = nullptr;
            updateButtonLabels();

            return true; // 拦截事件，不让它传递给按钮（防止触发点击）
        }
    }
    return QDialog::eventFilter(watched, event);
}

void SettingsDialog::updateButtonLabels() {
    // 将 int 键值转换为可读的字符串 (例如 68 -> "D")
    ui->btnKey1->setText(QKeySequence((Qt::Key)m_config.keyMapping[0]).toString());
    ui->btnKey2->setText(QKeySequence((Qt::Key)m_config.keyMapping[1]).toString());
    ui->btnKey3->setText(QKeySequence((Qt::Key)m_config.keyMapping[2]).toString());
    ui->btnKey4->setText(QKeySequence((Qt::Key)m_config.keyMapping[3]).toString());
}

GameConfig SettingsDialog::getConfig() const {
    GameConfig c = m_config;
    c.scrollSpeed = ui->spinSpeed->value();
    c.gameWidth = ui->spinWidth->value();

    c.audioOffset = ui->spinOffset->value();
    c.preGameDelay = ui->spinPreGameDelay->value();

    c.judgeWindow.perfect = ui->spinJ_Perfect->value();
    c.judgeWindow.great = ui->spinJ_Great->value();
    c.judgeWindow.good = ui->spinJ_Good->value();
    c.judgeWindow.miss = ui->spinMissWindow->value();
    return c;
}
