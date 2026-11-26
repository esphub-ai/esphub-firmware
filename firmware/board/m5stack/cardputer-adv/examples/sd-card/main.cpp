// Adapted for EspHub from https://github.com/m5stack/M5Cardputer/blob/bf64d3178ab43928fa02365fbe3937cec99716ad/examples/Basic/sdcard/sdcard.ino
#include "../common/sd-card.hpp"
#include "eh_agent.h"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    if (!cardputer::mountCard()) return;
    char path[24];
    int fd = -1;
    for (int i = 0; i < 1000; ++i) {
        snprintf(path, sizeof(path), "/sd/HELLO%03d.TXT", i);
        fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) {
        M5.Display.println("Cannot create a new file");
        return;
    }
    constexpr char greeting[] = "Hello from Cardputer ADV!";
    char result[sizeof(greeting)]{};
    const bool ok = write(fd, greeting, sizeof(greeting) - 1) == sizeof(greeting) - 1 &&
                    fsync(fd) == 0 && lseek(fd, 0, SEEK_SET) == 0 &&
                    read(fd, result, sizeof(result) - 1) == sizeof(greeting) - 1;
    const bool closed = close(fd) == 0;
    if (ok && closed) M5.Display.printf("%s\n%s\n", path, result);
    else M5.Display.println("SD read/write failed");
}
