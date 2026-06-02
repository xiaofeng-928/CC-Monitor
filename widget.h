#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QMap>
#include <QPointer>
#include "sessionclient.h"

class QVBoxLayout;
class QPropertyAnimation;
class SessionCard;
class FloatingBall;
class SoundManager;

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void setupUi();
    void removeCard(const QString &sessionId);
    void addCard(const QString &sessionId, SessionCard *card);
    void onSessionsUpdated(const QList<CCSession> &sessions);
    void onNeedsAttention(const CCSession &session);
    void onSessionStuck(const CCSession &session);
    void onCardClicked(const QString &sessionId);
    void addSession();
    void onToggleWindow();
    void onJumpToSession(qint64 pid);
    void repositionNearBall();
    void elasticResize();
    void updateFloatingBall();

    SessionMonitor         *m_monitor;
    FloatingBall           *m_ball;
    SoundManager           *m_soundMgr;
    QVBoxLayout            *m_cardLayout;
    QWidget                *m_cardContainer;
    QMap<QString, SessionCard*> m_cards;
    QMap<QString, SessionStatus> m_prevStatus;

    // Elastic layout
    int  m_toolbarHeight = 0;
    QPropertyAnimation *m_resizeAnim = nullptr;

    // Drag / resize state
    QPoint m_dragPos;
    bool   m_dragging = false;
    bool   m_resizingWidth = false;
    int    m_resizeStartWidth = 0;
    QPoint m_resizeStartPos;

    // Ball-following state
    bool   m_ballOnRight = true;
};

#endif // WIDGET_H
