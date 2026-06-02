#include "widget.h"
#include "sessioncard.h"
#include "addsessiondialog.h"
#include "windowactivator.h"
#include "floatingball.h"
#include "soundmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCloseEvent>
#include <QApplication>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QScreen>
#include <algorithm>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , m_monitor(new SessionMonitor(this))
    , m_ball(new FloatingBall)
    , m_soundMgr(new SoundManager(this))
{
    setupUi();

    connect(m_monitor, &SessionMonitor::sessionsUpdated,
            this, &Widget::onSessionsUpdated);
    connect(m_monitor, &SessionMonitor::sessionNeedsAttention,
            this, &Widget::onNeedsAttention);
    connect(m_monitor, &SessionMonitor::sessionStuck,
            this, &Widget::onSessionStuck);
    connect(m_ball, &FloatingBall::toggleMainWindow,
            this, &Widget::onToggleWindow);
    connect(m_ball, &FloatingBall::jumpToSession,
            this, &Widget::onJumpToSession);
    connect(m_ball, &FloatingBall::ballMoved,
            this, &Widget::repositionNearBall);

    m_monitor->start();
    m_ball->show();

    // Initial position next to the floating ball
    QPoint ballPos = m_ball->pos();
    int ballW = m_ball->width();
    int x = ballPos.x() + ballW + 8;
    int y = ballPos.y();
    QScreen *screen = QGuiApplication::screenAt(ballPos + QPoint(ballW / 2, 0));
    if (screen) {
        QRect geo = screen->availableGeometry();
        if (x + width() > geo.right()) {
            x = ballPos.x() - width() - 8;
            m_ballOnRight = false;
        }
        if (y < geo.top()) y = geo.top();
        if (y + height() > geo.bottom()) y = geo.bottom() - height();
    }
    move(x, y);
}

void Widget::setupUi()
{
    setWindowTitle("CC Monitor");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setMinimumWidth(370);
    setMouseTracking(true);
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
    header->setSpacing(6);
    header->addWidget(title, 1);
    header->addWidget(addBtn);

    // Card container — no scroll area initially
    m_cardContainer = new QWidget;
    m_cardContainer->setStyleSheet("background: transparent;");
    m_cardLayout = new QVBoxLayout(m_cardContainer);
    m_cardLayout->setContentsMargins(8, 0, 8, 8);
    m_cardLayout->setSpacing(6);
    // No addStretch() — critical for elastic behavior

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addLayout(header);
    mainLayout->addWidget(m_cardContainer);

    resize(370, 44);
}

QSize Widget::sizeHint() const
{
    int cardH = 0;
    if (!m_cards.isEmpty()) {
        cardH = m_cardLayout->sizeHint().height();
    }
    int tb = (m_toolbarHeight > 0) ? m_toolbarHeight : 44;
    return QSize(width(), tb + cardH);
}

void Widget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_toolbarHeight == 0) {
        m_toolbarHeight = height() - m_cardContainer->height();
        if (m_toolbarHeight <= 0)
            m_toolbarHeight = 44;
        if (m_cards.isEmpty())
            resize(width(), m_toolbarHeight);
    }
}

void Widget::addCard(const QString &sessionId, SessionCard *card)
{
    m_cardLayout->addWidget(card);
    m_cards[sessionId] = card;
}

void Widget::removeCard(const QString &sessionId)
{
    m_prevStatus.remove(sessionId);
    if (SessionCard *card = m_cards.take(sessionId)) {
        m_cardLayout->removeWidget(card);
        card->hide();
        card->deleteLater();
    }
}

void Widget::elasticResize()
{
    QSize target = sizeHint();

    QScreen *screen = QGuiApplication::screenAt(pos() + QPoint(width() / 2, height() / 2));
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        int maxH = screen->availableGeometry().height() - 40;
        if (target.height() > maxH)
            target.setHeight(maxH);
    }

    // Reuse or create animation, stop old one to prevent jitter
    if (!m_resizeAnim) {
        m_resizeAnim = new QPropertyAnimation(this, "size");
        m_resizeAnim->setDuration(150);
        m_resizeAnim->setEasingCurve(QEasingCurve::OutQuad);
    }
    m_resizeAnim->stop();
    m_resizeAnim->setStartValue(size());
    m_resizeAnim->setEndValue(target);
    m_resizeAnim->start();
}

void Widget::onSessionsUpdated(const QList<CCSession> &sessions)
{
    int prevCardCount = m_cards.size();
    QSet<QString> alive;
    for (const auto &s : sessions) {
        alive.insert(s.sessionId);

        SessionCard *card = m_cards.value(s.sessionId);
        if (!card) {
            card = new SessionCard;
            connect(card, &SessionCard::clicked, this, &Widget::onCardClicked);
            connect(card, &SessionCard::closeRequested, this, [this](const QString &sessionId) {
                // Reentrancy guard: if card was already removed during modal dialog, bail
                if (!m_cards.contains(sessionId))
                    return;

                QMessageBox dlg(this);
                dlg.setWindowTitle("Stop Monitoring");
                dlg.setText("Remove this session from the monitor?");
                dlg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                dlg.setDefaultButton(QMessageBox::No);
                dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
                dlg.setStyleSheet(
                    "QMessageBox { background: #11111b; color: #cdd6f4; }"
                    "QLabel { color: #cdd6f4; font-size: 13px; }"
                    "QPushButton { background: #313244; color: #cdd6f4; border: none; "
                    "border-radius: 4px; padding: 6px 20px; font-weight: bold; }"
                    "QPushButton:hover { background: #45475a; }"
                );
                dlg.adjustSize();
                QPoint globalCenter = mapToGlobal(rect().center());
                dlg.move(globalCenter - dlg.rect().center());
                if (dlg.exec() != QMessageBox::Yes)
                    return;

                // UAF guard: if Widget was destroyed during modal dialog, bail
                QPointer<Widget> self = this;
                if (!self)
                    return;

                // Reentrancy: session may have been removed by onSessionsUpdated while dialog was open
                if (!m_cards.contains(sessionId))
                    return;

                m_monitor->stopMonitoring(sessionId);
                removeCard(sessionId);
                elasticResize();
            });
            addCard(s.sessionId, card);
        }
        card->updateFrom(s);

        // Detect blue/red → green (task completed)
        SessionStatus prev = m_prevStatus.value(s.sessionId, SessionStatus::Idle);
        if (s.status == SessionStatus::Idle && prev != SessionStatus::Idle) {
            m_soundMgr->playTaskCompleted();
            m_ball->triggerAlert(SessionStatus::Idle, s.label, s.pid);
        }
        m_prevStatus[s.sessionId] = s.status;
    }

    bool removed = false;
    QStringList toRemove;
    for (auto it = m_cards.constBegin(); it != m_cards.constEnd(); ++it) {
        if (!alive.contains(it.key()))
            toRemove.append(it.key());
    }
    for (const auto &sid : toRemove) {
        removeCard(sid);
        removed = true;
    }

    // Resize whenever card count changes (added or removed)
    if (removed || m_cards.size() != prevCardCount)
        elasticResize();

    updateFloatingBall();
}

void Widget::onNeedsAttention(const CCSession &session)
{
    m_soundMgr->playNeedsApproval();
    m_ball->triggerAlert(SessionStatus::WaitingApproval, session.label, session.pid);
}

void Widget::onSessionStuck(const CCSession &session)
{
    m_soundMgr->playSessionStuck();
    m_ball->triggerAlert(SessionStatus::Stuck, session.label, session.pid);
}

void Widget::onCardClicked(const QString &sessionId)
{
    auto it = m_cards.find(sessionId);
    if (it != m_cards.end()) {
        qint64 pid = it.value()->pid();
        activateProcessWindow(pid, m_monitor->getCwdForPid(pid));
    }
}

void Widget::addSession()
{
    AddSessionDialog dlg(m_monitor, this);
    if (dlg.exec() == QDialog::Accepted) {
        QList<CCSession> selected;
        for (const auto &sid : dlg.selectedSessionIds()) {
            for (const auto &s : m_monitor->scanAvailable()) {
                if (s.sessionId == sid) {
                    selected.append(s);
                    break;
                }
            }
        }
        std::sort(selected.begin(), selected.end(),
                  [](const CCSession &a, const CCSession &b) {
                      return a.startedAt < b.startedAt;
                  });
        for (const auto &s : selected)
            m_monitor->startMonitoring(s.sessionId);
    }
}

void Widget::onToggleWindow()
{
    if (isVisible()) {
        hide();
    } else {
        // Position next to the floating ball
        QPoint ballPos = m_ball->pos();
        int ballW = m_ball->width();
        QScreen *screen = QGuiApplication::screenAt(ballPos + QPoint(ballW / 2, 0));
        if (!screen) screen = QGuiApplication::primaryScreen();

        int x = ballPos.x() + ballW + 8;
        int y = ballPos.y();
        m_ballOnRight = true;

        if (screen) {
            QRect geo = screen->availableGeometry();
            if (x + width() > geo.right()) {
                x = ballPos.x() - width() - 8;
                m_ballOnRight = false;
            }
            if (y + height() > geo.bottom())
                y = geo.bottom() - height();
            if (y < geo.top())
                y = geo.top();
        }

        move(x, y);
        show();
        activateWindow();
        raise();
    }
}

void Widget::onJumpToSession(qint64 pid)
{
    activateProcessWindow(pid, m_monitor->getCwdForPid(pid));
}

void Widget::repositionNearBall()
{
    if (!isVisible()) return;

    QPoint ballPos = m_ball->pos();
    int ballW = m_ball->width();
    QScreen *screen = QGuiApplication::screenAt(ballPos + QPoint(ballW / 2, 0));
    if (!screen) screen = QGuiApplication::primaryScreen();

    int x;
    if (m_ballOnRight) {
        x = ballPos.x() + ballW + 8;
        if (screen && x + width() > screen->availableGeometry().right()) {
            m_ballOnRight = false;
            x = ballPos.x() - width() - 8;
        }
    }
    if (!m_ballOnRight) {
        x = ballPos.x() - width() - 8;
        if (screen && x < screen->availableGeometry().left()) {
            m_ballOnRight = true;
            x = ballPos.x() + ballW + 8;
        }
    }

    int y = ballPos.y();
    if (screen) {
        QRect geo = screen->availableGeometry();
        y = qBound(geo.top(), y, geo.bottom() - height());
    }

    move(x, y);
}

void Widget::updateFloatingBall()
{
    SessionStatus aggregate = SessionStatus::Idle;
    int waitingCount = 0, thinkingCount = 0, stuckCount = 0;

    for (const auto &s : m_prevStatus) {
        if (s == SessionStatus::WaitingApproval || s == SessionStatus::Error) {
            waitingCount++;
            aggregate = SessionStatus::WaitingApproval;
        } else if (s == SessionStatus::Stuck) {
            stuckCount++;
            if (aggregate != SessionStatus::WaitingApproval)
                aggregate = SessionStatus::Stuck;
        } else if (s == SessionStatus::Thinking) {
            thinkingCount++;
            if (aggregate == SessionStatus::Idle)
                aggregate = SessionStatus::Thinking;
        }
    }

    m_ball->setAggregateStatus(aggregate, m_cards.size(),
                               waitingCount, thinkingCount, stuckCount);
}

void Widget::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}

void Widget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        int rightEdge = width() - static_cast<int>(event->position().x());
        if (rightEdge <= 6) {
            m_resizingWidth = true;
            m_resizeStartWidth = width();
            m_resizeStartPos = event->globalPosition().toPoint();
        } else {
            m_resizingWidth = false;
            m_dragging = true;
            m_dragPos = event->globalPosition().toPoint() - pos();
        }
    }
    QWidget::mousePressEvent(event);
}

void Widget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        if (m_resizingWidth) {
            int dx = event->globalPosition().x() - m_resizeStartPos.x();
            int newWidth = qMax(minimumWidth(), m_resizeStartWidth + dx);
            resize(newWidth, height());
        } else if (m_dragging) {
            move(event->globalPosition().toPoint() - m_dragPos);
        }
    } else {
        int rightEdge = width() - static_cast<int>(event->position().x());
        setCursor(rightEdge <= 6 ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void Widget::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    m_resizingWidth = false;
    QWidget::mouseReleaseEvent(event);
}
