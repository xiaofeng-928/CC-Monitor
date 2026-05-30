#ifndef SESSIONCLIENT_H
#define SESSIONCLIENT_H

#include <QObject>
#include <QProcess>
#include <QMap>
#include <QSet>
#include <QDateTime>

enum class SessionStatus {
    WaitingApproval,  // 红色 — 需要用户批准
    Thinking,         // 蓝色 — AI 工作中
    Stuck,            // 黄色 — API 无响应
    Idle,             // 绿色 — 空闲/已完成
    Error             // 红色 — 错误
};

struct CCSession {
    QString  sessionId;
    QString  label;
    qint64   pid = 0;
    QString  entrypoint;        // "claude-vscode" / "cli"
    QString  cwd;
    SessionStatus status = SessionStatus::Idle;

    // from JSONL events
    QString  model;
    QString  gitBranch;
    QString  permissionMode;
    double   costUSD = 0;
    int      tokensIn = 0;
    int      tokensOut = 0;
    int      turnCount = 0;
    QStringList activeFiles;

    // internal tracking
    QDateTime lastEventAt;
    QString   lastEventType;
    QString   lastStopReason;
    QSet<QString> pendingToolUseIds;
    QMap<QString, QDateTime> toolUseTimestamps;  // track when each tool_use was issued
    QMap<QString, QString> toolUseNames;         // tool_use id → tool name
    qint64    filePos = 0;      // incremental read offset
    QString   jsonlPath;        // cached path to JSONL file
    QDateTime lastUserPromptAt; // timestamp of last real user prompt (not tool_result)
    bool      awaitingAssistant = false;  // user sent prompt, waiting for AI
    QDateTime startedAt;        // when session was created
};

class QTimer;

class SessionMonitor : public QObject
{
    Q_OBJECT

public:
    explicit SessionMonitor(QObject *parent = nullptr);

    void start();
    void stop();

    QList<CCSession> scanAvailable();
    void startMonitoring(const QString &sessionId);
    void stopMonitoring(const QString &sessionId);
    bool isMonitoring(const QString &sessionId) const;

signals:
    void sessionsUpdated(const QList<CCSession> &sessions);
    void sessionNeedsAttention(const CCSession &session);
    void sessionStuck(const CCSession &session);

private:
    void onTimeout();
    void scanSessions();
    void scanJsonlFiles();
    void processFile(const QString &path, CCSession &session);
    void processEvent(const QJsonObject &event, CCSession &session);
    SessionStatus deriveStatus(const CCSession &session) const;
    QString findJsonlForSession(const CCSession &session) const;
    QString claudeDir() const;

    QTimer *m_timer;
    int     m_sessionScanCounter = 0;       // decouple scanSessions frequency from JSONL
    QMap<QString, CCSession> m_monitored;   // sessionId → session
    QMap<QString, qint64>    m_knownPids;   // sessionId → pid (from sessions/)
};

#endif // SESSIONCLIENT_H
