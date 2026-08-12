# Hivemind — self-spreading firmware for ESP32-C6

**Design document** · 2026-08-12 · author: lazyturtle22

---

## 1. One-line summary

Flash **one** ESP32-C6 board over USB with a new firmware version. It announces that
version over ESP-NOW. Neighbouring boards that hear a **newer, verified** version pull it
wirelessly, install it, reboot into it, and start announcing it themselves. The version
"wave" crosses the room hop by hop. After the very first wired flash of each board, every
future update travels board-to-board with **no cable**.

This is a controlled implementation of **epidemic / gossip firmware dissemination**
(the academic lineage: Trickle, Deluge/MOAP). It is an **authorized fleet-provisioning /
OTA tool**, not malware — the whole security model exists to make sure only firmware
*you* signed is ever accepted or spread.

## 2. The problem it solves

- We need to provision and update **500–1000 ESP32-C6 boards** (the WiFi-sensing mesh
  fleet). Hand-flashing each one over USB, for every update, does not scale.
- **Physical reality that bounds the design:** a *blank* chip has no radio firmware
  running, so it cannot hear anything over the air. Therefore **every board needs one
  wired flash, ever**, to install a permanent listener ("ears"). After that one flash,
  all future updates are wireless.
- So this tool does **not** eliminate the first flash (impossible). It makes the first
  flash the **last** wired flash a board ever needs.

## 3. Scope

### In scope (v1 — the standalone spreader)
- A minimal ESP-IDF firmware whose only job is to spread itself and show its version.
- ESP-NOW transport, automatic (epidemic) propagation with Trickle timing.
- Recovery-image safety model (see §6) so a bad update can never brick a board.
- SHA-256 integrity always-on; a pluggable `verify_image()` authenticity seam.
- Host-side firmware **registry/history** + a live fleet **version-map** dashboard.
- Windows-first PowerShell tooling (build / seed / monitor).

### Explicitly NOT in v1 (kept as clean future work)
- WiFi "fast-path" for quicker transfer of large images.
- Full **hardware** Secure Boot (irreversible eFuse burn).
- Grafting the agent into the large CSI sensing firmware.
- Mixed-chip fleets (anything other than ESP32-C6).

## 4. Hardware context

Target: **ESP32-C6** (native USB-Serial-JTAG), the same nodes used by the `Wifi-vision-test`
CSI mesh. Those nodes currently run a **single-app, 2 MB** partition layout with **no OTA
slots** — so establishing an OTA-capable layout is itself part of the one-time first flash.
2 MB is tight, which directly motivates the recovery-image design in §6.

## 5. What OTA is (reference)

OTA = replacing the running firmware over a wireless/network link. The hard part is doing it
**safely** — you are replacing the program while it runs, and a mistake can make the device
unreachable forever. The mechanism:

1. **Partitions** — flash is divided into regions: bootloader, partition table, one or more
   app slots, `nvs` (settings), `otadata` (which slot to boot).
2. **Bootloader** runs first, reads `otadata`, jumps into the chosen app slot.
3. **Never overwrite the running app** — write the new image into an *inactive* slot, so a
   power cut mid-write only damages the spare.
4. **Switch = flip a pointer** in `otadata`, then reboot. Atomic and cheap.
5. **Verify before trust** — SHA-256 (integrity) + signature/HMAC (authenticity) before the
   switch.
6. **Confirm-or-rollback** — the new image must mark itself healthy after boot; if it
   crashes/hangs/never confirms, the bootloader reverts. This is the anti-brick seatbelt.

The **transport is independent of OTA** — OTA only cares *where bytes land* and *whether
they're trusted*, not how they arrived. That is why ESP-NOW is a free choice.

## 6. Node architecture

### 6.1 Two on-device programs (the recovery-image pattern)

Rather than two full app copies (which do not fit 2 MB), each board holds:

```
[ recovery ]  small, IMMUTABLE. ESP-NOW + UART receiver, OTA writer, verify(). The "ears"
              and the "parachute". Flashed once, never updated over the air.
[ ota_0    ]  the real app (the spreader). Overwritten on every update.
[ nvs ]  [ otadata ]  settings + boot bookkeeping.
```

- **Normal boot** → run the real app in `ota_0`.
- **Update** → receive new image, write into `ota_0`, verify, flip pointer, reboot.
- **Bad update** → bootloader falls back to the **recovery** program, which sits in
  "safe mode" with a **working radio** and waits to receive a fixed image over the air (or
  UART), then re-flashes `ota_0`.

Why this beats two full slots:

- **Fits flash.** Two full ~1.3 MB apps ≈ 2.6 MB (does not fit 2 MB). One full app +
  ~0.4–0.5 MB recovery ≈ 1.8 MB (**fits**). This is what makes OTA feasible on these boards.
- **Un-brickable even when the update kills the radio.** The recovery program is a
  *separate, known-good program with its own working radio*, so it restores the ability to
  be healed wirelessly — which a plain "re-transmit the old image" scheme cannot, because a
  broken radio can't receive anything.

**Tradeoff (accepted):** a failed update drops that node into safe mode (not doing its real
job) until a fixed image arrives, instead of instantly resuming the previous full app. In a
sensing mesh this means one node's coverage briefly blinks out and self-heals. Acceptable.

**Rule:** the recovery program is **immutable** — flashed once at the bench/factory, never
OTA'd. It is the safety net and the permanent "ears"; overwriting it with a bad image would
remove the net.

### 6.2 History lives on the host, not the node

A node keeps **no deep version history** — only the current app. All previous versions are
kept **centrally** in the host firmware registry (§8), which can re-seed any of them on
demand. Rationale: nodes are flash-poor; the host is not; and any old version can always be
re-transmitted.

### 6.3 Node state machine

```
each node holds: current_version, image_hash
loop:
  BEACON  -> broadcast { version, hash, chunk_count }   (Trickle-timed)
  LISTEN  -> on hearing version > mine AND verify() ok AND chip-id ok:
       request image in chunks from that neighbour
       write chunks into ota_0
       on complete: SHA-256 integrity check -> verify_image() -> set boot slot -> reboot
  CONFIRM -> new image must mark itself healthy within N seconds/boots
       ok  -> mark valid (permanent)
       else -> AUTO-ROLLBACK (bootloader falls back to recovery, heals over air)
  -> now beaconing the new version = I am a spreader
```

### 6.4 Trickle timing (anti-flood)

A node stays quiet if it already hears **k** neighbours announcing the same version.
Beacon interval **grows** while everyone agrees (near-silent at rest) and **snaps short**
when a newer version appears (loud exactly when there is news). Keeps the air — and, once
grafted into CSI, the sensing channel — usable.

## 7. Safety & security

### 7.1 Integrity — always on
Every image is SHA-256 checked **before install and before re-spread**, so a corrupt image
can never become contagious. Non-negotiable, even in a demo.

### 7.2 Authenticity — one pluggable seam
A single `verify_image()` function decides "is this from someone allowed?":
- **Prototype:** HMAC over a shared secret (~10 lines, stops stray boards, no key ceremony).
- **Production:** ECDSA **signed-OTA** verification (no permanent eFuse burn needed). Swap is
  confined to this one function.

### 7.3 Trust model (the "stolen host" threat)
Threat: someone obtains a host/seed machine and tries to command the whole fleet.
The crown jewel is **the signing key**, not the hardware. Defences, most important first:

1. **Separate signing from distribution.** The field host only ever handles *already-signed*
   images and does **not** hold the signing key. Signing happens offline on a secured
   machine, ideally in a **hardware token** (YubiKey / secure element / HSM) where the key
   never leaves the chip. Stealing the host then yields only public keys + old images.
2. **Per-family keys.** Each deployment/family has its own key, so a compromise is contained
   to that family, not the entire installed base.
3. **Anti-rollback** (monotonic version, refuse older) blocks the replay/downgrade attack —
   a stolen host with old signed images cannot even push them.
4. **Detection.** The fleet version-map dashboard makes an anomalous/hostile version visible.

**Note:** HMAC (shared secret) does **not** provide defence #1 — the secret sits on every
board, so cracking one board leaks the family key. **Asymmetric signatures (ECDSA)** are the
property that actually survives physical compromise (nodes can *verify* but not *forge*). So
the moment "attacker gets hardware" is in the threat model, move the seam to ECDSA.

### 7.4 Distribution open, authorization narrow
Unifying principle for both the stolen-host worry and the "any board can be a host" idea:

> **Distribution is open** (any host, any board, any wire may hand out firmware).
> **Authorization is narrow** (only firmware signed by the offline key is accepted).
> The **signature**, not control of the hardware, is the fence.

Consequence — **UART seed intake as a safe feature:** the immutable recovery program can
accept a signed image over **UART** as well as ESP-NOW. So any board can be turned into a
seed by wiring in and pushing a signed image; it then spreads wirelessly. Safe *because*
unsigned images are rejected fleet-wide — the wire grants *distribution*, never
*authorization*. (Requires the ECDSA seam to be meaningful against theft; under HMAC it is
convenience only.)

### 7.5 Anti-brick guards (all on)
- **Auto-rollback** — unhealthy fresh image → fall back to recovery, heal over air.
- **Anti-rollback** — never accept an older version number.
- **Chip-ID guard** — refuse an image not built for ESP32-C6.

## 8. Host software

### 8.1 Firmware registry / history
Every version ever built is stored on the host: `version`, `hash`, `date`, `size`, notes.
Browsable. **Re-seed any version** on demand — a re-release is **re-stamped as a new higher
version** (e.g. "v10 = re-release of v7") so anti-rollback stays intact while still letting
you put old code back on the fleet.

### 8.2 Seeding
`seed.ps1 -Port COMx` flashes one node over USB with the current build; it spreads from
there. Any node can be seeded (no designated seed).

### 8.3 Live version-map dashboard
A live map of which node runs which version. Fed by a **gateway node**: any USB-connected
node already hears every neighbour's beacon, so it forwards `{node, version}` sightings to
the host, which reconstructs the whole fleet's version map. The dashboard also hosts the
registry/history UI (§8.1). Cost: a small Node serial→browser bridge (the only Node in an
otherwise PowerShell toolchain).

## 9. Observability
- **LED** encodes version (e.g. blink count = version number) — watch nodes visibly "flip"
  as the wave passes.
- **Serial log** — structured events: `VERSION`, `HEARD`, `PULL_START`, `PULL_DONE`,
  `INSTALL`, `ROLLBACK`.
- **Dashboard** — fleet-wide live version-map + history (§8.3).

## 10. Repository layout

```
/firmware          ESP-IDF C project
  /main            state machine, esp-now transport, ota writer, verify() seam, led
  /recovery        the immutable recovery program (ears + parachute)
  partitions.csv   recovery + ota_0 + nvs + otadata layout
/scripts           build.ps1, seed.ps1, monitor.ps1
/dashboard         live version-map + firmware registry (static page + node serial bridge)
/docs              this design, security model, "grafting into CSI later"
README.md          framed clearly as an authorized OTA/provisioning tool
LICENSE
```

## 11. Tooling
- `build.ps1` — build image, stamp a monotonically increasing version.
- `seed.ps1 -Port COMx` — flash one node (the seed).
- `monitor.ps1 -Port COMx` — tail a node's serial log.
- `dashboard/` — optional live map + registry (static page + tiny Node serial bridge).

## 12. Decisions log
- Standalone spreader first; graft into CSI firmware later. ✔
- Transport: **ESP-NOW** (most accessible: no router/password, coexists with WiFi, already
  the mesh substrate). ✔
- Propagation: **automatic epidemic** + Trickle. ✔
- Safety storage: **recovery image** (1 full app + small immutable recovery), not two full
  slots — fits 2 MB and survives a radio-killing update. ✔
- Integrity always-on (SHA-256); authenticity via pluggable `verify_image()`
  (HMAC now → ECDSA later). ✔
- History on the **host** registry, not on nodes. ✔
- Observability: LED + serial + host dashboard (gateway-node fed). ✔
- Tooling: PowerShell-first; a small Node bridge only for the dashboard. ✔
- Repo: **Hivemind-byNikitaP**, **public**, sole authorship (no AI co-author trailer). ✔

## 13. Open questions
- Dashboard in v1, or immediately after the core spreading works? (Leaning: core first.)
- Exact recovery-partition size vs real app size once the CSI app is the payload
  (measured during the graft phase, not now).
- LED version encoding detail (blink-count vs colour) — cosmetic, decide at build time.
