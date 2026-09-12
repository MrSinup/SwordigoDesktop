#pragma once
// ============================================================================
// audio_viewer_panel.h — Audio asset viewer (WAV / MP3 / OGG)
// ============================================================================
// Inline audio player for the Ruby GG asset browser, ported from the ImGui
// asset viewer's audio module (exptsrc/tools/av_audio.*). Every decoder runs
// through a common float32-PCM pipeline:
//
//   WAV  → SDL_LoadWAV + SDL_ConvertAudioSamples   (SDL3, already linked)
//   MP3  → libmpg123 decode loop to float32        (already a project dep)
//   OGG  → libvorbisfile ov_read_float             (already a project dep)
//
// The waveform is pre-computed on load (~500 peak samples, mixed to mono,
// normalized), playback uses an SDL audio-device stream fed in small chunks
// from a QTimer tick (position = bytes fed − bytes queued, exactly like the
// ImGui edition), and the widget paints the waveform itself with a playback
// cursor — clicking the waveform seeks.
//
// No new runtime dependencies: SDL3 / mpg123 / vorbisfile are all already
// required by the project's other binaries.
// ============================================================================

#include <QWidget>
#include <QString>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QSlider;
class QTimer;

// SDL3 typedefs this struct globally; we only hold a pointer to it here so a
// forward declaration (global scope, visible to moc) is enough — the real
// definition comes from <SDL3/SDL.h> in the .cpp.
struct SDL_AudioStream;

namespace ruby::panels {

class AudioViewerPanel final : public QWidget {
    Q_OBJECT

public:
    explicit AudioViewerPanel(QWidget* parent = nullptr);
    ~AudioViewerPanel() override;

    // Start decoding `path` on a worker thread (charter rule 1: file loads
    // are asynchronous). The panel's previous audio is stopped and cleared
    // immediately; when the decode lands, `loadFinished` is emitted.
    // Returns false only when the path itself is unusable (empty / missing).
    bool load_audio(const QString& path);

    // Stop playback and reset position without unloading the decoded audio.
    void stop();

    // Synchronous decode for tests / tooling: blocks until the file is
    // decoded or failed. Returns true when audio is ready to play.
    bool load_audio_sync(const QString& path);

    struct Info {
        bool loaded = false;
        int sample_rate = 0;
        int channels = 0;
        int bits_per_sample = 0;
        double duration = 0.0;      // seconds
        std::string format;         // "WAV PCM", "MP3", "OGG Vorbis"
        std::string filename;
    };
    const Info& info() const { return m_info; }
    const std::vector<float>& waveform() const { return m_waveform; }

signals:
    // Emitted when an async decode finishes. ok=false with detail on failure.
    void loadFinished(const QString& path, bool ok, const QString& detail);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void on_tick();          // QTimer: feed stream + refresh position UI
    void on_play_pause();
    void on_stop_clicked();
    void on_seek_slider(int value_ms);
    void on_volume_slider(int percent);

private:
    struct DecodedAudio {
        bool ok = false;
        std::vector<float> pcm;   // float32 interleaved
        int sample_rate = 0;
        int channels = 0;
        int bits_per_sample = 0;
        double duration = 0.0;
        std::string format;
        std::string error;
    };
    static DecodedAudio decode_file(const std::string& path);

    void apply_loaded(DecodedAudio&& audio);
    void clear_audio();
    void rebuild_waveform();
    bool open_stream();
    void close_stream();
    void feed_audio();
    double position_seconds() const;
    void seek_seconds(double seconds);
    void refresh_ui();
    void refresh_metadata_label();
    void refresh_time_label();
    void refresh_play_button();
    bool seek_from_wave_click(const QPoint& pos);

    // Decoded state (valid on the UI thread only).
    std::vector<float> m_pcm;
    int m_sample_rate = 0;
    int m_channels = 0;
    double m_duration = 0.0;
    std::vector<float> m_waveform;
    Info m_info;
    QString m_path;

    // SDL playback state (UI thread only).
    SDL_AudioStream* m_stream = nullptr;
    int m_cursor_bytes = 0;      // byte offset into m_pcm fed so far
    long long m_fed_bytes = 0;   // bytes pushed since last seek
    double m_gain = 0.8;         // stream gain (0..1); m_volume is the slider
    bool m_playing = false;
    bool m_paused = false;
    bool m_audio_ok = false;     // SDL_Init(SDL_INIT_AUDIO) succeeded

    // UI.
    QLabel* m_meta = nullptr;
    QLabel* m_time_label = nullptr;
    QPushButton* m_play_btn = nullptr;
    QSlider* m_seek = nullptr;
    QSlider* m_volume = nullptr;
    QTimer* m_timer = nullptr;
    bool m_syncing = false;      // programmatic slider updates
    bool m_seek_dragging = false;
    bool m_wave_dragging = false;

    // Async-load bookkeeping.
    int m_load_gen = 0;          // invalidates superseded decode threads
    QString m_loading_path;      // path currently decoding (dedupes re-loads)
};

} // namespace ruby::panels