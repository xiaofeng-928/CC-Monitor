#include "widget.h"
#include "sessioncard.h"
#include "addsessiondialog.h"
#include "windowactivator.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCloseEvent>
#include <QMenu>
#include <QScrollArea>
#include <QApplication>
#include <QStyle>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , m_monitor(new SessionMonitor(this))
    , m_tray(new QSystemTrayIcon(this))
{
    setupUi();
    setupTray();

    connect(m_monitor, &SessionMonitor::sessionsUpdated,
            this, &Widget::onSessionsUpdated);
    connect(m_monitor, &SessionMonitor::sessionNeedsAttention,
            this, &Widget::onNeedsAttention);
    connect(m_monitor, &SessionMonitor::sessionStuck,
            this, &Widget::onSessionStuck);

    m_monitor->start();
}

void Widget::setupUi()
{
    setWindowTitle("CC Monitor");
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::WindowMinMaxButtonsHint);
    setMinimumSize(320, 350);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet("background: #11111b;");

    auto *title = new QLabel("CC Monitor");
    title->setStyleSheet("color: #cdd6f4; font-size: 15px; font-weight: bold;");

    auto *addBtn = new QPushButton("+");
    addBtn->setFixedSize(28, 28);
    addBtn->setStyleSheet(
        "QPushButton { background: #313244; color: #89b4fa; border: none; "
        "border-radius: 14px; font-size: 18px; font-weight: bold; }"
        "QPushButton:hover { background: #45475a; }"
    );
    connect(addBtn, &QPushButton::clicked, this, &Widget::addSession);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(12, 10, 10, 6);
    header->addWidget(title, 1);
    header->addWidget(addBtn);

    auto *scrollContent = new QWidget;
    scrollContent->setStyleSheet("background: transparent;");
    m_cardLayout = new QVBoxLayout(scrollContent);
    m_cardLayout->setContentsMargins(8, 0, 8, 0);
    m_cardLayout->setSpacing(6);
    m_cardLayout->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidget(scrollContent);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #45475a; border-radius: 3px; }"
    );

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addLayout(header);
    mainLayout->addWidget(scroll, 1);
}

void Widget::setupTray()
{
    m_tray->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    m_tray->setToolTip("CC Monitor");

    auto *menu = new QMenu(this);
    menu->setStyleSheet(
        "QMenu { background: #1e1e2e; color: #cdd6f4; border: 1px solid #313244; }"
        "QMenu::item:selected { background: #313244; }"
    );
    menu->addAction("Show", this, &Widget::showNormal);
    menu->addSeparator();
    menu->addAction("Quit", qApp, &QApplication::quit);

    m_tray->setContextMenu(menu);
    m_tray->show();

    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick)
            showNormal();
    });
}

void Widget::onSessionsUpdated(const QList<CCSession> &sessions)
{
    QSet<QString> alive;
    for (const auto &s : sessions) {
        alive.insert(s.sessionId);

        SessionCard *card = m_cards.value(s.sessionId);
        if (!card) {
            card = new SessionCard;
            connect(card, &SessionCard::clicked, this, &Widget::onCardClicked);
            m_cardLayout->insertWidget(m_cardLayout->count() - 1, card);
            m_cards[s.sessionId] = card;
        }
        card->updateFrom(s);
    }

    for (auto it = m_cards.begin(); it != m_cards.end(); ) {
        if (!alive.contains(it.key())) {
            it.value()->deleteLater();
            it = m_cards.erase(it);
        } else {
            ++it;
        }
    }
}

void Widget::onNeedsAttention(const CCSession &session)
{
    m_tray->showMessage(
        "Claude Code Needs You",
        session.label + " is waiting for approval",
        QSystemTrayIcon::Warning, 5000
    );
    QApplication::alert(this);
}

void Widget::onSessionStuck(const CCSession &session)
{
    m_tray->showMessage(
        "Session Stuck",
        session.label + " — no response for 60s",
        QSystemTrayIcon::Warning, 5000
    );
}

void Widget::onCardClicked(const QString &sessionId)
{
    auto it = m_cards.find(sessionId);
    if (it != m_cards.end())
        activateProcessWindow(it.value()->pid());
}

void Widget::addSession()
{
    AddSessionDialog dlg(m_monitor, this);
    if (dlg.exec() == QDialog::Accepted) {
        for (const auto &sid : dlg.selectedSessionIds())
            m_monitor->startMonitoring(sid);
    }
}

void Widget::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}
