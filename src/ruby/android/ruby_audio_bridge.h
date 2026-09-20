#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QTimer>

namespace ruby::android {

class RubyAudioBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY filePathChanged)
    Q_PROPERTY(QString fileSizeStr READ fileSizeStr NOTIFY filePathChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY fileLoaded)
    Q_PROPERTY(int positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playStateChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY fileLoaded)
    Q_PROPERTY(int channels READ channels NOTIFY fileLoaded)
    Q_PROPERTY(int bitsPerSample READ bitsPerSample NOTIFY fileLoaded)
    Q_PROPERTY(QString formatName READ formatName NOTIFY fileLoaded)
    Q_PROPERTY(QVariantList waveform READ waveform NOTIFY waveformChanged)

public:
    explicit RubyAudioBridge(QObject* parent = nullptr);
    ~RubyAudioBridge() override;

    QString filePath() const { return m_filePath; }
    QString fileName() const { return m_fileName; }
    QString fileSizeStr() const { return m_fileSizeStr; }
    int durationMs() const { return m_durationMs; }
    int positionMs() const { return m_positionMs; }
    bool isPlaying() const { return m_isPlaying; }
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }
    int bitsPerSample() const { return m_bitsPerSample; }
    QString formatName() const { return m_formatName; }
    QVariantList waveform() const { return m_waveform; }

    Q_INVOKABLE bool load(const QString& path);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(int msec);

signals:
    void filePathChanged();
    void fileLoaded();
    void positionChanged();
    void playStateChanged();
    void waveformChanged();
    void errorOccurred(const QString& message);

private slots:
    void onPollTimer();

private:
    void parseWav(const QString& path);
    void generateFallbackWaveform(const QByteArray& data);

    QString m_filePath;
    QString m_fileName;
    QString m_fileSizeStr;
    int m_durationMs = 0;
    int m_positionMs = 0;
    bool m_isPlaying = false;
    int m_sampleRate = 44100;
    int m_channels = 2;
    int m_bitsPerSample = 16;
    QString m_formatName = QStringLiteral("Audio");
    QVariantList m_waveform;

    QTimer* m_pollTimer = nullptr;
};

} // namespace ruby::android
