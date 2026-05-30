#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QMap>
#include "sessionclient.h"

class QVBoxLayout;
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
    void onSessionsUpdated(const QList<CCSession> &sessions);
    void onNeedsAttention(const CCSession &session);
    void onSessionStuck(const CCSession &session);
    void onCardClicked(const QString &sessionId);
    void addSession();
    void onToggleWindow();
    void onJumpToSession(qint64 pid);
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

    // Drag / resize state
    QPoint m_dragPos;
    bool   m_resizingWidth = false;
    int    m_resizeStartWidth = 0;
    QPoint m_resizeStartPos;
};

#endif // WIDGET_H
