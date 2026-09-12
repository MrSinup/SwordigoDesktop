// ============================================================================
// audio_viewer_panel.cpp — Audio asset viewer (WAV / MP3 / OGG)
// ============================================================================
// Implements the AudioViewerPanel declared in audio_viewer_panel.h. The
// decode + playback pipeline is ported from the ImGui asset viewer's
// av_audio module (exptsrc/tools/av_audio.cpp), generalized so WAV, MP3 and
// OGG all land in a common float32-PCM buffer:
//
//   - WAV: SDL_LoadWAV → SDL_ConvertAudioSamples to float32
//   - MP3: libmpg123 decode loop with MPG123_FLOAT output
//   - OGG: libvorbisfile ov_read_float, interleaved
//
// Playback uses one SDL3 audio-device stream per file fed in small chunks
// (position = bytes fed − bytes queued), a QTimer standing in for the ImGui
// per-frame audio_update(), and the widget paints its own waveform (peaks per
// pixel, mirrored around the center line) with a click-to-seek cursor.
// ============================================================================

#include "audio_viewer_panel.h"

#include <SDL3/SDL.h>
#include <mpg123.h>
#include <vorbis/vorbisfile.h>

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace ruby::panels {

namespace {

QString fmt_time(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    const int total = (int)std::llround(seconds);
    return QString("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}

QString fmt_bytes(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

} // namespace

// ============================================================================
// Decoders (worker thread) — each returns fully decoded float32 PCM
// ============================================================================

AudioViewerPanel::DecodedAudio AudioViewerPanel::decode_file(const std::string& path) {
    const std::string ext = fs::path(path).extension().string();
    std::string low = ext;
    for (auto& c : low) c = (char)tolower((unsigned char)c);

    // Try the extension-matching decoder first, then fall back to the others
    // (a mislabeled file still opens if any decoder recognizes it).
    auto try_wav = [&path]() -> DecodedAudio {
        DecodedAudio r;
        SDL_AudioSpec spec{};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) return r;
        SDL_AudioSpec f32 = spec;
        f32.format = SDL_AUDIO_F32;
        Uint8* out = nullptr;
        int out_len = 0;
        if (!SDL_ConvertAudioSamples(&spec, buf, (int)len, &f32, &out, &out_len)) {
            SDL_free(buf);
            return r;
        }
        r.pcm.assign(reinterpret_cast<float*>(out), reinterpret_cast<float*>(out) + out_len / (int)sizeof(float));
        SDL_free(buf);
        SDL_free(out);
        if (r.pcm.empty() || spec.freq <= 0 || spec.channels <= 0) return r;
        r.sample_rate = spec.freq;
        r.channels = spec.channels;
        r.bits_per_sample = (int)SDL_AUDIO_BITSIZE(spec.format);
        r.duration = (double)(r.pcm.size() / (size_t)r.channels) / (double)spec.freq;
        r.format = "WAV PCM";
        r.ok = true;
        return r;
    };

    auto try_mp3 = [&path]() -> DecodedAudio {
        DecodedAudio r;
        static std::once_flag s_mpg123_once;
        std::call_once(s_mpg123_once, []() { mpg123_init(); });
        int err = 0;
        mpg123_handle* h = mpg123_new(nullptr, &err);
        if (!h) return r;
        if (mpg123_open(h, path.c_str()) != MPG123_OK) { mpg123_delete(h); return r; }
        // The enum-less mpg123 header ships no MPG123_QUIET / MPG123_ADD_FLAGS
        // constants, but their ABI values are stable: param 2 (ADD_FLAGS) with
        // flag 0x80 (QUIET) silences the "Note:" resync spam on non-MP3 files.
        // (param 3 is FORCE_RATE — an easy trap, it resamples the stream!)
        mpg123_param2(h, 2 /* MPG123_ADD_FLAGS */, 0x80 /* MPG123_QUIET */, 0);
        long rate = 0;   // mpg123_getformat reports the rate as long
        int channels = 0, encoding = 0;
        if (mpg123_getformat(h, &rate, &channels, &encoding) != MPG123_OK) {
            mpg123_close(h); mpg123_delete(h); return r;
        }
        // Reject with an error so fallback decoders get a turn.
        if (rate <= 0 || channels <= 0) {
            mpg123_close(h); mpg123_delete(h); return r;
        }
        // mpg123 decodes to signed 16-bit by default (same assumption as the
        // SRE-Music loader in jni_bridge_arm64.cpp) — convert to float32 so
        // every decoder feeds the same pipeline.
        std::vector<unsigned char> buf(1 << 15);
        size_t done = 0;
        int rc = MPG123_OK;
        std::vector<int16_t> s16;
        while ((rc = mpg123_read(h, buf.data(), buf.size(), &done)) == MPG123_OK && done > 0) {
            const size_t n = done / sizeof(int16_t);
            const int16_t* src = reinterpret_cast<const int16_t*>(buf.data());
            s16.insert(s16.end(), src, src + n);
        }
        mpg123_close(h);
        mpg123_delete(h);
        if (s16.empty()) return r;
        r.pcm.reserve(s16.size());
        const float scale = 1.0f / 32768.0f;
        for (int16_t v : s16) r.pcm.push_back((float)v * scale);
        r.sample_rate = (int)rate;
        r.channels = channels;
        r.bits_per_sample = 16;
        r.duration = (double)(r.pcm.size() / (size_t)channels) / (double)rate;
        r.format = "MP3";
        r.ok = true;
        return r;
    };

    auto try_ogg = [&path]() -> DecodedAudio {
        DecodedAudio r;
        OggVorbis_File vf;
        std::memset(&vf, 0, sizeof(vf));
        if (ov_fopen(path.c_str(), &vf) != 0) return r;
        vorbis_info* vi = ov_info(&vf, -1);
        if (!vi || vi->rate <= 0 || vi->channels <= 0) { ov_clear(&vf); return r; }
        const int channels = vi->channels;
        const int rate = vi->rate;
        const double total = ov_time_total(&vf, -1);
        std::vector<float> interleaved;
        const int chunk_frames = 4096;
        float** pcm = nullptr;
        long ret = 0;
        while ((ret = ov_read_float(&vf, &pcm, chunk_frames, nullptr)) > 0) {
            const size_t base = interleaved.size();
            interleaved.resize(base + (size_t)ret * (size_t)channels);
            for (long f = 0; f < ret; ++f)
                for (int c = 0; c < channels; ++c)
                    interleaved[base + (size_t)f * (size_t)channels + (size_t)c] = pcm[c][f];
        }
        ov_clear(&vf);
        if (interleaved.empty()) return r;
        r.pcm = std::move(interleaved);
        r.sample_rate = rate;
        r.channels = channels;
        r.bits_per_sample = 32;   // decoded as float
        r.duration = total > 0.0 ? total
                    : (double)(r.pcm.size() / (size_t)channels) / (double)rate;
        r.format = "OGG Vorbis";
        r.ok = true;
        return r;
    };

    if (low == ".wav" || low == ".wave") {
        DecodedAudio r = try_wav();
        if (r.ok) return r;
    } else if (low == ".mp3") {
        DecodedAudio r = try_mp3();
        if (r.ok) return r;
    } else if (low == ".ogg" || low == ".oga") {
        DecodedAudio r = try_ogg();
        if (r.ok) return r;
    }
    // Fallbacks for mislabeled files.
    if (low != ".wav" && low != ".wave") { DecodedAudio r = try_wav(); if (r.ok) return r; }
    if (low != ".mp3") { DecodedAudio r = try_mp3(); if (r.ok) return r; }
    if (low != ".ogg" && low != ".oga") { DecodedAudio r = try_ogg(); if (r.ok) return r; }

    DecodedAudio fail;
    fail.error = "unsupported or corrupt audio file";
    return fail;
}

// ============================================================================
// Construction / teardown
// ============================================================================

AudioViewerPanel::AudioViewerPanel(QWidget* parent) : QWidget(parent) {
    m_audio_ok = SDL_Init(SDL_INIT_AUDIO);   // SDL3: bool; refcounted, safe to repeat

    setMinimumSize(360, 240);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_meta = new QLabel("Open a WAV, MP3, or OGG audio file", this);
    m_meta->setWordWrap(true);
    m_meta->setStyleSheet("QLabel { color:#8b93a3; font-size:12px; }");
    layout->addWidget(m_meta);

    // Controls row: play/pause, stop, time, volume.
    auto* controls = new QHBoxLayout();
    controls->setSpacing(8);
    m_play_btn = new QPushButton("\u25B6", this);
    m_play_btn->setFixedWidth(44);
    m_play_btn->setToolTip("Play / Pause (Space)");
    m_play_btn->setCursor(Qt::PointingHandCursor);
    m_play_btn->setStyleSheet(
        "QPushButton { background:#1f6feb; color:white; border:none; border-radius:4px; font-size:14px; padding:4px 0; }"
        "QPushButton:hover { background:#2a7df7; }"
        "QPushButton:disabled { background:#2c3038; color:#6a7180; }");
    connect(m_play_btn, &QPushButton::clicked, this, &AudioViewerPanel::on_play_pause);
    controls->addWidget(m_play_btn);

    auto* stop_btn = new QPushButton("\u25A0", this);
    stop_btn->setFixedWidth(44);
    stop_btn->setToolTip("Stop (position back to start)");
    stop_btn->setCursor(Qt::PointingHandCursor);
    stop_btn->setStyleSheet(
        "QPushButton { background:#3a3f4b; color:#d7dbe3; border:none; border-radius:4px; font-size:11px; padding:4px 0; }"
        "QPushButton:hover { background:#464c5a; }");
    connect(stop_btn, &QPushButton::clicked, this, &AudioViewerPanel::on_stop_clicked);
    controls->addWidget(stop_btn);

    m_time_label = new QLabel("0:00 / 0:00", this);
    m_time_label->setStyleSheet("QLabel { color:#c6ccd6; font-size:12px; font-family:monospace; }");
    controls->addWidget(m_time_label);

    controls->addStretch();

    auto* vol_label = new QLabel("Volume", this);
    vol_label->setStyleSheet("QLabel { color:#8b93a3; font-size:12px; }");
    controls->addWidget(vol_label);
    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setRange(0, 100);
    m_volume->setValue(80);
    m_volume->setFixedWidth(110);
    m_volume->setToolTip("Volume");
    connect(m_volume, &QSlider::valueChanged, this, &AudioViewerPanel::on_volume_slider);
    controls->addWidget(m_volume);
    layout->addLayout(controls);

    // Seek slider.
    m_seek = new QSlider(Qt::Horizontal, this);
    m_seek->setRange(0, 0);
    m_seek->setToolTip("Seek (drag, or click the waveform)");
    m_seek->setStyleSheet(
        "QSlider::groove:horizontal { height:4px; background:#2a2f3a; border-radius:2px; }"
        "QSlider::sub-page:horizontal { background:#1f6feb; border-radius:2px; }"
        "QSlider::handle:horizontal { width:12px; margin:-4px 0; border-radius:6px; background:#c6ccd6; }");
    connect(m_seek, &QSlider::sliderPressed, this, [this]() { m_seek_dragging = true; });
    connect(m_seek, &QSlider::sliderReleased, this, [this]() { m_seek_dragging = false; });
    connect(m_seek, &QSlider::valueChanged, this, &AudioViewerPanel::on_seek_slider);
    layout->addWidget(m_seek);

    // Waveform takes the remaining space (painted in paintEvent).
    layout->addStretch(1);

    // Tick ~20 Hz: feed the device stream and refresh position UI.
    m_timer = new QTimer(this);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, &AudioViewerPanel::on_tick);
    m_timer->start();

    m_play_btn->setEnabled(false);
    m_seek->setEnabled(false);
}

AudioViewerPanel::~AudioViewerPanel() {
    stop();
    close_stream();
    if (m_audio_ok) {
        // SDL_Init is refcounted inside SDL3; only quit our own audio init.
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

// ============================================================================
// Loading
// ============================================================================

bool AudioViewerPanel::load_audio(const QString& path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) return false;
    // Same file still decoding (tab re-activation / doc re-activation both
    // call here) — don't restart the worker.
    if (m_loading_path == path) return true;
    ++m_load_gen;                       // invalidate any in-flight decode
    stop();
    clear_audio();
    m_path = path;
    m_loading_path = path;
    m_meta->setText("Decoding " + QFileInfo(path).fileName() + " \u2026");

    const int gen = m_load_gen;
    const std::string p = path.toStdString();
    std::thread([this, gen, p]() {
        DecodedAudio r = decode_file(p);
        QMetaObject::invokeMethod(this, [this, gen, r = std::move(r)]() mutable {
            if (gen != m_load_gen) return;   // superseded by a newer load
            apply_loaded(std::move(r));
        }, Qt::QueuedConnection);
    }).detach();
    return true;
}

bool AudioViewerPanel::load_audio_sync(const QString& path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) return false;
    ++m_load_gen;
    stop();
    clear_audio();
    m_path = path;
    m_loading_path.clear();
    apply_loaded(decode_file(path.toStdString()));
    return m_info.loaded;
}

void AudioViewerPanel::clear_audio() {
    m_pcm.clear();
    m_waveform.clear();
    m_sample_rate = 0;
    m_channels = 0;
    m_duration = 0.0;
    m_cursor_bytes = 0;
    m_fed_bytes = 0;
    m_info = Info{};
    m_seek->blockSignals(true);
    m_seek->setRange(0, 0);
    m_seek->setValue(0);
    m_seek->blockSignals(false);
    m_seek->setEnabled(false);
    m_play_btn->setEnabled(false);
    refresh_time_label();
    update();
}

void AudioViewerPanel::apply_loaded(DecodedAudio&& audio) {
    close_stream();
    m_loading_path.clear();
    if (!audio.ok) {
        clear_audio();
        m_meta->setText(QString("Could not open %1 \u2014 %2")
                            .arg(QFileInfo(m_path).fileName(),
                                 QString::fromStdString(audio.error)));
        emit loadFinished(m_path, false, QString::fromStdString(audio.error));
        return;
    }
    m_pcm = std::move(audio.pcm);
    m_sample_rate = audio.sample_rate;
    m_channels = audio.channels;
    m_duration = audio.duration;
    m_info.loaded = true;
    m_info.sample_rate = audio.sample_rate;
    m_info.channels = audio.channels;
    m_info.bits_per_sample = audio.bits_per_sample;
    m_info.duration = audio.duration;
    m_info.format = std::move(audio.format);
    m_info.filename = fs::path(m_path.toStdString()).filename().string();
    m_cursor_bytes = 0;
    m_fed_bytes = 0;

    rebuild_waveform();
    open_stream();   // may fail without an audio device; metadata still works

    const int dur_ms = std::max(1, (int)std::llround(m_duration * 1000.0));
    m_seek->blockSignals(true);
    m_seek->setRange(0, dur_ms);
    m_seek->setValue(0);
    m_seek->blockSignals(false);
    m_seek->setEnabled(true);
    m_play_btn->setEnabled(true);
    refresh_metadata_label();
    refresh_ui();
    update();

    const QString detail = QString("%1  \u2022  %2 Hz  \u2022  %3 ch  \u2022  %4")
        .arg(QString::fromStdString(m_info.format))
        .arg(m_info.sample_rate)
        .arg(m_info.channels)
        .arg(fmt_time(m_duration));
    emit loadFinished(m_path, true, detail);
}

// ============================================================================
// Waveform (peak per window, mono mix, normalized — ported from av_audio)
// ============================================================================

void AudioViewerPanel::rebuild_waveform() {
    constexpr int kPoints = 500;
    m_waveform.clear();
    if (m_pcm.empty() || m_channels <= 0) return;
    const int total_mono = (int)(m_pcm.size() / (size_t)m_channels);
    if (total_mono <= 0) return;
    m_waveform.resize(kPoints);
    for (int i = 0; i < kPoints; ++i) {
        const int win_start = (int)((int64_t)i * total_mono / kPoints);
        const int win_end = (int)((int64_t)(i + 1) * total_mono / kPoints);
        float peak = 0.0f;
        for (int f = win_start; f < win_end; ++f) {
            float mono = 0.0f;
            for (int c = 0; c < m_channels; ++c)
                mono += m_pcm[f * m_channels + c];
            mono /= (float)m_channels;
            peak = std::max(peak, std::fabs(mono));
        }
        m_waveform[i] = peak;
    }
    const float max_peak = *std::max_element(m_waveform.begin(), m_waveform.end());
    if (max_peak > 0.0f)
        for (float& v : m_waveform) v /= max_peak;
}

// ============================================================================
// SDL3 playback (ported from av_audio, float32-only)
// ============================================================================

bool AudioViewerPanel::open_stream() {
    close_stream();
    if (!m_audio_ok || m_sample_rate <= 0 || m_channels <= 0) return false;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.freq = m_sample_rate;
    spec.channels = (Uint8)m_channels;
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                         nullptr, nullptr);
    if (!m_stream) return false;
    SDL_SetAudioStreamGain(m_stream, (float)m_gain);
    return true;
}

void AudioViewerPanel::close_stream() {
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    m_playing = false;
    m_paused = false;
    m_cursor_bytes = 0;
    m_fed_bytes = 0;
}

void AudioViewerPanel::feed_audio() {
    if (!m_stream || !m_playing || m_paused) return;
    const int total_bytes = (int)(m_pcm.size() * sizeof(float));
    const int frame_bytes = m_channels * (int)sizeof(float);
    if (frame_bytes <= 0 || m_sample_rate <= 0) return;
    const int min_buffer = std::max(frame_bytes * m_sample_rate / 10, 1 << 15);  // ~0.1s
    int queued = SDL_GetAudioStreamQueued(m_stream);
    while (queued < min_buffer && m_cursor_bytes < total_bytes) {
        const int remaining = total_bytes - m_cursor_bytes;
        const int chunk = std::min(remaining, 1 << 15);
        if (!SDL_PutAudioStreamData(m_stream,
                                    reinterpret_cast<const Uint8*>(m_pcm.data()) + m_cursor_bytes,
                                    chunk)) {
            break;
        }
        m_cursor_bytes += chunk;
        m_fed_bytes += chunk;
        queued += chunk;
    }
}

double AudioViewerPanel::position_seconds() const {
    if (!m_stream) return 0.0;
    const int queued = SDL_GetAudioStreamQueued(m_stream);
    const int abs_pos = std::max(0, m_cursor_bytes - queued);
    const int frame_bytes = m_channels * (int)sizeof(float);
    if (frame_bytes <= 0 || m_sample_rate <= 0) return 0.0;
    return (double)(abs_pos / frame_bytes) / (double)m_sample_rate;
}

void AudioViewerPanel::seek_seconds(double seconds) {
    if (!m_info.loaded || m_sample_rate <= 0) return;
    seconds = std::clamp(seconds, 0.0, m_duration);
    const int frame_bytes = m_channels * (int)sizeof(float);
    m_cursor_bytes = (int)(seconds * (double)m_sample_rate) * frame_bytes;
    if (m_stream) {
        SDL_ClearAudioStream(m_stream);
        m_fed_bytes = 0;
    }
}

// ============================================================================
// Slots
// ============================================================================

void AudioViewerPanel::on_tick() {
    if (!m_info.loaded) return;
    feed_audio();

    // Detect end-of-playback: everything fed and the device drained.
    if (m_playing && m_stream) {
        const int total_bytes = (int)(m_pcm.size() * sizeof(float));
        const int queued = SDL_GetAudioStreamQueued(m_stream);
        if (m_cursor_bytes >= total_bytes && queued <= 0) {
            m_playing = false;
            m_paused = false;
        }
    }
    refresh_ui();
}

void AudioViewerPanel::on_play_pause() {
    if (!m_info.loaded || !m_stream) return;
    if (m_playing && !m_paused) {
        SDL_PauseAudioStreamDevice(m_stream);
        m_playing = false;
        m_paused = true;
    } else if (m_paused) {
        SDL_ResumeAudioStreamDevice(m_stream);
        m_playing = true;
        m_paused = false;
    } else {
        // Fresh start (or restart after end).
        if (m_duration > 0.0 && position_seconds() >= m_duration - 0.05)
            seek_seconds(0.0);
        SDL_ResumeAudioStreamDevice(m_stream);
        m_playing = true;
        m_paused = false;
    }
    refresh_play_button();
    refresh_time_label();
}

void AudioViewerPanel::on_stop_clicked() { stop(); }

void AudioViewerPanel::stop() {
    if (m_stream) {
        SDL_PauseAudioStreamDevice(m_stream);
        SDL_ClearAudioStream(m_stream);
        m_fed_bytes = 0;
    }
    m_playing = false;
    m_paused = false;
    m_cursor_bytes = 0;
    refresh_ui();
}

void AudioViewerPanel::on_seek_slider(int value_ms) {
    if (!m_info.loaded) return;
    const double s = (double)value_ms / 1000.0;
    if (m_seek_dragging || !m_syncing) seek_seconds(s);
    refresh_time_label();
    update();
}

void AudioViewerPanel::on_volume_slider(int percent) {
    m_gain = std::clamp((double)percent / 100.0, 0.0, 1.0);
    if (m_stream) SDL_SetAudioStreamGain(m_stream, (float)m_gain);
}

// ============================================================================
// Painting — waveform with playback cursor (audio-editor style)
// ============================================================================

void AudioViewerPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect area = rect().adjusted(0, 0, -1, -1);
    const QRect wave = area.adjusted(0, area.height() - 140, 0, 0);
    const int top = wave.top();
    const int bottom = wave.bottom();
    const int mid = (top + bottom) / 2;

    p.fillRect(area, QColor(0x16, 0x19, 0x1f));
    p.setPen(QColor(0x2a, 0x2f, 0x3a));
    p.drawRect(area);

    if (!m_info.loaded || m_waveform.empty()) {
        p.setPen(QColor(0x5c, 0x63, 0x70));
        p.drawText(area, Qt::AlignCenter,
                   m_path.isEmpty()
                       ? QStringLiteral("Open a WAV / MP3 / OGG file to see its waveform")
                       : QStringLiteral("No waveform data available"));
        return;
    }

    // Center line.
    p.setPen(QPen(QColor(0x35, 0x3a, 0x46), 1));
    p.drawLine(wave.left(), mid, wave.right(), mid);

    // Vertical bars: one per pixel column, peak per column.
    const int w = wave.width();
    const int n = (int)m_waveform.size();
    QColor bar_col(0x4f, 0x9d, 0xf0);
    p.setPen(Qt::NoPen);
    p.setBrush(bar_col);
    if (w > 0) {
        for (int x = 0; x < w; ++x) {
            const int idx = std::min(n - 1, (int)((int64_t)x * n / w));
            const float v = m_waveform[idx];
            const int half = std::max(1, (int)((bottom - top) / 2 * v));
            p.drawRect(wave.left() + x, mid - half, 1, half * 2);
        }
    }

    // Playback cursor.
    if (m_duration > 0.0) {
        const double progress = std::clamp(position_seconds() / m_duration, 0.0, 1.0);
        const int x = wave.left() + (int)(progress * (double)w);
        p.setPen(QPen(QColor(0xff, 0x9a, 0x3c), 2));
        p.drawLine(x, top, x, bottom);
    }
}

bool AudioViewerPanel::seek_from_wave_click(const QPoint& pos) {
    if (!m_info.loaded || m_duration <= 0.0) return false;
    const QRect area = rect().adjusted(0, 0, -1, -1);
    const QRect wave = area.adjusted(0, area.height() - 140, 0, 0);
    if (!wave.contains(pos)) return false;
    const double frac = std::clamp((double)(pos.x() - wave.left()) / (double)std::max(1, wave.width()),
                                   0.0, 1.0);
    seek_seconds(frac * m_duration);
    refresh_ui();
    return true;
}

void AudioViewerPanel::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && seek_from_wave_click(event->position().toPoint())) {
        m_wave_dragging = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void AudioViewerPanel::mouseMoveEvent(QMouseEvent* event) {
    if (m_wave_dragging && (event->buttons() & Qt::LeftButton)) {
        seek_from_wave_click(event->position().toPoint());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void AudioViewerPanel::mouseReleaseEvent(QMouseEvent* event) {
    if (m_wave_dragging && event->button() == Qt::LeftButton) {
        m_wave_dragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void AudioViewerPanel::hideEvent(QHideEvent* event) {
    // Leaving the Audio Viewer tab (or the whole studio) silences playback.
    stop();
    QWidget::hideEvent(event);
}

// ============================================================================
// UI refresh helpers
// ============================================================================

void AudioViewerPanel::refresh_ui() {
    refresh_time_label();
    refresh_play_button();
    if (!m_syncing && !m_seek_dragging && m_info.loaded && m_duration > 0.0) {
        m_syncing = true;
        m_seek->setValue((int)std::llround(position_seconds() * 1000.0));
        m_syncing = false;
    }
    update();
}

void AudioViewerPanel::refresh_metadata_label() {
    if (!m_info.loaded) return;
    m_meta->setText(QString("%1  \u2022  %2  \u2022  %3 Hz  \u2022  %4 ch  \u2022  %5-bit  \u2022  %6  \u2022  %7")
        .arg(QString::fromStdString(m_info.filename),
             QString::fromStdString(m_info.format))
        .arg(m_info.sample_rate)
        .arg(m_info.channels)
        .arg(m_info.bits_per_sample)
        .arg(fmt_time(m_duration))
        .arg(fmt_bytes(QFileInfo(m_path).size())));
}

void AudioViewerPanel::refresh_time_label() {
    const double pos = m_info.loaded ? position_seconds() : 0.0;
    m_time_label->setText(QString("%1 / %2")
        .arg(fmt_time(pos))
        .arg(fmt_time(m_info.loaded ? m_duration : 0.0)));
}

void AudioViewerPanel::refresh_play_button() {
    if (!m_play_btn) return;
    if (m_playing && !m_paused) {
        m_play_btn->setText("\u23F8");   // pause
        m_play_btn->setToolTip("Pause (Space)");
    } else {
        m_play_btn->setText("\u25B6");   // play
        m_play_btn->setToolTip("Play / Pause (Space)");
    }
}

} // namespace ruby::panels