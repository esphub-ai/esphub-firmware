# Native Wi-Fi manager fixture

Run from the repository root:

```sh
node --test tests/wifi.test.js
```

Requires `cc`, `pkg-config` and `libcjson` development headers (Debian package `libcjson-dev`). The Node harness compiles the actual `firmware/components/okou_agent/src/okou_wifi.c` as its own translation unit, then executes each scenario in a fresh process. It does not copy the connection algorithm or inspect source text.

## Simulation

- ESP-IDF scan/config/connect/disconnect calls use in-memory AP records. Assertions inspect actual station configuration, SSID order and selected AP.
- FreeRTOS mutexes check radio/list ownership. Task creation captures the production task; `setjmp`/`longjmp` stops it only at its quiescent online/backoff wait. The production task's lifetime scan allocation is left to process teardown.
- Pending radio events invoke production `okou_wifi_on_got_ip()` / `okou_wifi_on_disconnected()` hooks. Fake event waits advance an integer tick clock immediately, including through unsigned tick wraparound. There are no wall-clock sleeps, timers, threads, sockets or devices.
- NVS uses real libcjson and in-memory JSON, including failed persistence and rollback. No scan hint is written to NVS.

## Coverage

- Strongest-first visible saved SSIDs, distinct from saved order; deduplication keeps one strongest AP's RSSI/channel/auth/BSSID together; unsaved APs are ignored.
- Fresh scan hints select `WIFI_FAST_SCAN` with channel and BSSID; no unnecessary unpinned attempt after success.
- Failure clears channel, BSSID and `bssid_set` and retries the same SSID with `WIFI_ALL_CHANNEL_SCAN`, including another AP and an AP that changed channel.
- Initial attempt and fallback share the original 15-second per-SSID deadline. Tests include config/connect elapsed time, independent disconnect-cleanup waits, remaining-budget recovery, fallback timeout, exhausted-budget skip, one-tick remainder and tick wraparound. No new hinted-attempt timeout exists.
- Immediate config/connect failures, scan failure and no stale hint reuse on the next cycle.
- Hidden/unseen saved SSIDs and manual visible/hidden joins stay unpinned; failed manual joins and failed persistence preserve existing saved access. Queued manual joins preempt fallback.

## Limits

This models the C manager's configuration and deterministic control flow, **not the ESP-IDF Wi-Fi driver, ABI, real RF scan/association duration, authentication, DHCP, or task concurrency**. In particular, the fake deliberately rejects a stale targeted channel to exercise manager recovery; real ESP-IDF may discover a moved AP without that failure. The fake emits a deterministic disconnect event during cleanup and cancels pending association events. It does not prove behavior for arbitrary delayed/stale driver events or scheduler interleavings. A target ESP-IDF build and real-device RF validation remain separate checks; no RF latency improvement is claimed here.
