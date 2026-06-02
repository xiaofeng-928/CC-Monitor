#ifndef WINDOWACTIVATOR_H
#define WINDOWACTIVATOR_H

#include <QtGlobal>
#include <QString>

void activateProcessWindow(qint64 pid, const QString &cwd = {});

#endif // WINDOWACTIVATOR_H
