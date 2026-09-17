#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QRegularExpression>

namespace build_studio {

enum class LogLevel {
    Info,
    Success,
    Warning,
    Error
};

struct BuildOptions {
    QString rootDir;
    QStringList targets;
    QString hostCompiler; // "gcc" or "clang"
    QString sreCompiler;  // "aarch64-linux-gnu-gcc"
    bool strip;
    bool clean;
    bool useDynarmic;
};

class BuildRunner : public QObject {
    Q_OBJECT
public:
    explicit BuildRunner(QObject* parent = nullptr);
    ~BuildRunner() override;

    bool isRunning() const;
    void startBuild(const BuildOptions& options);
    void cancelBuild();

signals:
    void progressChanged(int percentage, const QString& stepDescription);
    void logAppended(const QString& text, LogLevel level);
    void buildCompleted(bool success, int exitCode);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void processLine(const QString& line, bool isStderr);

    QProcess* m_process = nullptr;
    QRegularExpression m_percentRegex;
    QRegularExpression m_stepRegex;
};

} // namespace build_studio
