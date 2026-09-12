#pragma once

#include <QWidget>
#include <QImage>
#include <QVector>

class QLabel;
class QScrollArea;
class QSlider;
class QImage;

namespace ruby::panels {

class TextureViewerPanel final : public QWidget {
    Q_OBJECT
public:
    explicit TextureViewerPanel(QWidget* parent = nullptr);
    bool load_texture(const QString& path);

private slots:
    void set_zoom(int percent);
    void set_channel(int channel);
    void export_png();
    void rotate_left();
    void rotate_right();
    void flip_horizontal();
    void flip_vertical();
    void fill_transparent();
    void undo();

private:
    void refresh_preview();
    QImage m_original;
    QVector<QImage> m_undo;
    QString m_path;
    QLabel* m_metadata = nullptr;
    QLabel* m_preview = nullptr;
    QSlider* m_zoom = nullptr;
    QScrollArea* m_scroll = nullptr;
    int m_channel = 0;
};

} // namespace ruby::panels
