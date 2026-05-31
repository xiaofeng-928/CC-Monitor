#ifndef FLOATINGBALL_H
#define FLOATINGBALL_H

#include <QWidget>
#include <QList>
#include "sessionclient.h"

class QPropertyAnimation;
class AlertPopup;

class FloatingBall : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal glowOpacity READ glowOpacity WRITE setGlowOpacity)

public:
    explicit FloatingBall(QWidget *parent = nullptr);

    void setAggregateStatus(SessionStatus status, int sessionCount,
                            int waitingCount = 0, int thinkingCount = 0,
                            int stuckCount = 0);
    void triggerAlert(SessionStatus urgency, const QString &label = QString(), qint64 pid = 0);

    qreal glowOpacity() const;
    void setGlowOpacity(qreal opacity);

signals:
    void toggleMainWindow();
    void jumpToSession(qint64 pid);
    void ballMoved();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    void setupWindow();
    void startPulseForStatus();
    void startPulseAnimation(int durationMs);
    void stopPulseAnimation();
    void snapToEdge();
    void savePosition();
    void loadPosition();
    QString statusColorHex(SessionStatus status) const;
    QString computeTooltip() const;
    QPoint ballAnchorPoint() const;
    void repositionPopups();
    void removePopup(AlertPopup *popup);
    void showAlert(const QString &label, SessionStatus status, qint64 pid);

    SessionStatus m_status = SessionStatus::Idle;
    int m_sessionCount = 0;
    int m_waitingCount = 0;
    int m_thinkingCount = 0;
    int m_stuckCount = 0;

    // Drag state
    bool m_dragging = false;
    bool m_wasDragged = false;
    QPoint m_dragStartPos;
    QPoint m_windowStartPos;

    // Animation
    qreal m_glowOpacity = 0.0;
    QPropertyAnimation *m_pulseAnim = nullptr;

    // Hover
    bool m_hovering = false;

    // Alert popups
    QList<AlertPopup*> m_popups;
};

#endif // FLOATINGBALL_H
