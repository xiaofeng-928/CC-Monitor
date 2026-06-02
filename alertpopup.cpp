#include "alertpopup.h"
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QPropertyAnimation>

AlertPopup::AlertPopup(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void AlertPopup::setupUi()
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(280);
    setMouseTracking(true);

    // Status dot
    m_dot = new QLabel;
    m_dot->setFixedSize(12, 12);
    m_dot->setStyleSheet("border-radius: 6px;");

    // Text area
    m_labelText = new QLabel;
    m_labelText->setStyleSheet("color: #cdd6f4; font-size: 13px; font-weight: bold;");
    m_labelText->setWordWrap(true);

    m_statusText = new QLabel;
    m_statusText->setStyleSheet("color: #6c7086; font-size: 11px;");
    m_statusText->setWordWrap(true);

    auto *textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(1);
    textLayout->addWidget(m_labelText);
    textLayout->addWidget(m_statusText);

    // Jump button
    m_jumpBtn = new QPushButton("Jump");
    m_jumpBtn->setFixedSize(52, 26);
    m_jumpBtn->setStyleSheet(
        "QPushButton { background: #313244; color: #89b4fa; border: none; "
        "border-radius: 4px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: #45475a; }"
    );
    connect(m_jumpBtn, &QPushButton::clicked, this, [this]() {
        emit jumpToSession(m_pid);
        emit dismissed(this);
    });

    // Main layout
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(12, 10, 8, 10);
    mainLayout->setSpacing(10);
    mainLayout->addWidget(m_dot, 0, Qt::AlignTop);
    mainLayout->addLayout(textLayout, 1);
    mainLayout->addWidget(m_jumpBtn, 0, Qt::AlignVCenter);

    setStyleSheet(
        "AlertPopup { background: #1e1e2e; border: 1px solid #313244; border-radius: 8px; }"
    );
}

void AlertPopup::showAlert(const QString &label, SessionStatus status, qint64 pid,
                            const QPoint &anchor)
{
    m_pid = pid;

    QString color = statusColorHex(status);
    m_dot->setStyleSheet(QString("background: %1; border-radius: 6px;").arg(color));
    m_labelText->setText(label);
    m_statusText->setText(statusText(status));

    // Auto-hide: WaitingApproval stays until user acts
    if (m_autoHideTimer) {
        m_autoHideTimer->stop();
        delete m_autoHideTimer;
        m_autoHideTimer = nullptr;
    }

    if (status != SessionStatus::WaitingApproval && status != SessionStatus::Error) {
        m_autoHideTimer = new QTimer(this);
        m_autoHideTimer->setSingleShot(true);
        connect(m_autoHideTimer, &QTimer::timeout, this, [this]() {
            auto *fade = new QPropertyAnimation(this, "windowOpacity");
            fade->setDuration(300);
            fade->setStartValue(1.0);
            fade->setEndValue(0.0);
            connect(fade, &QPropertyAnimation::finished, this, [this]() {
                emit dismissed(this);
            });
            fade->start(QAbstractAnimation::DeleteWhenStopped);
        });
        m_autoHideTimer->start(5000);
    }

    adjustSize();
    anchorTo(anchor);
    show();
    raise();
}

void AlertPopup::anchorTo(const QPoint &anchor)
{
    adjustSize();
    int pw = width();
    int ph = height();

    // Default: above the anchor, horizontally centered
    int x = anchor.x() - pw / 2;
    int y = anchor.y() - ph - 6;

    // Clamp to screen
    QScreen *screen = QGuiApplication::screenAt(anchor);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect geo = screen->availableGeometry();
        if (x < geo.left()) x = geo.left();
        if (x + pw > geo.right()) x = geo.right() - pw;
        // If not enough space above, put below
        if (y < geo.top())
            y = anchor.y() + 6;
        if (y + ph > geo.bottom())
            y = geo.bottom() - ph;
    }

    move(x, y);
}

qint64 AlertPopup::pid() const
{
    return m_pid;
}

QString AlertPopup::statusColorHex(SessionStatus status) const
{
    return sessionStatusColor(status);
}

QString AlertPopup::statusText(SessionStatus status) const
{
    switch (status) {
    case SessionStatus::WaitingApproval:
        return "Waiting for approval";
    case SessionStatus::Error:
        return "Error occurred";
    case SessionStatus::Stuck:
        return "No response for 60s";
    case SessionStatus::Thinking:
        return "Thinking...";
    case SessionStatus::Idle:
        return "Task completed";
    }
    return {};
}
