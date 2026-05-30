#include "sessionclient.h"
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDirIterator>
#include <climits>

static constexpr int MaxJsonlLinesPerScan = 1000;
static constexpr qint64 MaxJsonlBytesPerScan = 1024 * 1024;

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static QDateTime parseClaudeTimestamp(const QString &ts)
{
    QDateTime dt = QDateTime::fromString(ts, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(ts, Qt::ISODate);
    return dt;
}

static bool isPidAlive(qint64 pid, const QDateTime &sessionStartedAt = {})
{
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return false;

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(h, &exitCode) || exitCode != STILL_ACTIVE) {
        CloseHandle(h);
        return false;
    }

    if (sessionStartedAt.isValid()) {
        FILETIME creationTime, exitTime, kernelTime, userTime;
        if (GetProcessTimes(h, &creationTime, &exitTime, &kernelTime, &userTime)) {
            ULARGE_INTEGER ticks;
            ticks.LowPart = creationTime.dwLowDateTime;
            ticks.HighPart = creationTime.dwHighDateTime;
            qint64 createdMs = static_cast<qint64>(ticks.QuadPart / 10000ULL) - 11644473600000LL;
            QDateTime processCreatedAt = QDateTime::fromMSecsSinceEpoch(createdMs);

            // A reused PID will have a process creation time after the session's
            // recorded start time. Allow a small clock/write-order tolerance.
            if (processCreatedAt > sessionStartedAt.addSecs(5)) {
                CloseHandle(h);
                return false;
            }
        }
    }

    CloseHandle(h);
    return true;
#else
    Q_UNUSED(sessionStartedAt);
    return kill(pid, 0) == 0;
#endif
}

static QString makeSessionLabel(const QString &cwd)
{
    QFileInfo current(QDir::cleanPath(cwd));
    QString child = current.fileName();
    QString parent = QFileInfo(current.absolutePath()).fileName();

    if (!parent.isEmpty() && !child.isEmpty())
        return parent + "/" + child;
    if (!child.isEmpty())
        return child;
    return cwd;
}

static bool isReadOnlyTool(const QString &name)
{
    return name == "Read"
        || name == "Grep"
        || name == "Glob"
        || name == "LS";
}

static bool isEditTool(const QString &name)
{
    return name == "Edit"
        || name == "Write"
        || name == "MultiEdit"
        || name == "NotebookEdit";
}

static bool shouldTreatPendingToolAsThinking(const QString &permissionMode, const QString &toolName)
{
    if (permissionMode == "bypassPermissions")
        return true;

    if (isReadOnlyTool(toolName))
        return true;

    if (permissionMode == "acceptEdits" && isEditTool(toolName))
        return true;

    return false;
}

SessionMonitor::SessionMonitor(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &SessionMonitor::onTimeout);
}

void SessionMonitor::start()
{
    onTimeout();
    m_timer->start(1500);
}

void SessionMonitor::stop()
{
    m_timer->stop();
}

QString SessionMonitor::claudeDir() const
{
    return QDir::homePath() + "/.claude";
}

QList<CCSession> SessionMonitor::scanAvailable()
{
    QList<CCSession> result;
    QString sessionsDir = claudeDir() + "/sessions";
    QDir dir(sessionsDir);
    if (!dir.exists()) return result;

    for (const auto &fi : dir.entryInfoList({"*.json"}, QDir::Files)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();

        CCSession s;
        s.sessionId  = obj["sessionId"].toString();
        s.pid        = obj["pid"].toVariant().toLongLong();
        s.cwd        = obj["cwd"].toString();
        s.entrypoint = obj["entrypoint"].toString();
        s.startedAt  = QDateTime::fromMSecsSinceEpoch(obj["startedAt"].toVariant().toLongLong());

        if (s.sessionId.isEmpty()) continue;
        if (s.pid > 0 && !isPidAlive(s.pid, s.startedAt)) continue;  // skip zombie/reused sessions

        // derive label from cwd
        if (!s.cwd.isEmpty())
            s.label = makeSessionLabel(s.cwd);
        if (s.label.isEmpty())
            s.label = QStringLiteral("PID:%1").arg(s.pid);

        result.append(s);
    }
    return result;
}

void SessionMonitor::startMonitoring(const QString &sessionId)
{
    if (m_monitored.contains(sessionId)) return;

    CCSession s;
    s.sessionId = sessionId;

    // try to populate from sessions/
    if (m_knownPids.contains(sessionId)) {
        s.pid = m_knownPids[sessionId];
    }

    // try to get cwd and startedAt from session file
    QString sessionsDir = claudeDir() + "/sessions";
    QDir dir(sessionsDir);
    if (dir.exists()) {
        for (const auto &fi : dir.entryInfoList({"*.json"}, QDir::Files)) {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) continue;
            QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
            if (obj["sessionId"].toString() == sessionId) {
                s.pid = obj["pid"].toVariant().toLongLong();
                s.cwd = obj["cwd"].toString();
                s.entrypoint = obj["entrypoint"].toString();
                s.startedAt = QDateTime::fromMSecsSinceEpoch(obj["startedAt"].toVariant().toLongLong());
                if (!s.cwd.isEmpty())
                    s.label = makeSessionLabel(s.cwd);
                break;
            }
        }
    }

    if (s.pid > 0 && !isPidAlive(s.pid, s.startedAt))
        return;

    // find and read initial JSONL data
    QString jsonlPath = findJsonlForSession(s);
    if (!jsonlPath.isEmpty()) {
        s.jsonlPath = jsonlPath;
        processFile(jsonlPath, s);
    }

    m_monitored[sessionId] = s;
}

void SessionMonitor::stopMonitoring(const QString &sessionId)
{
    m_monitored.remove(sessionId);
}

bool SessionMonitor::isMonitoring(const QString &sessionId) const
{
    return m_monitored.contains(sessionId);
}

void SessionMonitor::onTimeout()
{
    scanSessions();
    scanJsonlFiles();

    // emit updated list
    QList<CCSession> list;
    for (auto &s : m_monitored) {
        SessionStatus newStatus = deriveStatus(s);
        SessionStatus oldStatus = s.status;
        s.status = newStatus;

        if (newStatus != oldStatus) {
            if (newStatus == SessionStatus::WaitingApproval)
                emit sessionNeedsAttention(s);
            else if (newStatus == SessionStatus::Stuck)
                emit sessionStuck(s);
        }

        list.append(s);
    }
    emit sessionsUpdated(list);
}

void SessionMonitor::scanSessions()
{
    QString sessionsDir = claudeDir() + "/sessions";
    QDir dir(sessionsDir);
    if (!dir.exists()) {
        m_knownPids.clear();
        return;
    }

    QMap<QString, qint64> activePids;

    for (const auto &fi : dir.entryInfoList({"*.json"}, QDir::Files)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();

        QString sid = obj["sessionId"].toString();
        qint64 pid = obj["pid"].toVariant().toLongLong();
        QDateTime startedAt = QDateTime::fromMSecsSinceEpoch(obj["startedAt"].toVariant().toLongLong());

        // skip zombie/reused PID — PID file exists but process is dead or belongs to a newer process
        if (pid > 0 && !isPidAlive(pid, startedAt)) continue;

        if (!sid.isEmpty())
            activePids[sid] = pid;

        // update monitored session's pid/cwd if found
        if (m_monitored.contains(sid)) {
            auto &s = m_monitored[sid];
            s.pid = pid;
            if (s.cwd.isEmpty()) s.cwd = obj["cwd"].toString();
            if (s.entrypoint.isEmpty()) s.entrypoint = obj["entrypoint"].toString();
            if (!s.startedAt.isValid()) s.startedAt = startedAt;
            if (s.label.isEmpty() && !s.cwd.isEmpty())
                s.label = makeSessionLabel(s.cwd);
        }
    }

    m_knownPids = activePids;
}

void SessionMonitor::scanJsonlFiles()
{
    for (auto it = m_monitored.begin(); it != m_monitored.end(); ++it) {
        CCSession &s = it.value();

        if (s.jsonlPath.isEmpty()) {
            s.jsonlPath = findJsonlForSession(s);
        } else if (!QFile::exists(s.jsonlPath)) {
            // cached path gone (file deleted/moved) — retry
            s.jsonlPath = findJsonlForSession(s);
        }

        if (s.jsonlPath.isEmpty()) continue;
        processFile(s.jsonlPath, s);
    }
}

static QString encodeCwd(const QString &cwd)
{
    QString result = cwd;
    result.replace(QChar('\\'), QChar('-'));
    result.replace(QChar('/'), QChar('-'));
    result.replace(QChar(':'), QChar('-'));
    return result;
}

static QString findProjectDir(const QString &projectsDir, const QString &cwd)
{
    QString encoded = encodeCwd(cwd);
    QDir dir(projectsDir);
    for (const auto &fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QString::compare(fi.fileName(), encoded, Qt::CaseInsensitive) == 0)
            return fi.absoluteFilePath();
    }
    return {};
}

struct JsonlMetadata {
    QString sessionId;
    QString entrypoint;
    QString cwd;
    QDateTime firstTimestamp;
};

static JsonlMetadata readJsonlMetadata(const QString &jsonPath)
{
    JsonlMetadata meta;
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) return meta;

    QByteArray line = f.readLine();
    f.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError) return meta;

    QJsonObject obj = doc.object();
    meta.sessionId  = obj["sessionId"].toString();
    meta.entrypoint = obj["entrypoint"].toString();
    meta.cwd        = obj["cwd"].toString();

    QString ts = obj["timestamp"].toString();
    if (!ts.isEmpty())
        meta.firstTimestamp = parseClaudeTimestamp(ts);

    return meta;
}

QString SessionMonitor::findJsonlForSession(const CCSession &session) const
{
    QString projectsDir = claudeDir() + "/projects";
    QDir dir(projectsDir);
    if (!dir.exists()) return {};

    // Step 1: Exact sessionId match (fast path)
    QDirIterator it(projectsDir, {session.sessionId + "*.jsonl"}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        if (path.contains("compact")) continue;
        return path;
    }

    // Step 2: Fallback - find unclaimed JSONL in same project
    if (session.cwd.isEmpty()) return {};

    QString projectPath = findProjectDir(projectsDir, session.cwd);
    if (projectPath.isEmpty()) return {};

    // Collect all JSONL files (non-recursive)
    QFileInfoList files = QDir(projectPath).entryInfoList({"*.jsonl"}, QDir::Files);

    // Build exclusion set: paths claimed by other monitored sessions
    QSet<QString> claimedPaths;
    for (auto it2 = m_monitored.constBegin(); it2 != m_monitored.constEnd(); ++it2) {
        if (it2.key() == session.sessionId) continue;
        if (!it2.value().jsonlPath.isEmpty())
            claimedPaths.insert(it2.value().jsonlPath);
    }

    // Find candidate JSONL files: unclaimed, not compact/subagents,
    // and not belonging to another active session (even if not yet monitored)
    QStringList candidates;
    for (const auto &fi : files) {
        if (fi.fileName().contains("compact")) continue;
        if (fi.fileName().contains("subagents")) continue;
        if (claimedPaths.contains(fi.absoluteFilePath())) continue;

        JsonlMetadata meta = readJsonlMetadata(fi.absoluteFilePath());
        // Skip JSONL files whose internal sessionId belongs to another active session
        if (!meta.sessionId.isEmpty()
            && meta.sessionId != session.sessionId
            && m_knownPids.contains(meta.sessionId))
            continue;

        candidates.append(fi.absoluteFilePath());
    }

    if (candidates.isEmpty()) return {};

    // Time proximity: select JSONL whose first event timestamp is closest to session startedAt
    if (!session.startedAt.isValid())
        return candidates.first();

    QString bestPath;
    qint64 bestDiff = LLONG_MAX;

    for (const auto &path : candidates) {
        JsonlMetadata meta = readJsonlMetadata(path);
        qint64 fileTime;
        if (meta.firstTimestamp.isValid())
            fileTime = meta.firstTimestamp.toMSecsSinceEpoch();
        else
            fileTime = QFileInfo(path).birthTime().toMSecsSinceEpoch();
        qint64 diff = qAbs(fileTime - session.startedAt.toMSecsSinceEpoch());
        if (diff < bestDiff) {
            bestDiff = diff;
            bestPath = path;
        }
    }

    return bestPath.isEmpty() ? candidates.first() : bestPath;
}

void SessionMonitor::processFile(const QString &path, CCSession &session)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;

    qint64 size = f.size();
    if (size < session.filePos)
        session.filePos = 0;
    if (size == session.filePos)
        return;

    if (!f.seek(session.filePos))
        return;

    int processedLines = 0;
    qint64 processedBytes = 0;

    while (!f.atEnd() && processedLines < MaxJsonlLinesPerScan && processedBytes < MaxJsonlBytesPerScan) {
        qint64 lineStart = f.pos();
        QByteArray line = f.readLine();
        processedBytes += line.size();

        // Claude Code writes JSONL incrementally; keep a partial tail for the
        // next scan instead of advancing past an incomplete JSON object.
        if (!line.endsWith('\n')) {
            f.seek(lineStart);
            break;
        }

        line = line.trimmed();
        if (line.isEmpty()) continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError) continue;
        processEvent(doc.object(), session);
        ++processedLines;
    }

    session.filePos = f.pos();
}

void SessionMonitor::processEvent(const QJsonObject &event, CCSession &session)
{
    QString type = event["type"].toString();
    if (type == "file-history-snapshot" || type == "queue-operation" || type == "last-prompt")
        return;

    // update timestamp
    QString ts = event["timestamp"].toString();
    if (!ts.isEmpty()) {
        QDateTime parsed = parseClaudeTimestamp(ts);
        session.lastEventAt = parsed.isValid() ? parsed : QDateTime::currentDateTime();
    }
    session.lastEventType = type;

    // extract session-level info
    if (event.contains("cwd") && session.cwd.isEmpty())
        session.cwd = event["cwd"].toString();
    if (event.contains("gitBranch") && session.gitBranch.isEmpty())
        session.gitBranch = event["gitBranch"].toString();
    if (event.contains("permissionMode"))
        session.permissionMode = event["permissionMode"].toString();

    // derive label from cwd
    if (session.label.isEmpty() && !session.cwd.isEmpty())
        session.label = makeSessionLabel(session.cwd);

    QJsonObject msg = event["message"].toObject();

    // assistant message
    if (type == "assistant" && msg["role"].toString() == "assistant") {
        session.awaitingAssistant = false;
        if (msg.contains("model"))
            session.model = msg["model"].toString();

        // usage
        QJsonObject usage = msg["usage"].toObject();
        if (!usage.isEmpty()) {
            session.tokensIn += usage["input_tokens"].toInt();
            session.tokensOut += usage["output_tokens"].toInt();
        }
        if (event.contains("costUSD"))
            session.costUSD += event["costUSD"].toDouble();
        else if (event.contains("cost_usd"))
            session.costUSD += event["cost_usd"].toDouble();

        // stop_reason
        QString stopReason = msg["stop_reason"].toString();
        if (!stopReason.isEmpty())
            session.lastStopReason = stopReason;

        // content blocks — collect tool_use IDs
        QJsonArray content = msg["content"].toArray();
        for (const auto &val : content) {
            QJsonObject block = val.toObject();
            if (block["type"].toString() == "tool_use") {
                QString toolId = block["id"].toString();
                if (!toolId.isEmpty()) {
                    session.pendingToolUseIds.insert(toolId);
                    session.toolUseTimestamps[toolId] = session.lastEventAt;
                    session.toolUseNames[toolId] = block["name"].toString();
                }

                // track active files
                QJsonObject input = block["input"].toObject();
                QString fp = input["file_path"].toString();
                if (fp.isEmpty()) fp = input["path"].toString();
                if (!fp.isEmpty()) {
                    QString base = QFileInfo(fp).fileName();
                    if (base.isEmpty())
                        base = fp;
                    session.activeFiles.removeAll(base);
                    session.activeFiles.append(base);
                    while (session.activeFiles.size() > 10)
                        session.activeFiles.removeFirst();
                }
            }
        }

        // turn count
        if (!stopReason.isEmpty())
            session.turnCount++;
    }

    // user message with tool_result
    if (type == "user" && msg["role"].toString() == "user") {
        QJsonArray content = msg["content"].toArray();
        bool isToolResult = false;
        for (const auto &val : content) {
            QJsonObject block = val.toObject();
            if (block["type"].toString() == "tool_result") {
                QString toolId = block["tool_use_id"].toString();
                session.pendingToolUseIds.remove(toolId);
                session.toolUseTimestamps.remove(toolId);
                session.toolUseNames.remove(toolId);
                isToolResult = true;
            }
        }
        if (isToolResult) {
            // all tool_results processed → AI should start thinking again
            if (session.pendingToolUseIds.isEmpty()) {
                session.awaitingAssistant = true;
                session.lastStopReason.clear();
            }
        } else {
            // real user prompt (not tool_result) → AI should start thinking
            session.activeFiles.clear();
            session.lastUserPromptAt = session.lastEventAt;
            session.awaitingAssistant = true;
            session.lastStopReason.clear();
        }
    }

    // system events
    if (type == "system") {
        QString subtype = event["subtype"].toString();
        if (subtype.isEmpty())
            subtype = msg["subtype"].toString();
        if (subtype == "api_error") {
            session.lastEventType = "error";
        }
        if (subtype == "turn_duration") {
            session.lastStopReason = "end_turn";
        }
    }
}

SessionStatus SessionMonitor::deriveStatus(const CCSession &session) const
{
    // error
    if (session.lastEventType == "error")
        return SessionStatus::Error;

    // pending tool_use without tool_result
    if (!session.pendingToolUseIds.isEmpty()) {
        constexpr qint64 approvalThresholdSecs = 3;
        bool allPendingToolsAreThinking = true;
        bool needsApproval = false;
        QDateTime now = QDateTime::currentDateTime();

        for (const QString &toolId : session.pendingToolUseIds) {
            QString toolName = session.toolUseNames.value(toolId);
            bool treatAsThinking = shouldTreatPendingToolAsThinking(session.permissionMode, toolName);
            if (!treatAsThinking)
                allPendingToolsAreThinking = false;

            QDateTime issuedAt = session.toolUseTimestamps.value(toolId);
            if (!treatAsThinking && issuedAt.isValid() && issuedAt.secsTo(now) >= approvalThresholdSecs)
                needsApproval = true;
        }

        if (allPendingToolsAreThinking)
            return SessionStatus::Thinking;         // blue — auto-executing/read-only work
        if (needsApproval)
            return SessionStatus::WaitingApproval;  // red — likely needs user approval
        return SessionStatus::Thinking;             // blue — below approval threshold
    }

    // no events yet → idle, unless session just started
    if (!session.lastEventAt.isValid()) {
        // If session started within last 5 minutes, show thinking (waiting for JSONL data)
        if (session.startedAt.isValid()) {
            qint64 secsSinceStart = session.startedAt.secsTo(QDateTime::currentDateTime());
            if (secsSinceStart < 300)  // 5 minutes
                return SessionStatus::Thinking;  // blue — waiting for data
        }
        return SessionStatus::Idle;
    }

    qint64 elapsed = session.lastEventAt.secsTo(QDateTime::currentDateTime());

    // Session inactive for 30s with no pending work → idle (task ended, crashed, force stopped)
    if (elapsed > 30 && session.pendingToolUseIds.isEmpty() && !session.awaitingAssistant)
        return SessionStatus::Idle;             // green

    // Session inactive for 2min with pending work → stuck (API timeout, etc.)
    if (elapsed > 120)
        return SessionStatus::Stuck;            // yellow

    // user sent a prompt, waiting for AI to respond
    if (session.awaitingAssistant) {
        return SessionStatus::Thinking;         // blue — waiting for AI
    }

    // assistant finished with end_turn → idle (green)
    if (session.lastStopReason == "end_turn")
        return SessionStatus::Idle;

    // assistant issued tool_use but all processed → thinking (blue)
    if (session.lastStopReason == "tool_use" && session.pendingToolUseIds.isEmpty())
        return SessionStatus::Thinking;         // blue — AI processing results

    // assistant still working (streaming or thinking)
    if (session.lastEventType == "assistant" ||
        session.lastEventType == "thinking" ||
        session.lastEventType == "text") {
        return SessionStatus::Thinking;         // blue
    }

    // fallback
    return SessionStatus::Thinking;             // blue
}
