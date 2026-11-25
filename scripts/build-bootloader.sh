#!/usr/bin/env bash
set -eo pipefail
source "$IDF_PATH/export.sh"
cd "firmware/bootloader/$VARIANT"
idf.py -DPROJECT_VER="$VERSION" build
for setting in PM_ENABLE OKOU_BLE_WIFI BT_NIMBLE_ROLE_OBSERVER BT_NIMBLE_ROLE_PERIPHERAL; do
  grep -Fx "CONFIG_${setting}=y" sdkconfig
done
if [[ "$VARIANT" == "esp32s31-16mb-usb-cdc" ]]; then
  grep -Fx '# CONFIG_FREERTOS_USE_TICKLESS_IDLE is not set' sdkconfig
else
  grep -Fx 'CONFIG_FREERTOS_USE_TICKLESS_IDLE=y' sdkconfig
fi
for setting in BOOTLOADER_APP_ROLLBACK_ENABLE BT_NIMBLE_ROLE_CENTRAL ESP_WIFI_SOFTAP_SUPPORT SPIRAM; do
  if grep -Fx "CONFIG_${setting}=y" sdkconfig; then exit 1; fi
done
python ../../../scripts/package-bootloader.py "$VARIANT" "$VERSION"
