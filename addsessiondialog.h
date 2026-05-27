#ifndef ADDSESSIONDIALOG_H
#define ADDSESSIONDIALOG_H

#include <QDialog>
#include "sessionclient.h"

class QListWidget;
class QListWidgetItem;

class AddSessionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddSessionDialog(SessionMonitor *monitor, QWidget *parent = nullptr);

    QStringList selectedSessionIds() const;

private:
    void refreshList();

    SessionMonitor *m_monitor;
    QListWidget    *m_list;
};

#endif // ADDSESSIONDIALOG_H
