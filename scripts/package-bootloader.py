"""Validate one EspHub bootloader build and collect its published bootloader set.

Every board with this bootloader ID shares the set: the ESP-IDF second-stage bootloader,
the partition table and the EspHub Launcher. build.json describes where each file and the
board's app and NVS images go on flash."""
import csv
import json
import re
import shutil
import sys
from hashlib import sha256
from pathlib import Path

from elftools.elf.elffile import ELFFile

bootloader_id, version = sys.argv[1:]
flash = bootloader_id.split("-", 2)[1]
build = Path("build")
out = Path(__file__).resolve().parents[1] / "shared-bl"
out.mkdir(exist_ok=True)
parts = json.loads((build / "flasher_args.json").read_text())
config = Path("sdkconfig").read_text()
image = (build / "esp-bootloader.bin").read_bytes()
settings = parts["flash_settings"]
assert settings["flash_size"].lower() == flash
assert image[288:].startswith(f"ESPHUB2/bootloader/{bootloader_id}\0bootloader-ota-v2\0".encode())
# Check the linked firmware.
# An MQTT client configured for TLS can still link ESP-IDF's default WS transport.
lines = set(config.splitlines())
for key in ("CONFIG_MQTT_TRANSPORT_SSL", "CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS"):
    assert f"{key}=y" in lines, f"Missing {key}"
for key in ("CONFIG_WS_TRANSPORT", "CONFIG_MQTT_TRANSPORT_WEBSOCKET", "CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE"):
    assert f"{key}=y" not in lines, f"Unexpected {key}"
with (build / "esp-bootloader.elf").open("rb") as elf:
    symbols = {symbol.name for symbol in ELFFile(elf).get_section_by_name(".symtab").iter_symbols()
               if symbol["st_shndx"] != "SHN_UNDEF"}
unexpected = sorted(s for s in symbols if s.startswith(("esp_transport_ws_", "esp_websocket_client_")))
assert not unexpected, f"WebSocket implementation linked: {unexpected}"
required = {"esp_transport_ssl_init", "esp_mqtt_client_start", "esp_http_client_perform", "eh_hub_host_valid"}
assert required <= symbols, f"Missing native transport or hostname validation: {required - symbols}"
print(f"{bootloader_id} {build}: {len(image)} bytes, SHA-256 {sha256(image).hexdigest()}; ELF transport checks passed")

# The table the build flashed is generated from the shared layout for this flash size.
table_csv = re.search(r'^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="([^"]+)"$', config, re.M)[1]
table_csv = (Path.cwd() / table_csv).resolve()
assert table_csv == Path(__file__).resolve().parents[1] / "firmware/partitions" / f"{flash}.csv", table_csv
with table_csv.open() as f:
    rows = {row[0]: row for row in csv.reader(f, skipinitialspace=True)
            if len(row) >= 5 and not row[0].startswith("#")}
launcher, standby, app, nvs = (rows[key] for key in ("launcher_a", "launcher_b", "app", "nvs"))
assert launcher[1:3] == ["app", "ota_0"] and standby[1:3] == ["app", "ota_1"] and app[1:3] == ["app", "ota_2"]
assert nvs[1:3] == ["data", "nvs"]
assert len(image) <= min(int(launcher[4], 0), int(standby[4], 0))

files = {name: int(offset, 0) for offset, name in parts["flash_files"].items()}
assert files["partition_table/partition-table.bin"] == 0x8000
published = {
    "bootloader": ("bootloader.bin", build / "bootloader/bootloader.bin", files["bootloader/bootloader.bin"]),
    "partition_table": ("partition-table.bin", build / "partition_table/partition-table.bin", 0x8000),
    "launcher": ("launcher.bin", build / "esp-bootloader.bin", int(launcher[3], 0)),
}
descriptor = {
    "id": bootloader_id,
    "version": version,
    "chip": parts.get("extra_esptool_args", {}).get("chip") or bootloader_id.split("-", 1)[0],
    "flash_mode": settings["flash_mode"],
    "flash_size": settings["flash_size"],
    "flash_freq": settings["flash_freq"],
    "app_offset": int(app[3], 0),
    "nvs_offset": int(nvs[3], 0),
    "nvs_size": int(nvs[4], 0),
    "files": {},
}
for key, (name, source, offset) in published.items():
    data = source.read_bytes()
    assert data, f"Empty {name}"
    shutil.copyfile(source, out / name)
    descriptor["files"][key] = {"name": name, "offset": offset, "size": len(data), "sha256": sha256(data).hexdigest()}
(out / "build.json").write_text(json.dumps(descriptor, indent=2) + "\n")
