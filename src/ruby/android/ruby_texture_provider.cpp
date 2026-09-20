#include "ruby_texture_provider.h"
#include "platform/pvr_loader.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QUrlQuery>
#include <QTransform>
#include <vector>
#include <cstdint>

namespace ruby::android {

#pragma pack(push, 1)
struct PVRv2Hdr {
    uint32_t header_size  = 52;
    uint32_t height;
    uint32_t width;
    uint32_t mip_count    = 1;
    uint32_t flags;        // 0x12 = RGBA8888 uncompressed
    uint32_t data_size;
    uint32_t bpp          = 32;
    uint32_t mask_r       = 0;
    uint32_t mask_g       = 0;
    uint32_t mask_b       = 0;
    uint32_t mask_a       = 0;
    uint32_t magic        = 0x21525650; // "PVR!"
    uint32_t num_surfaces = 1;
};
#pragma pack(pop)

RubyTextureProvider::RubyTextureProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

bool RubyTextureProvider::decodeImage(const QString& path, QImage& outImage, bool& outIsPvr) {
    QFile input(path);
    const QString low = path.toLower();
    outIsPvr = low.endsWith(QStringLiteral(".pvr")) || low.endsWith(QStringLiteral(".tex")) || low.endsWith(QStringLiteral(".tex.png"));

    if (outIsPvr) {
        if (input.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = input.readAll();
            std::vector<uint8_t> rgba;
            int width = 0, height = 0;
            if (pvr_decode_to_rgba(reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size()), rgba, width, height)) {
                outImage = QImage(rgba.data(), width, height, QImage::Format_RGBA8888).copy();
            }
            input.close();
        }
    }

    if (outImage.isNull()) {
        outImage.load(path);
    }

    if (outImage.isNull() && !outIsPvr) {
        // Retry pvr decoding just in case extension was omitted
        if (input.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = input.readAll();
            std::vector<uint8_t> rgba;
            int width = 0, height = 0;
            if (pvr_decode_to_rgba(reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size()), rgba, width, height)) {
                outImage = QImage(rgba.data(), width, height, QImage::Format_RGBA8888).copy();
                outIsPvr = true;
            }
            input.close();
        }
    }

    return !outImage.isNull();
}

void RubyTextureProvider::applyChannelFilter(QImage& img, int channel) {
    if (channel <= 0 || img.isNull()) return;
    img = img.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < img.height(); ++y) {
        auto* scanline = reinterpret_cast<uint32_t*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            uint32_t pixel = scanline[x];
            // Format_RGBA8888 in memory: byte 0=R, 1=G, 2=B, 3=A
            uint8_t r = pixel & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = (pixel >> 16) & 0xFF;
            uint8_t a = (pixel >> 24) & 0xFF;

            uint8_t v = 0;
            if (channel == 1) v = a;      // Alpha
            else if (channel == 2) v = r; // Red
            else if (channel == 3) v = g; // Green
            else if (channel == 4) v = b; // Blue

            scanline[x] = (0xFF000000) | (v << 16) | (v << 8) | v;
        }
    }
}

QImage RubyTextureProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    QString cleanPath = id;
    int ch = 0;
    int qidx = cleanPath.indexOf('?');
    if (qidx != -1) {
        QUrlQuery query(cleanPath.mid(qidx + 1));
        ch = query.queryItemValue(QStringLiteral("ch")).toInt();
        cleanPath = cleanPath.left(qidx);
    }

    QImage img;
    bool isPvr = false;

    // Check if image is currently loaded in active texture bridge (supports real-time rotate/flip/undo)
    if (m_bridge && m_bridge->filePath() == cleanPath && !m_bridge->currentImage().isNull()) {
        img = m_bridge->currentImage();
        isPvr = m_bridge->isPvr();
    } else {
        if (!decodeImage(cleanPath, img, isPvr)) {
            return QImage();
        }
    }

    if (ch > 0) {
        applyChannelFilter(img, ch);
    }

    if (size) {
        *size = img.size();
    }

    if (requestedSize.isValid() && requestedSize != img.size()) {
        img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return img;
}


// ─── RubyTextureBridge Implementation ──────────────────────────────────────

RubyTextureBridge::RubyTextureBridge(QObject* parent)
    : QObject(parent)
{
}

QString RubyTextureBridge::fileName() const {
    return QFileInfo(m_path).fileName();
}

bool RubyTextureBridge::loadTexture(const QString& path) {
    QImage img;
    bool isPvr = false;
    if (!RubyTextureProvider::decodeImage(path, img, isPvr)) {
        return false;
    }

    m_path = path;
    m_original = img.convertToFormat(QImage::Format_RGBA8888);
    m_isPvr = isPvr;
    m_channel = 0;
    m_undoStack.clear();
    m_fileSizeKb = static_cast<int>(QFileInfo(path).size() / 1024);

    emit textureChanged();
    emit channelChanged();
    return true;
}

void RubyTextureBridge::setChannel(int ch) {
    if (m_channel != ch) {
        m_channel = ch;
        emit channelChanged();
    }
}

void RubyTextureBridge::rotateLeft() {
    if (m_original.isNull()) return;
    m_undoStack.push_back(m_original);
    QTransform t;
    t.rotate(-90);
    m_original = m_original.transformed(t);
    emit textureChanged();
}

void RubyTextureBridge::rotateRight() {
    if (m_original.isNull()) return;
    m_undoStack.push_back(m_original);
    QTransform t;
    t.rotate(90);
    m_original = m_original.transformed(t);
    emit textureChanged();
}

void RubyTextureBridge::flipHorizontal() {
    if (m_original.isNull()) return;
    m_undoStack.push_back(m_original);
    m_original = m_original.mirrored(true, false);
    emit textureChanged();
}

void RubyTextureBridge::flipVertical() {
    if (m_original.isNull()) return;
    m_undoStack.push_back(m_original);
    m_original = m_original.mirrored(false, true);
    emit textureChanged();
}

void RubyTextureBridge::undo() {
    if (m_undoStack.isEmpty()) return;
    m_original = m_undoStack.takeLast();
    emit textureChanged();
}

bool RubyTextureBridge::exportPng(const QString& destPath) {
    if (m_original.isNull()) return false;
    QString target = destPath;
    if (target.isEmpty()) {
        target = m_path + QStringLiteral(".png");
    }
    return m_original.save(target, "PNG");
}

bool RubyTextureBridge::exportPvr(int outW, int outH, const QString& destPath) {
    if (m_original.isNull()) return false;
    if (outW <= 0) outW = m_original.width();
    if (outH <= 0) outH = m_original.height();

    QString target = destPath;
    if (target.isEmpty()) {
        QFileInfo fi(m_path);
        target = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".pvr"));
    }

    QImage img = (outW != m_original.width() || outH != m_original.height())
        ? m_original.scaled(outW, outH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888)
        : m_original.convertToFormat(QImage::Format_RGBA8888);

    const uint32_t data_size = static_cast<uint32_t>(outW * outH * 4);
    PVRv2Hdr hdr;
    hdr.height    = static_cast<uint32_t>(outH);
    hdr.width     = static_cast<uint32_t>(outW);
    hdr.flags     = 0x12; // RGBA8888 uncompressed
    hdr.data_size = data_size;

    QFile out(target);
    if (!out.open(QIODevice::WriteOnly)) return false;
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    out.write(reinterpret_cast<const char*>(img.constBits()), static_cast<qint64>(data_size));
    out.close();
    return true;
}

} // namespace ruby::android
