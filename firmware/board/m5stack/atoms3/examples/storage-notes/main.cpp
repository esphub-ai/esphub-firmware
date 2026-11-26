// Adapted from the MIT-licensed M5AtomS3 SPIFFS write-and-read example.
// The EspHub data partition is LittleFS and must already be initialized.
#include "../storage_io.hpp"

namespace {
constexpr char greeting[] = "Hello IOT World.\n";

bool prepareFile()
{
    // O_EXCL preserves a file from an earlier boot or another application.
    const int fd = open(storage::filePath, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
        if (errno == EEXIST) {
            storage::show("Existing note kept", "Press A to read");
            return true;
        }
        storage::show("Create failed");
        return false;
    }
    const auto written = write(fd, greeting, sizeof(greeting) - 1);
    const int closed = close(fd);
    if (written != sizeof(greeting) - 1 || closed != 0) {
        unlink(storage::filePath);
        storage::show("Write failed");
        return false;
    }

    storage::show("Note written", "Press A to read");
    std::printf("Wrote %zu bytes to %s\n", sizeof(greeting) - 1, storage::filePath);
    return true;
}

}

extern "C" void app_main()
{
    if (storage::start()) {
        storage::pollRead(prepareFile());
    }
}
