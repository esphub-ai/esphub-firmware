// Adapted from the MIT-licensed M5AtomS3 SPIFFS_Delete example.
#include "../storage_io.hpp"

extern "C" void app_main()
{
    if (!storage::start()) {
        return;
    }

    struct stat note{};
    if (stat(storage::filePath, &note) != 0) {
        if (errno == ENOENT) {
            storage::show("Note not found");
        } else {
            storage::show("Lookup failed");
        }
        return;
    }
    if (!S_ISREG(note.st_mode)) {
        storage::show("Not a regular file");
        return;
    }
    if (unlink(storage::filePath) != 0) {
        storage::show("Delete failed");
        return;
    }
    storage::show("Note deleted");
}
