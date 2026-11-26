// Adapted for EspHub from https://github.com/m5stack/M5Cardputer/blob/bf64d3178ab43928fa02365fbe3937cec99716ad/examples/Basic/mic_wav_record/mic_wav_record.ino
#include "../common/sd-card.hpp"
#include "eh_agent.h"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

static constexpr uint32_t rate = 16000;
static constexpr size_t blockSamples = 1000, blocks = 48;
static constexpr uint32_t dataBytes = blocks * blockSamples * sizeof(int16_t);

// ESP32-S3 stores these fixed-width PCM fields in WAV's little-endian order.
struct WavHeader {
    char riff[4] = {'R','I','F','F'};
    uint32_t size = 36 + dataBytes;
    char wave[4] = {'W','A','V','E'};
    char fmt[4] = {'f','m','t',' '};
    uint32_t fmtSize = 16;
    uint16_t format = 1, channels = 1;
    uint32_t sampleRate = rate, byteRate = rate * 2;
    uint16_t blockAlign = 2, bits = 16;
    char data[4] = {'d','a','t','a'};
    uint32_t dataSize = dataBytes;
};
static_assert(sizeof(WavHeader) == 44);

static bool capture(int fd)
{
    static int16_t buffers[2][blockSamples];
    WavHeader header;
    if (write(fd, &header, sizeof(header)) != sizeof(header) || !M5.Mic.begin()) return false;

    bool ok = true;
    for (size_t i = 0; ok && i < blocks; ++i) {
        ok = M5.Mic.record(buffers[i % 2], blockSamples, rate);
        if (ok && i > 0) {
            // The two requests finish in order. Save the completed buffer
            // while the microphone fills the other one.
            while (M5.Mic.isRecording() > 1) M5.delay(1);
            auto& previous = buffers[(i - 1) % 2];
            ok = write(fd, previous, sizeof(previous)) == sizeof(previous);
        }
    }
    while (M5.Mic.isRecording()) M5.delay(1);
    M5.Mic.end();
    auto& last = buffers[(blocks - 1) % 2];
    return ok && write(fd, last, sizeof(last)) == sizeof(last);
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    if (!cardputer::mountCard()) return;
    M5.Speaker.end();
    M5.Display.println("BtnA: record 3 seconds");
    unsigned next = 0;
    for (;;) {
        M5.update();
        if (M5.BtnA.wasClicked()) {
            char path[24], temporary[24];
            int fd = -1;
            while (next < 10000) {
                snprintf(path, sizeof(path), "/sd/REC%04u.WAV", next);
                snprintf(temporary, sizeof(temporary), "/sd/REC%04u.TMP", next++);
                if (access(path, F_OK) == 0) continue;
                if (errno != ENOENT) break;
                fd = open(temporary, O_CREAT | O_EXCL | O_WRONLY, 0666);
                if (fd >= 0 || errno != EEXIST) break;
            }
            if (fd < 0) {
                M5.Display.println("Cannot create a new WAV file");
                return;
            }
            M5.Display.clear();
            M5.Display.setCursor(0, 0);
            M5.Display.println("Recording...");
            const bool recorded = capture(fd);
            const bool closed = close(fd) == 0;
            if (recorded && closed && rename(temporary, path) == 0) {
                M5.Display.printf("Saved %s\n", path);
            } else {
                unlink(temporary); // Only publish a complete WAV.
                M5.Display.println("Recording failed");
            }
            M5.Display.println("BtnA: record again");
        }
        M5.delay(10);
    }
}
