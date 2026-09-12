// audio_viewer_test.cpp — AudioViewerPanel acceptance test.
// Writes a real RIFF WAV (16-bit mono sine), loads it through the panel's
// public API and asserts metadata + waveform are produced, exercises the
// async load path, and verifies corrupt input fails cleanly. Offscreen Qt;
// SDL3 audio device presence is NOT required (decode + waveform work without
// one, exactly like the ImGui asset viewer).
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QThread>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "ruby/panels/audio_viewer_panel.h"

namespace {

bool write_sine_wav(const QString& path, int rate, int seconds) {
    const int samples = rate * seconds;
    std::vector<int16_t> pcm(samples);
    for (int i = 0; i < samples; ++i)
        pcm[i] = (int16_t)(12000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / rate));

    const uint32_t data_bytes = (uint32_t)(pcm.size() * sizeof(int16_t));
    const uint32_t byte_rate = (uint32_t)(rate * sizeof(int16_t));

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    auto put32 = [&f](uint32_t v) {
        const char b[4] = {(char)(v & 0xff), (char)((v >> 8) & 0xff),
                           (char)((v >> 16) & 0xff), (char)((v >> 24) & 0xff)};
        f.write(b, 4);
    };
    auto put16 = [&f](uint16_t v) {
        const char b[2] = {(char)(v & 0xff), (char)((v >> 8) & 0xff)};
        f.write(b, 2);
    };
    f.write("RIFF", 4);
    put32(36 + data_bytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    put32(16);            // fmt chunk size
    put16(1);             // PCM
    put16(1);             // mono
    put32((uint32_t)rate);
    put32(byte_rate);
    put16(2);             // block align
    put16(16);            // bits per sample
    f.write("data", 4);
    put32(data_bytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), pcm.size() * sizeof(int16_t));
    f.close();
    return f.size() > 0;
}

} // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    const QString dir = "/tmp/ruby_audio_viewer_test";
    QDir().mkpath(dir);
    const QString wav_path = dir + "/sine440.wav";
    const QString bad_path = dir + "/corrupt.bin";

    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
    };

    if (!write_sine_wav(wav_path, 44100, 1)) {
        std::printf("FAIL: could not write test WAV\n");
        return 1;
    }
    // Corrupt file: not a RIFF/MP3/OGG stream at all (no crash, clean fail).
    {
        QFile dst(bad_path);
        if (dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const char junk[64] = "this is definitely not an audio container\0\x01\x02\xff...";
            dst.write(junk, sizeof(junk));
        }
    }

    {
        ruby::panels::AudioViewerPanel panel;

        // Sync load of a valid WAV.
        check(panel.load_audio_sync(wav_path), "sync load of valid WAV");
        const auto& info = panel.info();
        check(info.loaded, "info.loaded after valid load");
        check(info.sample_rate == 44100, "sample rate 44100");
        check(info.channels == 1, "mono");
        check(info.bits_per_sample == 16, "16-bit PCM");
        check(std::fabs(info.duration - 1.0) < 0.05, "duration ~1.0s");
        check(info.format == "WAV PCM", "format label WAV PCM");
        check(panel.waveform().size() == 500, "waveform has 500 points");
        bool any_peak = false;
        for (float v : panel.waveform())
            if (v > 0.0f) { any_peak = true; break; }
        check(any_peak, "waveform contains signal peaks");
        panel.stop();

        // Async load path (worker thread + queued apply).
        {
            bool got_signal = false;
            QObject::connect(&panel, &ruby::panels::AudioViewerPanel::loadFinished,
                             &app, [&](const QString&, bool ok, const QString&) {
                                 got_signal = ok;
                             });
            check(panel.load_audio(wav_path), "async load starts");
            for (int i = 0; i < 200 && !got_signal; ++i) {
                app.processEvents();
                QThread::msleep(10);
            }
            check(got_signal, "async loadFinished emitted with ok=true");
        }

        // Corrupt input must fail cleanly, not crash.
        check(!panel.load_audio_sync(bad_path), "corrupt file rejected");
        check(!panel.info().loaded, "info.loaded false after failed load");

        // Missing file.
        check(!panel.load_audio(dir + "/nope.wav"), "missing file rejected");
    }

    std::printf(checks == 0 ? "FAIL: no checks ran\n"
                            : "%s  (%d checks)\n",
                failures == 0 ? "PASS: audio viewer works" : "FAIL: audio viewer",
                checks);
    QDir(dir).removeRecursively();
    return failures == 0 ? 0 : 1;
}