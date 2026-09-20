#pragma once

#include <QObject>
#include <QQuickTextDocument>
#include <QAbstractListModel>
#include <QString>
#include <memory>
#include "editor/filerift_highlighter.h"
#include "editor/filerift_analyzer.h"

namespace ruby::android {

struct QmlDiagnostic {
    int line;
    QString severity;
    QString message;
};

class DiagnosticsListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        LineRole = Qt::UserRole + 1,
        SeverityRole,
        MessageRole
    };

    explicit DiagnosticsListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDiagnostics(const std::vector<ruby::filerift::Diagnostic>& diags);
    void clear();

private:
    QVector<QmlDiagnostic> m_list;
};

class RubyHighlighterBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString filePath READ filePath WRITE setFilePath NOTIFY filePathChanged)

public:
    explicit RubyHighlighterBridge(QObject* parent = nullptr);
    ~RubyHighlighterBridge() override = default;

    QQuickTextDocument* document() const { return m_quickDocument; }
    void setDocument(QQuickTextDocument* doc);

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& path);

signals:
    void documentChanged();
    void filePathChanged();

private:
    void applyMode();

    QQuickTextDocument* m_quickDocument = nullptr;
    QString m_filePath;
    std::unique_ptr<ruby::filerift::FileRiftHighlighter> m_highlighter;
};

class RubyCodeBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath NOTIFY fileChanged)
    Q_PROPERTY(QString fileContent READ fileContent NOTIFY fileChanged)
    Q_PROPERTY(QString formatName READ formatName NOTIFY fileChanged)
    Q_PROPERTY(bool isDirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(bool isFileriftTranscoded READ isFileriftTranscoded NOTIFY fileChanged)
    Q_PROPERTY(int diagnosticCount READ diagnosticCount NOTIFY diagnosticsChanged)
    Q_PROPERTY(QObject* diagnosticsModel READ diagnosticsModel CONSTANT)

public:
    explicit RubyCodeBridge(QObject* parent = nullptr);

    QString filePath() const { return m_path; }
    QString fileContent() const { return m_content; }
    QString formatName() const { return m_formatName; }
    bool isDirty() const { return m_isDirty; }
    bool isLoading() const { return m_isLoading; }
    bool isFileriftTranscoded() const { return m_isFileriftTranscoded; }
    int diagnosticCount() const;
    QObject* diagnosticsModel() { return &m_diagnosticsModel; }

    Q_INVOKABLE bool loadFile(const QString& path);
    Q_INVOKABLE bool saveFile(const QString& newContent);
    Q_INVOKABLE void analyzeText(const QString& text);
    Q_INVOKABLE void markDirty();
    Q_INVOKABLE void clearDirty();

signals:
    void fileChanged();
    void dirtyChanged();
    void loadingChanged();
    void diagnosticsChanged();
    void statusMessage(const QString& message);

private:
    QString m_path;
    QString m_content;
    QString m_formatName;
    bool m_isDirty = false;
    bool m_isLoading = false;
    bool m_isFileriftTranscoded = false;
    DiagnosticsListModel m_diagnosticsModel;
};

} // namespace ruby::android

