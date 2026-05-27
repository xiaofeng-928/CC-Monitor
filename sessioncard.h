#ifndef SESSIONCARD_H
#define SESSIONCARD_H

#include <QFrame>
#include "sessionclient.h"

class QLabel;
class QPushButton;

class SessionCard : public QFrame
{
    Q_OBJECT

public:
    explicit SessionCard(QWidget *parent = nullptr);

    void updateFrom(const CCSession &session);

    QString sessionId() const { return m_sessionId; }
    qint64  pid() const { return m_pid; }

signals:
    void clicked(const QString &sessionId);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void applyStatusStyle(SessionStatus status);

    QString  m_sessionId;
    qint64   m_pid = 0;
    QLabel  *m_statusDot;
    QLabel  *m_labelText;
    QLabel  *m_pidText;
    QLabel  *m_detailText;
    QPushButton *m_closeBtn;
};

#endif // SESSIONCARD_H
