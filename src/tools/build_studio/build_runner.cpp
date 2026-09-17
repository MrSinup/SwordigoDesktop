#include "build_runner.h"
#include <QFileInfo>

namespace build_studio {

BuildRunner::BuildRunner(QObject* parent)
    : QObject(parent),
      m_percentRegex(R"(\[\s*(\d+)%\])"),
      m_stepRegex(R"(\[\s*(\d+)/(\d+)\])")
{
}

BuildRunner::~BuildRunner() {
    cancelBuild();
}

bool BuildRunner::isRunning() const {
    return m_process && m_process->state() != QProcess::NotRunning;
}

void BuildRunner::startBuild(const BuildOptions& options) {
    if (isRunning()) return;

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, &BuildRunner::onReadyReadStandardOutput);
        connect(m_process, &QProcess::readyReadStandardError, this, &BuildRunner::onReadyReadStandardError);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &BuildRunner::onProcessFinished);
    }

    m_process->setWorkingDirectory(options.rootDir);

    QString program;
    QStringList args;

#ifdef Q_OS_WIN
    program = "cmd.exe";
    args << "/c" << "build_swordfare.bat" << "--nogui";
#else
    program = options.rootDir + "/build_swordfare.sh";
    args << "--nogui";
#endif

    if (options.clean) {
        args << "--clean";
    }

    if (options.strip) {
        args << "--strip";
    }

    if (!options.useDynarmic) {
        args << "--no-dynarmic";
    }

    if (!options.hostCompiler.isEmpty()) {
        args << "--compiler-host" << options.hostCompiler;
    }

    if (!options.sreCompiler.isEmpty()) {
        args << "--compiler-sre" << options.sreCompiler;
    }

    for (const QString& target : options.targets) {
        args << "--target" << target;
    }

    emit logAppended(QString("Starting build process: %1 %2\n").arg(program, args.join(" ")), LogLevel::Info);
    emit progressChanged(0, "Configuring...");

    m_process->start(program, args);
}

void BuildRunner::cancelBuild() {
    if (isRunning()) {
        emit logAppended("\n[Build Studio] Cancelling build by user request...\n", LogLevel::Warning);
        m_process->terminate();
        if (!m_process->waitForFinished(2000)) {
            m_process->kill();
        }
    }
}

void BuildRunner::onReadyReadStandardOutput() {
    while (m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        processLine(line, false);
    }
}

void BuildRunner::onReadyReadStandardError() {
    while (m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        processLine(line, true);
    }
}

void BuildRunner::processLine(const QString& line, bool isStderr) {
    if (line.isEmpty()) return;

    // Check for [ 45%] pattern (GNU Make)
    QRegularExpressionMatch pctMatch = m_percentRegex.match(line);
    if (pctMatch.hasMatch()) {
        int pct = pctMatch.captured(1).toInt();
        emit progressChanged(pct, line);
    } else {
        // Check for [45/100] pattern (Ninja)
        QRegularExpressionMatch stepMatch = m_stepRegex.match(line);
        if (stepMatch.hasMatch()) {
            int current = stepMatch.captured(1).toInt();
            int total = stepMatch.captured(2).toInt();
            if (total > 0) {
                int pct = (current * 100) / total;
                emit progressChanged(pct, line);
            }
        }
    }

    LogLevel level = LogLevel::Info;
    if (line.contains("error:", Qt::CaseInsensitive) || line.contains("fatal error:", Qt::CaseInsensitive)) {
        level = LogLevel::Error;
    } else if (line.contains("warning:", Qt::CaseInsensitive)) {
        level = LogLevel::Warning;
    } else if (line.contains("Built target", Qt::CaseInsensitive) || line.contains("Linking", Qt::CaseInsensitive) || line.contains("success", Qt::CaseInsensitive)) {
        level = LogLevel::Success;
    } else if (isStderr) {
        level = LogLevel::Warning;
    }

    emit logAppended(line, level);
}

void BuildRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    bool success = (exitStatus == QProcess::NormalExit && exitCode == 0);
    if (success) {
        emit progressChanged(100, "Build completed successfully.");
        emit logAppended("\n[Build Studio] Build completed successfully!\n", LogLevel::Success);
    } else {
        emit logAppended(QString("\n[Build Studio] Build failed with exit code %1\n").arg(exitCode), LogLevel::Error);
    }
    emit buildCompleted(success, exitCode);
}

} // namespace build_studio
