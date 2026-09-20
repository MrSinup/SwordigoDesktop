#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace ruby::android {

class RubyToolsBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastStatusMessage READ lastStatusMessage NOTIFY statusChanged)

public:
    explicit RubyToolsBridge(QObject* parent = nullptr);

    QString lastStatusMessage() const { return m_lastStatus; }

    Q_INVOKABLE bool convertModel(const QString& sourcePath, const QString& destPath, double scale, bool flipUv);
    Q_INVOKABLE QVariantMap inspectModel(const QString& filePath);
    Q_INVOKABLE void convertModelAdvanced(const QVariantMap& options);
    Q_INVOKABLE bool generateGroundMesh(const QString& rbmPath);
    Q_INVOKABLE bool batchConvertTextures(const QString& folderPath, const QString& targetFormat);
    Q_INVOKABLE bool exportSwdm(const QString& filePath, const QVariantList& polygonPoints,
                                double minDepth, double maxDepth,
                                const QString& topTexture, const QString& frontTexture,
                                double surfaceWidth, double z);

    // Documentation & Modding Guides
    Q_INVOKABLE QString loadDocMarkdown(const QString& docId);
    Q_INVOKABLE QVariantList getModdingGuides();

signals:
    void statusChanged();
    void conversionStarted();
    void conversionFinished(bool success, const QString& outputPath, const QString& errorMessage);

private:
    QString m_lastStatus;
};

} // namespace ruby::android
