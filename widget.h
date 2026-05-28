#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QSystemTrayIcon>
#include "sessionclient.h"

class QVBoxLayout;
class SessionCard;

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupUi();
    void setupTray();
    void onSessionsUpdated(const QList<CCSession> &sessions);
    void onNeedsAttention(const CCSession &session);
    void onSessionStuck(const CCSession &session);
    void onCardClicked(const QString &sessionId);
    void addSession();

    SessionMonitor         *m_monitor;
    QSystemTrayIcon        *m_tray;
    QVBoxLayout            *m_cardLayout;
    QMap<QString, SessionCard*> m_cards;
    QMap<QString, SessionStatus> m_prevStatus;
};

#endif // WIDGET_H
