// Adapted from the MIT-licensed M5AtomS3 SPIFFS_Add example.
#include "../storage_io.hpp"

namespace {
constexpr char addition[] = "This is Appended Info.\n";

bool appendOrCreate()
{
    const int fd = open(storage::filePath, O_WRONLY | O_APPEND);
    if (fd < 0) {
        if (errno == ENOENT) {
            const int created = open(storage::filePath, O_WRONLY | O_CREAT | O_EXCL, 0664);
            if (created >= 0 && close(created) == 0) {
                storage::show("Empty note created", "Press A to read");
                return true;
            }
        }
        storage::show("Open failed");
        return false;
    }

    const auto written = write(fd, addition, sizeof(addition) - 1);
    const int closed = close(fd);
    if (written != sizeof(addition) - 1 || closed != 0) {
        storage::show("Append failed");
        return false;
    }

    storage::show("Appended one line", "Press A to read");
    return true;
}
}

extern "C" void app_main()
{
    if (storage::start()) {
        storage::pollRead(storage::prepareDirectory() && appendOrCreate());
    }
}
