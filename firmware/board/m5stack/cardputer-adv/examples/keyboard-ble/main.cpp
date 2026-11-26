// Adapted for EspHub from https://github.com/m5stack/M5Cardputer-UserDemo/blob/9d7190317d49ff87fa98db345b049ed77c5a1780/main/apps/app_keyboard/app_keyboard.cpp
#include "../common/keyboard.hpp"
#include "eh_agent.h"
#include "NimBLEDevice.h"
#include "NimBLEHIDDevice.h"
#include "HIDTypes.h"

// Report ID 1: eight modifier bits, one reserved byte and six keyboard usages.
static uint8_t reportMap[] = {
    USAGE_PAGE(1), 0x01, USAGE(1), 0x06, COLLECTION(1), 0x01, REPORT_ID(1), 1,
    USAGE_PAGE(1), 0x07, USAGE_MINIMUM(1), 0xe0, USAGE_MAXIMUM(1), 0xe7,
    LOGICAL_MINIMUM(1), 0, LOGICAL_MAXIMUM(1), 1, REPORT_SIZE(1), 1, REPORT_COUNT(1), 8,
    HIDINPUT(1), 0x02, REPORT_SIZE(1), 8, REPORT_COUNT(1), 1, HIDINPUT(1), 0x01,
    USAGE_MINIMUM(1), 0, USAGE_MAXIMUM(1), 0x65, LOGICAL_MAXIMUM(1), 0x65,
    REPORT_COUNT(1), 6, HIDINPUT(1), 0x00, END_COLLECTION(0)
};

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    cardputer::Keyboard keyboard;
    if (!keyboard.begin()) return;
    const char* name = "Cardputer ADV";
    if (!NimBLEDevice::init(name)) {
        M5.Display.println("BLE unavailable");
        return;
    }
    NimBLEDevice::setSecurityAuth(true, false, true);
    auto* server = NimBLEDevice::createServer();
    server->advertiseOnDisconnect(true);
    NimBLEHIDDevice hid(server);
    auto* input = hid.getInputReport(1);
    hid.setReportMap(reportMap, sizeof(reportMap));
    hid.setManufacturer("M5Stack");
    // USB vendor/product IDs used by M5Stack's ADV factory HID demo.
    hid.setPnp(2, 0x16c0, 0x05df, 0x0100);
    hid.setHidInfo(0, 2);
    const int level = M5.Power.getBatteryLevel();
    if (level >= 0 && level <= 100) hid.setBatteryLevel(level);
    server->start();
    auto* advertising = NimBLEDevice::getAdvertising();
    advertising->setAppearance(0x03c1); // HID keyboard
    advertising->addServiceUUID(hid.getHidService()->getUUID());
    advertising->enableScanResponse(true);
    advertising->setName(name);
    if (!advertising->start()) {
        M5.Display.println("BLE advertising failed");
        return;
    }
    M5.Display.println("Pair with Cardputer ADV");
    bool retry = false;
    for (;;) {
        cardputer::KeyEvent event;
        if (retry || keyboard.next(event)) {
            const auto report = keyboard.report();
            input->setValue(report);
            retry = server->getConnectedCount() && !input->notify();
        }
        M5.delay(10);
    }
}
