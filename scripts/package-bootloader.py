"""Validate one ESPHub bootloader build and collect the reusable binaries."""
import json
import os
import shutil
import sys
from hashlib import sha256
from pathlib import Path

from elftools.elf.elffile import ELFFile

variant, version = sys.argv[1:]
flash = variant.split("-", 2)[1]
build = Path("build")
out = Path(__file__).resolve().parents[1] / "shared-bl"
out.mkdir(exist_ok=True)
parts = json.loads((build / "flasher_args.json").read_text())
config = Path("sdkconfig").read_text()
image = (build / "esp-bootloader.bin").read_bytes()
assert parts["flash_settings"]["flash_size"].lower() == flash
assert image[288:].startswith(f"ESPHUB2/bootloader/{variant}\0bootloader-ota-v2\0".encode())
# Check the linked firmware.
# An MQTT client configured for TLS can still link ESP-IDF's default WS transport.
settings = set(config.splitlines())
for key in ("CONFIG_MQTT_TRANSPORT_SSL", "CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS"):
    assert f"{key}=y" in settings, f"Missing {key}"
for key in ("CONFIG_WS_TRANSPORT", "CONFIG_MQTT_TRANSPORT_WEBSOCKET", "CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE"):
    assert f"{key}=y" not in settings, f"Unexpected {key}"
with (build / "esp-bootloader.elf").open("rb") as elf:
    symbols = {symbol.name for symbol in ELFFile(elf).get_section_by_name(".symtab").iter_symbols()
               if symbol["st_shndx"] != "SHN_UNDEF"}
unexpected = sorted(s for s in symbols if s.startswith(("esp_transport_ws_", "esp_websocket_client_")))
assert not unexpected, f"WebSocket implementation linked: {unexpected}"
required = {"esp_transport_ssl_init", "esp_mqtt_client_start", "esp_http_client_perform", "okou_hub_host_valid"}
assert required <= symbols, f"Missing native transport or hostname validation: {required - symbols}"
print(f"{variant} {build}: {len(image)} bytes, SHA-256 {sha256(image).hexdigest()}; ELF transport checks passed")

files = {name: offset for offset, name in parts["flash_files"].items()}
assert files["partition_table/partition-table.bin"] == "0x8000"
shutil.copyfile(build / "bootloader/bootloader.bin", out / "bootloader.bin")
shutil.copyfile(build / "esp-bootloader.bin", out / "esp-bootloader.bin")
(out / "build.json").write_text(json.dumps({
    "source_commit": os.environ["GITHUB_SHA"],
    "variant": variant,
    "version": version,
    "bootloader_offset": files["bootloader/bootloader.bin"],
    "partition_offset": files["partition_table/partition-table.bin"],
    "write_flash_args": parts["write_flash_args"],
    "ota_sha256": sha256(image).hexdigest(),
}, indent=2) + "\n")
