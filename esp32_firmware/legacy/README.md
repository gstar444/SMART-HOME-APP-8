# Legacy v3.0.0 firmware (reference only)

`esp32_smart_home_v3/esp32_smart_home_v3.ino` is the pre-Phase-2 sketch, kept so you can
roll back or compare behaviour. **Do not use it on a network you do not fully trust**:
its REST API is unauthenticated by default, uses cleartext HTTP with credentials in the
URL, and opens a setup hotspot with a fixed, documented password.

The Phase 2 firmware in `../esp32_smart_home/` migrates the v3 device list and Wi-Fi
credentials automatically on first boot (see docs/MIGRATION.md).
