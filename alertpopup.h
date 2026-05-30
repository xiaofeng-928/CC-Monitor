#ifndef ALERTPOPUP_H
#define ALERTPOPUP_H

#include <QWidget>
#include "sessionclient.h"

class QLabel;
class QPushButton;
class QTimer;

class AlertPopup : public QWidget
{
    Q_OBJECT

public:
    explicit AlertPopup(QWidget *parent = nullptr);

    void showAlert(const QString &label, SessionStatus status, qint64 pid,
                   const QPoint &anchor);
    void anchorTo(const QPoint &anchor);

    qint64 pid() const;

signals:
    void jumpToSession(qint64 pid);
    void dismissed(AlertPopup *self);

private:
    void setupUi();
    QString statusColorHex(SessionStatus status) const;
    QString statusText(SessionStatus status) const;

    qint64     m_pid = 0;
    QLabel    *m_dot;
    QLabel    *m_labelText;
    QLabel    *m_statusText;
    QPushButton *m_jumpBtn;
    QTimer    *m_autoHideTimer = nullptr;
};

#endif // ALERTPOPUP_H
