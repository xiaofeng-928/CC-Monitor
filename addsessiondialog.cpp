#include "addsessiondialog.h"
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

AddSessionDialog::AddSessionDialog(SessionMonitor *monitor, QWidget *parent)
    : QDialog(parent)
    , m_monitor(monitor)
    , m_list(new QListWidget(this))
{
    setWindowTitle("Add Monitor");
    setFixedSize(360, 400);
    setStyleSheet("background: #1e1e2e; color: #cdd6f4;");

    auto *title = new QLabel("Available Claude Code Sessions");
    title->setStyleSheet("font-size: 13px; font-weight: bold; padding: 8px;");

    m_list->setStyleSheet(
        "QListWidget { border: 1px solid #313244; border-radius: 4px; background: #11111b; }"
        "QListWidget::item { padding: 8px; border-bottom: 1px solid #313244; }"
        "QListWidget::item:selected { background: #313244; }"
    );

    auto *addBtn = new QPushButton("Add Selected");
    addBtn->setStyleSheet(
        "QPushButton { background: #89b4fa; color: #1e1e2e; border: none; "
        "border-radius: 4px; padding: 8px 24px; font-weight: bold; }"
        "QPushButton:hover { background: #74c7ec; }"
    );

    auto *cancelBtn = new QPushButton("Cancel");
    cancelBtn->setStyleSheet(
        "QPushButton { background: #313244; color: #cdd6f4; border: none; "
        "border-radius: 4px; padding: 8px 24px; }"
        "QPushButton:hover { background: #45475a; }"
    );

    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(addBtn);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addWidget(title);
    layout->addWidget(m_list, 1);
    layout->addLayout(btnLayout);

    connect(addBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    refreshList();
}

void AddSessionDialog::refreshList()
{
    m_list->clear();
    QList<CCSession> available = m_monitor->scanAvailable();

    for (const auto &s : available) {
        // skip already monitored
        if (m_monitor->isMonitoring(s.sessionId)) continue;

        QString text = QString("%1  ·  PID:%2  ·  %3")
            .arg(s.label)
            .arg(s.pid)
            .arg(s.entrypoint);

        auto *item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, s.sessionId);
        item->setCheckState(Qt::Unchecked);
    }

    if (m_list->count() == 0) {
        auto *item = new QListWidgetItem("No active Claude Code sessions found", m_list);
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
    }
}

QStringList AddSessionDialog::selectedSessionIds() const
{
    QStringList ids;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        if (item->checkState() == Qt::Checked)
            ids.append(item->data(Qt::UserRole).toString());
    }
    return ids;
}
