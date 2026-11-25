# SPDX-License-Identifier: MIT
# Frozen modules of the esp-bootloader Python runtime: much smaller than UIFlow's (no unit/chain/
# usb/driver libraries, no startup menu). Scripts add pure-Python libraries under /flash/lib.
module("_boot.py", base_path="$(MPY_DIR)/../m5stack/okl")
include("$(MPY_DIR)/../m5stack/modules/widgets/manifest.py")
include("$(MPY_DIR)/../m5stack/libs/requests2/manifest.py")
include("$(MPY_DIR)/extmod/asyncio")
require("ssl")
