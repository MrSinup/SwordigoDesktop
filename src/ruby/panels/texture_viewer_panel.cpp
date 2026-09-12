#include "texture_viewer_panel.h"

#include "platform/pvr_loader.h"
#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QSpinBox>
#include <QToolButton>
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
    m_export_btn = new QPushButton("Export PNG", this);
    // Export action is context-sensitive: PVR/TEX files → decode to PNG; PNG/JPEG → encode to PVR
    connect(m_export_btn, &QPushButton::clicked, this, [this]() {
        if (m_is_pvr) export_png(); else export_pvr();
    });
    controls->addWidget(m_zoom); controls->addStretch(); controls->addWidget(rotate_l); controls->addWidget(rotate_r); controls->addWidget(flip_h); controls->addWidget(flip_v); controls->addWidget(fill); controls->addWidget(undo); controls->addWidget(m_export_btn); layout->addLayout(controls);

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
    m_is_pvr = is_custom;
    m_metadata->setText(QString("%1  •  %2 × %3  •  RGBA8888 preview  •  %4 KiB")
        .arg(QFileInfo(path).fileName()).arg(m_original.width()).arg(m_original.height())
        .arg(QFileInfo(path).size() / 1024));
    // Update export button: PVR/TEX files get "Export PNG"; PNG/JPEG get "Export PVR"
    if (m_export_btn)
        m_export_btn->setText(m_is_pvr ? "Export PNG" : "Export PVR");
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

void TextureViewerPanel::export_pvr() {
    if (m_original.isNull()) return;

    const int src_w = m_original.width();
    const int src_h = m_original.height();

    // ── Resolution picker dialog ──────────────────────────────────────────────
    QDialog dlg(this);
    dlg.setWindowTitle("Export as PVR — Resolution");
    dlg.setFixedWidth(320);
    auto* dlg_layout = new QVBoxLayout(&dlg);

    // Info label
    dlg_layout->addWidget(new QLabel(
        QString("Source: <b>%1 × %2</b>").arg(src_w).arg(src_h), &dlg));

    // Spinboxes row
    auto* spin_row = new QHBoxLayout();
    auto* w_spin = new QSpinBox(&dlg);
    auto* h_spin = new QSpinBox(&dlg);
    auto* lock_btn = new QToolButton(&dlg);
    lock_btn->setText("🔒");
    lock_btn->setCheckable(true);
    lock_btn->setChecked(true);
    lock_btn->setToolTip("Lock aspect ratio");
    for (auto* s : {w_spin, h_spin}) { s->setRange(1, 16384); s->setSuffix(" px"); }
    w_spin->setValue(src_w);
    h_spin->setValue(src_h);
    spin_row->addWidget(new QLabel("W:", &dlg));
    spin_row->addWidget(w_spin);
    spin_row->addWidget(lock_btn);
    spin_row->addWidget(new QLabel("H:", &dlg));
    spin_row->addWidget(h_spin);
    dlg_layout->addLayout(spin_row);

    // Aspect-ratio lock logic
    bool updating = false;
    QObject::connect(w_spin, &QSpinBox::valueChanged, [&](int val) {
        if (!lock_btn->isChecked() || updating) return;
        updating = true;
        h_spin->setValue(qMax(1, static_cast<int>(std::round(val * static_cast<double>(src_h) / src_w))));
        updating = false;
    });
    QObject::connect(h_spin, &QSpinBox::valueChanged, [&](int val) {
        if (!lock_btn->isChecked() || updating) return;
        updating = true;
        w_spin->setValue(qMax(1, static_cast<int>(std::round(val * static_cast<double>(src_w) / src_h))));
        updating = false;
    });

    // Preset buttons row
    auto* preset_row = new QHBoxLayout();
    struct Preset { const char* label; int num, den; };
    for (auto [lbl, num, den] : std::initializer_list<Preset>{
            {"Original", 1, 1}, {"½", 1, 2}, {"¼", 1, 4}, {"2×", 2, 1}}) {
        auto* btn = new QPushButton(lbl, &dlg);
        btn->setFixedHeight(24);
        const int pn = num, pd = den;
        QObject::connect(btn, &QPushButton::clicked, [&, pn, pd]() {
            updating = true;
            w_spin->setValue(qMax(1, src_w * pn / pd));
            h_spin->setValue(qMax(1, src_h * pn / pd));
            updating = false;
        });
        preset_row->addWidget(btn);
    }
    dlg_layout->addLayout(preset_row);

    // OK / Cancel
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg_layout->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    const int out_w = w_spin->value();
    const int out_h = h_spin->value();

    // ── File picker ───────────────────────────────────────────────────────────
    QFileInfo fi(m_path);
    QString suggested = fi.dir().filePath(fi.completeBaseName() + ".pvr");
    const QString target = QFileDialog::getSaveFileName(this, "Export as PVR (RGBA8888)", suggested, "PVR texture (*.pvr)");
    if (target.isEmpty()) return;

    // Scale if needed (smooth bilinear)
    const QImage img = (out_w != src_w || out_h != src_h)
        ? m_original.scaled(out_w, out_h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .convertToFormat(QImage::Format_RGBA8888)
        : m_original.convertToFormat(QImage::Format_RGBA8888);

    // ── PVR v2 header — RGBA8888 uncompressed (flags 0x12, bpp 32) ───────────
    // Same layout as batch_converter.cpp make_v2_pvr_hdr for RGBA8888 round-trips.
    const uint32_t data_size = static_cast<uint32_t>(out_w * out_h * 4);

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

    PVRv2Hdr hdr;
    hdr.height    = static_cast<uint32_t>(out_h);
    hdr.width     = static_cast<uint32_t>(out_w);
    hdr.flags     = 0x12;
    hdr.data_size = data_size;

    QFile out(target);
    if (!out.open(QIODevice::WriteOnly)) return;
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    out.write(reinterpret_cast<const char*>(img.constBits()), static_cast<qint64>(data_size));
    out.close();
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
