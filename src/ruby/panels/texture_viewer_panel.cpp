#include "texture_viewer_panel.h"

#include "platform/pvr_loader.h"
#include <QButtonGroup>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

namespace ruby::panels {

TextureViewerPanel::TextureViewerPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    m_metadata = new QLabel("Open a PVR, TEX, PNG, or JPEG texture", this);
    m_metadata->setWordWrap(true);
    layout->addWidget(m_metadata);

    auto* controls = new QHBoxLayout();
    auto* channels = new QButtonGroup(this);
    const QStringList labels = {"RGBA", "Alpha", "Red", "Green", "Blue"};
    for (int i = 0; i < labels.size(); ++i) {
        auto* button = new QRadioButton(labels[i], this); button->setChecked(i == 0);
        channels->addButton(button, i); controls->addWidget(button);
    }
    connect(channels, &QButtonGroup::idClicked, this, &TextureViewerPanel::set_channel);
    controls->addSpacing(12); controls->addWidget(new QLabel("Zoom", this));
    m_zoom = new QSlider(Qt::Horizontal, this); m_zoom->setRange(25, 800); m_zoom->setValue(100);
    m_zoom->setFixedWidth(150); connect(m_zoom, &QSlider::valueChanged, this, &TextureViewerPanel::set_zoom);
    auto* rotate_l = new QPushButton("Rotate L", this); connect(rotate_l, &QPushButton::clicked, this, &TextureViewerPanel::rotate_left);
    auto* rotate_r = new QPushButton("Rotate R", this); connect(rotate_r, &QPushButton::clicked, this, &TextureViewerPanel::rotate_right);
    auto* flip_h = new QPushButton("Flip H", this); connect(flip_h, &QPushButton::clicked, this, &TextureViewerPanel::flip_horizontal);
    auto* flip_v = new QPushButton("Flip V", this); connect(flip_v, &QPushButton::clicked, this, &TextureViewerPanel::flip_vertical);
    auto* fill = new QPushButton("Fill", this); connect(fill, &QPushButton::clicked, this, &TextureViewerPanel::fill_transparent);
    auto* undo = new QPushButton("Undo", this); connect(undo, &QPushButton::clicked, this, &TextureViewerPanel::undo);
    auto* export_button = new QPushButton("Export PNG", this); connect(export_button, &QPushButton::clicked, this, &TextureViewerPanel::export_png);
    controls->addWidget(m_zoom); controls->addStretch(); controls->addWidget(rotate_l); controls->addWidget(rotate_r); controls->addWidget(flip_h); controls->addWidget(flip_v); controls->addWidget(fill); controls->addWidget(undo); controls->addWidget(export_button); layout->addLayout(controls);

    m_preview = new QLabel(this); m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet("QLabel { background-color: #282c34; border: 1px solid #323742; }");
    m_scroll = new QScrollArea(this); m_scroll->setWidget(m_preview); m_scroll->setWidgetResizable(true);
    layout->addWidget(m_scroll, 1);
}

bool TextureViewerPanel::load_texture(const QString& path) {
    if (m_path == path && !m_original.isNull()) return true;
    QImage image;
    QFile input(path);
    const QString low = path.toLower();
    const bool is_custom = low.endsWith(".pvr") || low.endsWith(".tex") || low.endsWith(".tex.png");
    if (is_custom) {
        if (input.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = input.readAll(); std::vector<uint8_t> rgba; int width = 0, height = 0;
            if (pvr_decode_to_rgba(reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size()), rgba, width, height)) {
                image = QImage(rgba.data(), width, height, QImage::Format_RGBA8888).copy();
            }
            input.close();
        }
    }
    if (image.isNull()) {
        image.load(path);
    }
    if (image.isNull()) {
        if (input.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = input.readAll(); std::vector<uint8_t> rgba; int width = 0, height = 0;
            if (pvr_decode_to_rgba(reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size()), rgba, width, height)) {
                image = QImage(rgba.data(), width, height, QImage::Format_RGBA8888).copy();
            }
            input.close();
        }
    }
    if (image.isNull()) return false;
    m_original = image.convertToFormat(QImage::Format_RGBA8888); m_undo.clear(); m_path = path;
    m_metadata->setText(QString("%1  •  %2 × %3  •  RGBA8888 preview  •  %4 KiB")
        .arg(QFileInfo(path).fileName()).arg(m_original.width()).arg(m_original.height())
        .arg(QFileInfo(path).size() / 1024));
    refresh_preview(); return true;
}

void TextureViewerPanel::set_zoom(int) { refresh_preview(); }
void TextureViewerPanel::set_channel(int channel) { m_channel = channel; refresh_preview(); }
void TextureViewerPanel::refresh_preview() {
    if (m_original.isNull()) return;
    QImage display = m_original.copy();
    if (m_channel) for (int y = 0; y < display.height(); ++y) for (int x = 0; x < display.width(); ++x) {
        const QColor p = display.pixelColor(x, y); const int value = m_channel == 1 ? p.alpha() : (m_channel == 2 ? p.red() : (m_channel == 3 ? p.green() : p.blue()));
        display.setPixelColor(x, y, m_channel == 1 ? QColor(value, value, value, 255) : QColor(value, value, value, 255));
    }
    const QSize target = display.size() * m_zoom->value() / 100;
    m_preview->setPixmap(QPixmap::fromImage(display.scaled(target, Qt::KeepAspectRatio, Qt::FastTransformation)));
    m_preview->resize(target);
}
void TextureViewerPanel::export_png() {
    if (m_original.isNull()) return;
    const QString target = QFileDialog::getSaveFileName(this, "Export decoded texture", m_path + ".png", "PNG image (*.png)");
    if (!target.isEmpty()) m_original.save(target, "PNG");
}

void TextureViewerPanel::rotate_left() {
    if (m_original.isNull()) return; m_undo.push_back(m_original); QTransform t; t.rotate(-90); m_original = m_original.transformed(t); refresh_preview();
}
void TextureViewerPanel::rotate_right() {
    if (m_original.isNull()) return; m_undo.push_back(m_original); QTransform t; t.rotate(90); m_original = m_original.transformed(t); refresh_preview();
}
void TextureViewerPanel::flip_horizontal() {
    if (m_original.isNull()) return; m_undo.push_back(m_original); m_original = m_original.flipped(Qt::Horizontal); refresh_preview();
}
void TextureViewerPanel::flip_vertical() {
    if (m_original.isNull()) return; m_undo.push_back(m_original); m_original = m_original.flipped(Qt::Vertical); refresh_preview();
}
void TextureViewerPanel::fill_transparent() {
    if (m_original.isNull()) return; m_undo.push_back(m_original); m_original.fill(Qt::transparent); refresh_preview();
}
void TextureViewerPanel::undo() {
    if (m_undo.isEmpty()) return; m_original = m_undo.takeLast(); refresh_preview();
}

} // namespace ruby::panels
