#ifndef SOUNDMANAGER_H
#define SOUNDMANAGER_H

#include <QObject>

class QSoundEffect;

class SoundManager : public QObject
{
    Q_OBJECT

public:
    explicit SoundManager(QObject *parent = nullptr);

    void playTaskCompleted();
    void playNeedsApproval();
    void playSessionStuck();

private:
    void loadConfig();
    QSoundEffect* createEffect(const QString &filePath);

    QSoundEffect *m_taskCompleted = nullptr;
    QSoundEffect *m_needsApproval = nullptr;
    QSoundEffect *m_stuck = nullptr;
};

#endif // SOUNDMANAGER_H
