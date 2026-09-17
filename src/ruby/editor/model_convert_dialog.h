#pragma once
// ============================================================================
// model_convert_dialog.h — 3D Model (GLTF / GLB / FBX / OBJ) to Game POD Converter
//   Native Qt6 port of Ruby's ImGui conversion pipeline (asset_viewer.cpp).
//   Allows setting geometry scale with auto-fit presets against in-game Swordigo
//   reference heights, flipping UVs, and compressing diffuse textures into
//   game-ready PowerVR ETC1 .pvr files at multiple resolutions.
// ============================================================================

#include <QDialog>
#include <QString>
#include <vector>
#include <string>
#include "tools/gltf_glb.h"

class QLineEdit;
class QPushButton;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QGroupBox;
class QScrollArea;
class QFrame;

namespace ruby::editor {

class ModelConvertDialog : public QDialog {
    Q_OBJECT

public:
    explicit ModelConvertDialog(QWidget* parent = nullptr, const QString& initial_source_path = QString());
    ~ModelConvertDialog() override = default;

    /// Set or change the source model path and recalculate bounds.
    void set_source_path(const QString& path);

    /// Target .POD path chosen by user.
    QString output_pod_path() const;

signals:
    /// Fired when conversion finishes successfully.
    void conversionSucceeded(const QString& pod_path);

private slots:
    void onBrowseSource();
    void onBrowseOutput();
    void onSourcePathEdited(const QString& text);
    void onScaleChanged(double val);
    void onTexConvertToggled(bool checked);
    void onPvrResChanged(int index);
    void onAnimSourceChanged(int index);
    void onAnimFpsChanged(double val);
    void onConvertClicked();
    void onOpenConvertedClicked();

private:
    void setup_ui();
    void analyze_source_model(const QString& file_path);
    void update_resulting_bounds();
    void update_anim_ui_and_list();
    void set_working(bool working);

    // Path widgets
    QLineEdit*      m_source_edit       = nullptr;
    QPushButton*    m_source_browse_btn = nullptr;
    QLineEdit*      m_output_edit       = nullptr;
    QPushButton*    m_output_browse_btn = nullptr;

    // Geometry inspection labels
    QLabel*         m_dim_label         = nullptr;
    QLabel*         m_bounds_label      = nullptr;

    // Scale controls
    QDoubleSpinBox* m_scale_spin        = nullptr;
    QPushButton*    m_btn_raw           = nullptr;
    QPushButton*    m_btn_m_to_ft       = nullptr;
    QPushButton*    m_btn_sketchfab     = nullptr;
    QPushButton*    m_btn_cm_to_m       = nullptr;

    // Dynamic auto-fit buttons
    QPushButton*    m_btn_hero          = nullptr;
    QPushButton*    m_btn_prop          = nullptr;
    QPushButton*    m_btn_decor         = nullptr;
    QPushButton*    m_btn_large         = nullptr;

    // Texture & UV controls
    QCheckBox*      m_flip_v_check         = nullptr;
    QCheckBox*      m_convert_tex_check    = nullptr;
    QCheckBox*      m_filter_normals_check = nullptr;
    QCheckBox*      m_smart_naming_check   = nullptr;
    QLabel*         m_textures_info_label  = nullptr;
    QWidget*        m_pvr_res_container    = nullptr;
    QComboBox*      m_pvr_res_combo        = nullptr;
    QLabel*         m_pvr_mode_badge       = nullptr;

    // Animation controls & routing
    QGroupBox*      m_grp_anims            = nullptr;
    QFrame*         m_anim_warning_frame   = nullptr;
    QLabel*         m_anim_warning_label   = nullptr;
    QComboBox*      m_anim_source_combo    = nullptr;
    QLabel*         m_anim_route_info_label= nullptr;
    QLabel*         m_anim_list_label      = nullptr;
    QScrollArea*    m_anim_scroll_area     = nullptr;

    // Advanced / Compatibility
    QCheckBox*      m_rigid_skin_check  = nullptr;
    QDoubleSpinBox* m_anim_fps_spin     = nullptr;
    QCheckBox*      m_overwrite_check   = nullptr;

    // Action & Status
    QProgressBar*   m_progress_bar      = nullptr;
    QLabel*         m_status_label      = nullptr;
    QPushButton*    m_convert_btn       = nullptr;
    QPushButton*    m_cancel_btn        = nullptr;
    QPushButton*    m_open_btn          = nullptr;

    // Measured source model dimensions
    float           m_cur_w             = 0.0f;
    float           m_cur_h             = 0.0f;
    float           m_cur_d             = 0.0f;
    int             m_mesh_count        = 0;
    int             m_vert_count        = 0;

    // Detected animation clips
    std::string     m_companion_motions_path;
    std::vector<av::AnimationClipSummary> m_in_glb_clips;
    std::vector<av::AnimationClipSummary> m_json_clips;

    // Last written output
    QString         m_last_written_pod;
};

} // namespace ruby::editor
