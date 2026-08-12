# Hivemind

**Self-spreading firmware for ESP32-C6 — an authorized fleet-provisioning / OTA tool.**

Flash **one** board over USB with a new firmware version. It announces that version over
ESP-NOW; neighbouring boards that hear a **newer, cryptographically verified** version pull
it wirelessly, install it safely, and start announcing it themselves. The update spreads
board-to-board across a room — after each board's **one and only** wired flash.

This is controlled **epidemic / gossip firmware dissemination** (Trickle / Deluge lineage),
built for provisioning large ESP32-C6 fleets. It is **not** malware: every node refuses any
image it cannot verify came from the fleet owner, so only firmware *you* signed is ever
accepted or spread. See [`docs/design.md`](docs/design.md) for the full design and security
model.

> ⚠️ Status: **early — design phase.** The specification lives in `docs/design.md`;
> implementation follows.

## Why

Provisioning and updating hundreds of boards by plugging in a USB cable for every change
does not scale. A blank chip cannot receive anything over the air (its radio isn't running),
so each board still needs **one** wired flash to install a permanent listener. After that,
every future update is wireless. Hivemind makes the first flash the **last** wired flash a
board ever needs.

## Safety in one glance

- **Recovery image** — each board keeps a small, immutable recovery program (its "ears" and
  "parachute"). A bad update drops the board into safe mode and heals it over the air instead
  of bricking it.
- **Integrity always on** — SHA-256 before install *and* before re-spread, so a corrupt
  image can never become contagious.
- **Narrow authorization** — a pluggable `verify_image()` seam (HMAC for prototyping,
  ECDSA signatures for production). Distribution is open; only signed firmware is trusted.

## License

MIT — see [`LICENSE`](LICENSE).
