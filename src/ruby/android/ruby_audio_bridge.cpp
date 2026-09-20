#include "ruby_audio_bridge.h"
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <cmath>
#include <algorithm>

#if defined(Q_OS_ANDROID)
#  include <QCoreApplication>
#  include <QJniObject>
#  include <QtCore/qnativeinterface.h>
#  include <jni.h>
#endif

namespace ruby::android {

RubyAudioBridge::RubyAudioBridge(QObject* parent)
    : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &RubyAudioBridge::onPollTimer);
}

RubyAudioBridge::~RubyAudioBridge() {
    stop();
}

bool RubyAudioBridge::load(const QString& path) {
    stop();
    m_filePath = path;
    emit filePathChanged();

    QFileInfo fi(path);
    if (!fi.exists()) {
        emit errorOccurred(QStringLiteral("File does not exist: %1").arg(path));
        return false;
    }

    m_fileName = fi.fileName();
    qint64 bytes = fi.size();
    if (bytes < 1024) {
        m_fileSizeStr = QString::number(bytes) + QStringLiteral(" B");
    } else if (bytes < 1024 * 1024) {
        m_fileSizeStr = QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    } else {
        m_fileSizeStr = QString::number(bytes / (1024.0 * 1024.0), 'f', 2) + QStringLiteral(" MB");
    }

    QString ext = fi.suffix().toLower();
    if (ext == QStringLiteral("wav")) {
        parseWav(path);
    } else {
        if (ext == QStringLiteral("ogg")) {
            m_formatName = QStringLiteral("OGG Vorbis");
        } else if (ext == QStringLiteral("mp3")) {
            m_formatName = QStringLiteral("MP3 Audio");
        } else {
            m_formatName = QStringLiteral("Audio File");
        }
        m_sampleRate = 44100;
        m_channels = 2;
        m_bitsPerSample = 16;
        m_durationMs = 0;

        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray head = file.read(65536);
            generateFallbackWaveform(head);
            file.close();
        }
    }

    m_positionMs = 0;
    emit fileLoaded();
    emit positionChanged();
    return true;
}

void RubyAudioBridge::parseWav(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(QStringLiteral("Cannot open audio file: %1").arg(path));
        return;
    }

    QByteArray fileData = file.readAll();
    file.close();

    if (fileData.size() < 44) {
        emit errorOccurred(QStringLiteral("Invalid WAV header (file too small)"));
        return;
    }

    const char* raw = fileData.constData();
    if (std::memcmp(raw, "RIFF", 4) != 0 || std::memcmp(raw + 8, "WAVE", 4) != 0) {
        emit errorOccurred(QStringLiteral("Not a valid RIFF/WAVE file"));
        return;
    }

    int offset = 12;
    int dataOffset = -1;
    quint32 dataSize = 0;
    quint16 audioFormat = 1;
    m_channels = 2;
    m_sampleRate = 44100;
    m_bitsPerSample = 16;

    while (offset + 8 <= fileData.size()) {
        char chunkId[5] = {0};
        std::memcpy(chunkId, raw + offset, 4);
        quint32 chunkSize = 0;
        std::memcpy(&chunkSize, raw + offset + 4, 4);
        offset += 8;

        if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            std::memcpy(&audioFormat, raw + offset, 2);
            std::memcpy(&m_channels, raw + offset + 2, 2);
            std::memcpy(&m_sampleRate, raw + offset + 4, 4);
            std::memcpy(&m_bitsPerSample, raw + offset + 14, 2);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataOffset = offset;
            dataSize = std::min<quint32>(chunkSize, fileData.size() - offset);
            break;
        }
        offset += chunkSize;
        if (chunkSize % 2 != 0) offset += 1; // Pad byte
    }

    if (audioFormat == 1) {
        m_formatName = QStringLiteral("WAV PCM");
    } else if (audioFormat == 3) {
        m_formatName = QStringLiteral("WAV IEEE Float");
    } else {
        m_formatName = QStringLiteral("WAV Encoded");
    }

    int bytesPerSec = m_sampleRate * m_channels * (m_bitsPerSample / 8);
    if (bytesPerSec > 0 && dataSize > 0) {
        m_durationMs = static_cast<int>((static_cast<qint64>(dataSize) * 1000) / bytesPerSec);
    } else {
        m_durationMs = 0;
    }

    // Generate 100 waveform peak points
    m_waveform.clear();
    const int numBuckets = 100;
    m_waveform.reserve(numBuckets);

    if (dataOffset >= 0 && dataSize > 0 && m_bitsPerSample == 16) {
        int totalSamples = dataSize / 2;
        int samplesPerBucket = std::max(1, totalSamples / numBuckets);
        const qint16* pcm = reinterpret_cast<const qint16*>(raw + dataOffset);

        float maxGlobal = 0.001f;
        std::vector<float> peaks;
        peaks.reserve(numBuckets);

        for (int b = 0; b < numBuckets; ++b) {
            int startIdx = b * samplesPerBucket;
            int endIdx = std::min(totalSamples, startIdx + samplesPerBucket);
            float maxVal = 0.0f;
            int step = std::max(1, (endIdx - startIdx) / 100);
            for (int s = startIdx; s < endIdx; s += step) {
                float v = std::abs(static_cast<float>(pcm[s])) / 32768.0f;
                if (v > maxVal) maxVal = v;
            }
            if (maxVal > maxGlobal) maxGlobal = maxVal;
            peaks.push_back(maxVal);
        }

        for (float p : peaks) {
            float norm = std::clamp(p / maxGlobal, 0.08f, 1.0f);
            m_waveform.append(norm);
        }
    } else if (dataOffset >= 0 && dataSize > 0 && m_bitsPerSample == 8) {
        int totalSamples = dataSize;
        int samplesPerBucket = std::max(1, totalSamples / numBuckets);
        const quint8* pcm = reinterpret_cast<const quint8*>(raw + dataOffset);

        float maxGlobal = 0.001f;
        std::vector<float> peaks;
        peaks.reserve(numBuckets);

        for (int b = 0; b < numBuckets; ++b) {
            int startIdx = b * samplesPerBucket;
            int endIdx = std::min(totalSamples, startIdx + samplesPerBucket);
            float maxVal = 0.0f;
            int step = std::max(1, (endIdx - startIdx) / 100);
            for (int s = startIdx; s < endIdx; s += step) {
                float v = std::abs(static_cast<float>(pcm[s]) - 128.0f) / 128.0f;
                if (v > maxVal) maxVal = v;
            }
            if (maxVal > maxGlobal) maxGlobal = maxVal;
            peaks.push_back(maxVal);
        }

        for (float p : peaks) {
            float norm = std::clamp(p / maxGlobal, 0.08f, 1.0f);
            m_waveform.append(norm);
        }
    } else {
        generateFallbackWaveform(fileData);
    }

    emit waveformChanged();
}

void RubyAudioBridge::generateFallbackWaveform(const QByteArray& data) {
    m_waveform.clear();
    const int numBuckets = 100;
    m_waveform.reserve(numBuckets);

    quint32 hash = 2166136261u;
    for (int i = 0; i < std::min<int>(data.size(), 1024); ++i) {
        hash = (hash ^ static_cast<quint8>(data.at(i))) * 16777619u;
    }

    for (int i = 0; i < numBuckets; ++i) {
        hash = (hash ^ (i * 31 + 7)) * 16777619u;
        float r = static_cast<float>(hash % 1000) / 1000.0f;
        float env = std::sin(static_cast<float>(i) / numBuckets * 3.14159f);
        float val = std::clamp(0.15f + 0.85f * (r * 0.6f + env * 0.4f), 0.10f, 1.0f);
        m_waveform.append(val);
    }
    emit waveformChanged();
}

void RubyAudioBridge::play() {
    if (m_filePath.isEmpty()) return;

#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        if (!m_isPlaying && m_positionMs > 0 && m_positionMs < m_durationMs) {
            activity.callMethod<void>("audioResume");
        } else {
            activity.callMethod<jboolean>("audioPlay", "(Ljava/lang/String;)Z",
                QJniObject::fromString(m_filePath).object<jstring>());
        }
    }
#endif

    m_isPlaying = true;
    m_pollTimer->start(50);
    emit playStateChanged();
}

void RubyAudioBridge::pause() {
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("audioPause");
    }
#endif

    m_isPlaying = false;
    m_pollTimer->stop();
    emit playStateChanged();
}

void RubyAudioBridge::togglePlay() {
    if (m_isPlaying) {
        pause();
    } else {
        play();
    }
}

void RubyAudioBridge::stop() {
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("audioStop");
    }
#endif

    m_isPlaying = false;
    m_positionMs = 0;
    m_pollTimer->stop();
    emit playStateChanged();
    emit positionChanged();
}

void RubyAudioBridge::seek(int msec) {
    m_positionMs = (m_durationMs > 0) ? std::clamp(msec, 0, m_durationMs) : std::max(0, msec);

#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("audioSeek", "(I)V", static_cast<jint>(m_positionMs));
    }
#endif

    emit positionChanged();
}

void RubyAudioBridge::onPollTimer() {
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        int pos = activity.callMethod<jint>("audioGetPosition");
        bool playing = activity.callMethod<jboolean>("audioIsPlaying");
        int dur = activity.callMethod<jint>("audioGetDuration");

        if (dur > 0 && (m_durationMs <= 0 || std::abs(dur - m_durationMs) > 1000)) {
            m_durationMs = dur;
            emit fileLoaded();
        }

        if (pos != m_positionMs) {
            m_positionMs = pos;
            emit positionChanged();
        }

        if (playing != m_isPlaying) {
            m_isPlaying = playing;
            emit playStateChanged();
            if (!playing && m_durationMs > 0 && m_positionMs >= m_durationMs - 200) {
                m_pollTimer->stop();
            }
        }
    }
#else
    // Desktop simulation
    if (m_isPlaying) {
        m_positionMs += 50;
        if (m_durationMs > 0 && m_positionMs >= m_durationMs) {
            m_positionMs = m_durationMs;
            m_isPlaying = false;
            m_pollTimer->stop();
            emit playStateChanged();
        }
        emit positionChanged();
    }
#endif
}

} // namespace ruby::android
