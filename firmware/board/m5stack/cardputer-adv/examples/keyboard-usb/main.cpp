// Adapted for EspHub from https://github.com/m5stack/M5Cardputer/blob/bf64d3178ab43928fa02365fbe3937cec99716ad/examples/Basic/keyboard/usbKeyboard/usbKeyboard.ino
#include "../common/keyboard.hpp"
#include "eh_agent.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

static const uint8_t descriptor[] = {TUD_HID_REPORT_DESC_KEYBOARD()};
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN, 0, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(descriptor), 0x81, 8, 10),
};

extern "C" const uint8_t* tud_hid_descriptor_report_cb(uint8_t) { return descriptor; }
extern "C" uint16_t tud_hid_get_report_cb(uint8_t, uint8_t, hid_report_type_t, uint8_t*, uint16_t)
{
    return 0;
}
extern "C" void tud_hid_set_report_cb(uint8_t, uint8_t, hid_report_type_t, const uint8_t*, uint16_t) {}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    cardputer::Keyboard keyboard;
    if (!keyboard.begin()) return;
    tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG();
    usb.descriptor.full_speed_config = configuration;
    ESP_ERROR_CHECK(tinyusb_driver_install(&usb));
    M5.Display.println("USB keyboard: connect a data cable");
    bool sendPending = true;
    for (;;) {
        cardputer::KeyEvent event;
        // Keep each press and release in the FIFO until the endpoint can take it.
        if (!tud_mounted()) {
            while (keyboard.next(event)) {}
            sendPending = true;
        } else if (tud_hid_ready() && (sendPending || keyboard.next(event))) {
            const auto report = keyboard.report();
            sendPending = !tud_hid_keyboard_report(0, report.modifiers, report.keys.data());
        }
        M5.delay(10);
    }
}
