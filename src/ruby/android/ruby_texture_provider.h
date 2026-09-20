#pragma once

#include <QQuickImageProvider>
#include <QObject>
#include <QImage>
#include <QVector>
#include <QString>

namespace ruby::android {

class RubyTextureBridge;

class RubyTextureProvider : public QQuickImageProvider {
public:
    RubyTextureProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    static bool decodeImage(const QString& path, QImage& outImage, bool& outIsPvr);
    static void applyChannelFilter(QImage& img, int channel);

    void setBridge(RubyTextureBridge* bridge) { m_bridge = bridge; }

private:
    RubyTextureBridge* m_bridge = nullptr;
};

class RubyTextureBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath NOTIFY textureChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY textureChanged)
    Q_PROPERTY(int width READ width NOTIFY textureChanged)
    Q_PROPERTY(int height READ height NOTIFY textureChanged)
    Q_PROPERTY(int fileSizeKb READ fileSizeKb NOTIFY textureChanged)
    Q_PROPERTY(bool isPvr READ isPvr NOTIFY textureChanged)
    Q_PROPERTY(int currentChannel READ currentChannel NOTIFY channelChanged)

public:
    explicit RubyTextureBridge(QObject* parent = nullptr);

    QString filePath() const { return m_path; }
    QString fileName() const;
    int width() const { return m_original.width(); }
    int height() const { return m_original.height(); }
    int fileSizeKb() const { return m_fileSizeKb; }
    bool isPvr() const { return m_isPvr; }
    int currentChannel() const { return m_channel; }
    const QImage& currentImage() const { return m_original; }


    Q_INVOKABLE bool loadTexture(const QString& path);
    Q_INVOKABLE void setChannel(int ch);
    Q_INVOKABLE void rotateLeft();
    Q_INVOKABLE void rotateRight();
    Q_INVOKABLE void flipHorizontal();
    Q_INVOKABLE void flipVertical();
    Q_INVOKABLE void undo();
    Q_INVOKABLE bool exportPng(const QString& destPath = QString());
    Q_INVOKABLE bool exportPvr(int outW, int outH, const QString& destPath = QString());

signals:
    void textureChanged();
    void channelChanged();

private:
    QString m_path;
    QImage m_original;
    QVector<QImage> m_undoStack;
    int m_fileSizeKb = 0;
    bool m_isPvr = false;
    int m_channel = 0;
};

} // namespace ruby::android
