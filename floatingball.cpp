#include "floatingball.h"
#include "alertpopup.h"
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QScreen>
#include <QSettings>
#include <QMoveEvent>
#include <QContextMenuEvent>
#include <QToolTip>

static constexpr int BallSize = 42;
static constexpr int GlowMargin = 8;
static constexpr int TotalSize = BallSize + GlowMargin * 2;
static constexpr int BadgeSize = 16;
static constexpr int SnapThreshold = 20;
static constexpr int DragThreshold = 5;

FloatingBall::FloatingBall(QWidget *parent)
    : QWidget(parent)
{
    setupWindow();
}

void FloatingBall::setupWindow()
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(TotalSize, TotalSize);
    setMouseTracking(true);
    loadPosition();
}

void FloatingBall::setAggregateStatus(SessionStatus status, int sessionCount,
                                       int waitingCount, int thinkingCount,
                                       int stuckCount)
{
    if (m_status == status && m_sessionCount == sessionCount
        && m_waitingCount == waitingCount && m_thinkingCount == thinkingCount
        && m_stuckCount == stuckCount)
        return;

    m_status = status;
    m_sessionCount = sessionCount;
    m_waitingCount = waitingCount;
    m_thinkingCount = thinkingCount;
    m_stuckCount = stuckCount;

    // Update pulse animation based on new status
    stopPulseAnimation();
    startPulseForStatus();

    setToolTip(computeTooltip());
    update();
}

void FloatingBall::triggerAlert(SessionStatus urgency, const QString &label, qint64 pid)
{
    // 3 fast flashes then settle into steady pulse
    stopPulseAnimation();

    auto *seq = new QSequentialAnimationGroup(this);

    for (int i = 0; i < 3; ++i) {
        auto *up = new QPropertyAnimation(this, "glowOpacity");
        up->setDuration(120);
        up->setStartValue(0.2);
        up->setEndValue(1.0);
        seq->addAnimation(up);

        auto *down = new QPropertyAnimation(this, "glowOpacity");
        down->setDuration(120);
        down->setStartValue(1.0);
        down->setEndValue(0.2);
        seq->addAnimation(down);
    }

    connect(seq, &QSequentialAnimationGroup::finished, this, [this, seq]() {
        startPulseForStatus();
        seq->deleteLater();
    });

    seq->start();

    // Show alert popup if we have session info
    if (!label.isEmpty() && pid > 0)
        showAlert(label, urgency, pid);
}

void FloatingBall::showAlert(const QString &label, SessionStatus status, qint64 pid)
{
    // Remove old popup for same pid if it exists
    for (auto *popup : m_popups) {
        if (popup->pid() == pid) {
            removePopup(popup);
            break;
        }
    }

    // Max 3 popups — remove oldest
    while (m_popups.size() >= 3)
        removePopup(m_popups.first());

    auto *popup = new AlertPopup;
    connect(popup, &AlertPopup::jumpToSession, this, &FloatingBall::jumpToSession);
    connect(popup, &AlertPopup::dismissed, this, [this](AlertPopup *p) {
        removePopup(p);
    });

    m_popups.append(popup);
    repositionPopups();
    popup->showAlert(label, status, pid, ballAnchorPoint());
}

qreal FloatingBall::glowOpacity() const
{
    return m_glowOpacity;
}

void FloatingBall::setGlowOpacity(qreal opacity)
{
    m_glowOpacity = opacity;
    update();
}

void FloatingBall::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QString color = statusColorHex(m_status);
    QColor statusColor(color);

    int r = BallSize / 2;
    int cx = GlowMargin + r;
    int cy = GlowMargin + r;

    // Outer glow (only when not idle)
    if (m_status != SessionStatus::Idle && m_glowOpacity > 0) {
        QColor glowColor = statusColor;
        glowColor.setAlphaF(m_glowOpacity * 0.4);
        QRadialGradient glowGrad(QPointF(cx, cy), r + GlowMargin);
        glowGrad.setColorAt(0.6, glowColor);
        glowGrad.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.setBrush(glowGrad);
        p.setPen(Qt::NoPen);
        p.drawEllipse(0, 0, TotalSize, TotalSize);
    }

    // Main circle fill
    p.setBrush(QColor("#1e1e2e"));
    QColor borderColor = (m_status == SessionStatus::Idle)
        ? QColor("#313244")
        : statusColor;
    QPen borderPen(borderColor, 2);
    p.setPen(borderPen);
    p.drawEllipse(GlowMargin + 1, GlowMargin + 1, BallSize - 2, BallSize - 2);

    // "CC" text
    p.setPen((m_status == SessionStatus::Idle)
        ? QColor("#585b70")
        : QColor("#cdd6f4"));
    QFont font = p.font();
    font.setPixelSize(13);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QRect(GlowMargin, GlowMargin, BallSize, BallSize), Qt::AlignCenter, "CC");

    // Badge (session count or "!")
    if (m_sessionCount > 0 || m_status == SessionStatus::WaitingApproval) {
        int bx = GlowMargin + BallSize - BadgeSize + 2;
        int by = GlowMargin + BallSize - BadgeSize + 2;

        p.setBrush(statusColor);
        p.setPen(Qt::NoPen);
        p.drawEllipse(bx, by, BadgeSize, BadgeSize);

        p.setPen(QColor("#1e1e2e"));
        font.setPixelSize(10);
        font.setBold(true);
        p.setFont(font);

        QString badgeText = (m_status == SessionStatus::WaitingApproval || m_status == SessionStatus::Error)
            ? "!" : QString::number(m_sessionCount);
        p.drawText(QRect(bx, by, BadgeSize, BadgeSize), Qt::AlignCenter, badgeText);
    }
}

void FloatingBall::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_wasDragged = false;
        m_dragStartPos = event->globalPosition().toPoint();
        m_windowStartPos = pos();
    }
    QWidget::mousePressEvent(event);
}

void FloatingBall::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        if (!m_wasDragged && delta.manhattanLength() > DragThreshold)
            m_wasDragged = true;
        if (m_wasDragged)
            move(m_windowStartPos + delta);
    }
    QWidget::mouseMoveEvent(event);
}

void FloatingBall::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (!m_wasDragged)
            emit toggleMainWindow();
        else
            snapToEdge();
        savePosition();
        m_dragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void FloatingBall::enterEvent(QEnterEvent *event)
{
    m_hovering = true;
    QWidget::enterEvent(event);
}

void FloatingBall::leaveEvent(QEvent *event)
{
    m_hovering = false;
    QWidget::leaveEvent(event);
}

void FloatingBall::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu;
    menu.setStyleSheet(
        "QMenu { background: #1e1e2e; color: #cdd6f4; border: 1px solid #313244; }"
        "QMenu::item:selected { background: #313244; }"
    );
    menu.addAction("Quit", qApp, &QApplication::quit);
    menu.exec(event->globalPos());
}

void FloatingBall::startPulseForStatus()
{
    switch (m_status) {
    case SessionStatus::WaitingApproval:
    case SessionStatus::Error:
        startPulseAnimation(1000);
        break;
    case SessionStatus::Stuck:
        startPulseAnimation(2000);
        break;
    case SessionStatus::Thinking:
        startPulseAnimation(3000);
        break;
    case SessionStatus::Idle:
        m_glowOpacity = 0.0;
        update();
        break;
    }
}

void FloatingBall::startPulseAnimation(int durationMs)
{
    m_pulseAnim = new QPropertyAnimation(this, "glowOpacity");
    m_pulseAnim->setDuration(durationMs);
    m_pulseAnim->setStartValue(0.2);
    m_pulseAnim->setEndValue(1.0);
    m_pulseAnim->setEasingCurve(QEasingCurve::CosineCurve);
    m_pulseAnim->setLoopCount(-1);
    m_pulseAnim->start();
}

void FloatingBall::stopPulseAnimation()
{
    if (m_pulseAnim) {
        m_pulseAnim->stop();
        m_pulseAnim->deleteLater();
        m_pulseAnim = nullptr;
    }
}

void FloatingBall::snapToEdge()
{
    QScreen *screen = QGuiApplication::screenAt(pos() + QPoint(TotalSize / 2, TotalSize / 2));
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    QRect geo = screen->availableGeometry();
    QPoint center = pos() + QPoint(TotalSize / 2, TotalSize / 2);

    int distLeft = center.x() - geo.left();
    int distRight = geo.right() - center.x();
    int distTop = center.y() - geo.top();
    int distBottom = geo.bottom() - center.y();

    int minDist = qMin(qMin(distLeft, distRight), qMin(distTop, distBottom));

    QPoint target = pos();

    if (minDist < SnapThreshold) {
        if (minDist == distLeft)
            target.setX(geo.left());
        else if (minDist == distRight)
            target.setX(geo.right() - TotalSize);
        else if (minDist == distTop)
            target.setY(geo.top());
        else
            target.setY(geo.bottom() - TotalSize);
    }

    if (target != pos()) {
        auto *anim = new QPropertyAnimation(this, "pos");
        anim->setDuration(150);
        anim->setEndValue(target);
        anim->setEasingCurve(QEasingCurve::OutQuad);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void FloatingBall::savePosition()
{
    QSettings settings("CCMonitor", "FloatingBall");
    settings.setValue("pos", pos());
}

void FloatingBall::loadPosition()
{
    QSettings settings("CCMonitor", "FloatingBall");
    if (settings.contains("pos")) {
        move(settings.value("pos").toPoint());
    } else {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect geo = screen->availableGeometry();
            move(geo.right() - TotalSize - 20, geo.top() + 20);
        }
    }
}

QString FloatingBall::statusColorHex(SessionStatus status) const
{
    // Idle uses gray for the ball border (intentionally different from card/popup green)
    if (status == SessionStatus::Idle)
        return "#313244";
    return sessionStatusColor(status);
}

QString FloatingBall::computeTooltip() const
{
    if (m_sessionCount == 0)
        return "CC Monitor — No sessions";

    QStringList parts;
    if (m_waitingCount > 0)
        parts << QString("%1 need%2 approval").arg(m_waitingCount).arg(m_waitingCount > 1 ? "" : "s");
    if (m_stuckCount > 0)
        parts << QString("%1 stuck").arg(m_stuckCount);
    if (m_thinkingCount > 0)
        parts << QString("%1 thinking").arg(m_thinkingCount);

    int idleCount = m_sessionCount - m_waitingCount - m_stuckCount - m_thinkingCount;
    if (idleCount > 0)
        parts << QString("%1 idle").arg(idleCount);

    return QString("CC Monitor — %1 session%2: %3")
        .arg(m_sessionCount)
        .arg(m_sessionCount > 1 ? "s" : "")
        .arg(parts.join(", "));
}

QPoint FloatingBall::ballAnchorPoint() const
{
    return pos() + QPoint(TotalSize / 2, 0);
}

void FloatingBall::repositionPopups()
{
    QPoint anchor = ballAnchorPoint();
    int offsetY = 0;
    for (auto *popup : m_popups) {
        popup->anchorTo(QPoint(anchor.x(), anchor.y() - offsetY));
        offsetY += popup->height() + 4;
    }
}

void FloatingBall::removePopup(AlertPopup *popup)
{
    m_popups.removeOne(popup);
    popup->hide();
    popup->deleteLater();
    repositionPopups();
}

void FloatingBall::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    repositionPopups();
    emit ballMoved();
}
