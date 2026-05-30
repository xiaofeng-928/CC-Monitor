#include "soundmanager.h"
#include <QSoundEffect>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

SoundManager::SoundManager(QObject *parent)
    : QObject(parent)
{
    loadConfig();
}

void SoundManager::loadConfig()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString iniPath = appDir + "/sounds.ini";

    if (!QFile::exists(iniPath))
        return;

    QSettings ini(iniPath, QSettings::IniFormat, this);
    ini.beginGroup("Sounds");

    QDir iniDir(appDir);

    QString taskFile = ini.value("task_completed").toString();
    QString approvalFile = ini.value("needs_approval").toString();
    QString stuckFile = ini.value("session_stuck").toString();

    ini.endGroup();

    if (!taskFile.isEmpty()) {
        QString resolved = QFileInfo(taskFile).isAbsolute()
            ? taskFile : iniDir.absoluteFilePath(taskFile);
        m_taskCompleted = createEffect(resolved);
    }

    if (!approvalFile.isEmpty()) {
        QString resolved = QFileInfo(approvalFile).isAbsolute()
            ? approvalFile : iniDir.absoluteFilePath(approvalFile);
        m_needsApproval = createEffect(resolved);
    }

    if (!stuckFile.isEmpty()) {
        QString resolved = QFileInfo(stuckFile).isAbsolute()
            ? stuckFile : iniDir.absoluteFilePath(stuckFile);
        m_stuck = createEffect(resolved);
    }
}

QSoundEffect* SoundManager::createEffect(const QString &filePath)
{
    if (!QFile::exists(filePath))
        return nullptr;

    auto *effect = new QSoundEffect(this);
    effect->setSource(QUrl::fromLocalFile(filePath));
    effect->setVolume(1.0);
    effect->setLoopCount(1);

    if (effect->status() == QSoundEffect::Error)
        return nullptr;

    return effect;
}

void SoundManager::playTaskCompleted()
{
    if (m_taskCompleted && m_taskCompleted->status() != QSoundEffect::Error)
        m_taskCompleted->play();
}

void SoundManager::playNeedsApproval()
{
    if (m_needsApproval && m_needsApproval->status() != QSoundEffect::Error)
        m_needsApproval->play();
}

void SoundManager::playSessionStuck()
{
    if (m_stuck && m_stuck->status() != QSoundEffect::Error)
        m_stuck->play();
}
