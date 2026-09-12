#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QFrame>

namespace ruby::viewport {

class SceneLoadingOverlay : public QWidget {
    Q_OBJECT

public:
    explicit SceneLoadingOverlay(QWidget* parent = nullptr);
    ~SceneLoadingOverlay() override = default;

    void show_loading(const QString& title = "Loading Scene", const QString& stage = "Preparing...");
    void set_stage(const QString& stage);
    void hide_loading();

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void on_spin_tick();

private:
    QFrame* m_card = nullptr;
    QLabel* m_title_label = nullptr;
    QLabel* m_stage_label = nullptr;
    QProgressBar* m_progress = nullptr;
    QTimer* m_spin_timer = nullptr;
    float m_spinner_angle = 0.0f;
};

} // namespace ruby::viewport
