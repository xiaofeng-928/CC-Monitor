#include "sessioncard.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>

SessionCard::SessionCard(QWidget *parent)
    : QFrame(parent)
{
    setMinimumHeight(72);

    // Status indicator - left side, dedicated box
    m_statusDot = new QLabel;
    m_statusDot->setFixedSize(48, 48);
    m_statusDot->setStyleSheet("border-radius: 10px;");

    // Right side - info container
    m_labelText = new QLabel;
    m_labelText->setStyleSheet("color: #cdd6f4; font-size: 14px; font-weight: bold;");

    m_pidText = new QLabel;
    m_pidText->setStyleSheet("color: #6c7086; font-size: 12px;");

    m_detailText = new QLabel;
    m_detailText->setStyleSheet("color: #585b70; font-size: 11px;");
    m_detailText->setWordWrap(true);

    m_closeBtn = new QPushButton("x");
    m_closeBtn->setFixedSize(20, 20);
    m_closeBtn->setStyleSheet(
        "QPushButton { color: #585b70; border: none; font-size: 12px; }"
        "QPushButton:hover { color: #e61e1e; }"
    );
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        emit closeRequested(m_sessionId);
    });

    // Info layout (label + pid + close)
    auto *infoHeader = new QHBoxLayout;
    infoHeader->setSpacing(8);
    infoHeader->addWidget(m_labelText, 1);
    infoHeader->addWidget(m_pidText);
    infoHeader->addWidget(m_closeBtn);

    // Right side container
    auto *rightLayout = new QVBoxLayout;
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(2);
    rightLayout->addLayout(infoHeader);
    rightLayout->addWidget(m_detailText);

    // Main layout: status dot on left, info on right
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 8, 8, 8);
    mainLayout->setSpacing(12);
    mainLayout->addWidget(m_statusDot, 0, Qt::AlignTop);
    mainLayout->addLayout(rightLayout, 1);
}

void SessionCard::updateFrom(const CCSession &session)
{
    m_sessionId = session.sessionId;
    m_pid = session.pid;

    m_labelText->setText(session.label.isEmpty() ? session.sessionId.left(8) : session.label);
    m_pidText->setText(QString("PID:%1").arg(session.pid));

    QStringList parts;
    if (!session.model.isEmpty())     parts << session.model;
    if (!session.gitBranch.isEmpty()) parts << session.gitBranch;
    if (!session.activeFiles.isEmpty()) parts << session.activeFiles.last();
    m_detailText->setText(parts.join("  ·  "));

    applyStatusStyle(session.status);
}

void SessionCard::applyStatusStyle(SessionStatus status)
{
    QString dotColor = sessionStatusColor(status);
    QString borderColor = (status == SessionStatus::Thinking || status == SessionStatus::Idle)
        ? "#313244" : dotColor;
    QString bgColor = "#1e1e2e";

    m_statusDot->setStyleSheet(QString("background: %1; border-radius: 8px;").arg(dotColor));
    setStyleSheet(QString(
        "SessionCard { background: %1; border: 1px solid %2; border-radius: 4px; }"
        "SessionCard:hover { background: #1e1f2e; }"
    ).arg(bgColor, borderColor));

    setCursor(Qt::PointingHandCursor);
}

void SessionCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked(m_sessionId);
    QFrame::mousePressEvent(event);
}
