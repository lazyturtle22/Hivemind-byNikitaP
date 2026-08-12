# Hivemind Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the standalone self-spreading ESP32-C6 firmware — flash one board with a newer signed version and it propagates board-to-board over ESP-NOW, verified and un-brickable.

**Architecture:** Pure, hardware-independent decision logic (version policy, beacon framing, Trickle timing, chunk accounting, verify seam) lives in a dependency-free `hive_core` component that is unit-tested on the host (ESP-IDF `linux` target, Unity, run inside the `espressif/idf:release-v5.2` Docker image — mirroring the Wifi-vision CI). Thin hardware adapters (ESP-NOW, OTA, LED) live in `main` and are verified on real boards. The app is a state machine: **beacon → hear newer → pull chunks → verify → install → confirm-or-rollback → beacon**.

**Tech Stack:** ESP-IDF v5.2.x · target `esp32c6` · C11 · ESP-NOW · esp_ota · mbedTLS (SHA-256 + HMAC) · Unity (host tests) · Docker (`espressif/idf:release-v5.2`).

## Global Constraints

- ESP-IDF **v5.2.x** only — newer IDF changed the C6 wifi APIs (matches Wifi-vision). One line: `idf.py set-target esp32c6`.
- Pure logic in `components/hive_core` has **zero** ESP-IDF / hardware dependencies — it must compile and unit-test on the `linux` target.
- Crypto reaches `hive_core` only through an injected `hive_crypto_t` vtable — `hive_core` never calls mbedTLS directly (keeps it host-testable and makes the HMAC→ECDSA swap a one-vtable change).
- Authenticity for v1 is **HMAC** over a shared secret; the seam is designed for an ECDSA swap later. Integrity (**SHA-256**) is always on.
- Anti-rollback: a node accepts an image **only if `incoming_version > current_version`** (strictly newer).
- Chip guard: reject any image whose `chip_id != HIVE_CHIP_C6`.
- ESP-NOW payload cap is **250 bytes** — every frame packer must assert it fits.
- MVP (Tasks 0–11) uses a **two-OTA-slot** layout (`ota_0`/`ota_1`) — the tiny spreader fits two full copies, giving instant full-app rollback. The **recovery-image** layout (design §6) is introduced in Task 12; it is the model required when grafting into the large CSI firmware (where two full slots do *not* fit 2 MB). This is a conscious, documented sequencing choice, not a deviation.
- Commits are **sole-authored** — never add a Claude/AI co-author trailer (repo owner is building their GitHub profile).
- Secret material (HMAC key) is **never committed** — it lives in `sdkconfig` (gitignored) / NVS, referenced by name only.

---

## File Structure

```
firmware/
  CMakeLists.txt                      # top-level IDF project
  partitions_two_ota.csv              # MVP: nvs, otadata, ota_0, ota_1
  partitions_recovery.csv             # Task 12: nvs, otadata, factory(recovery), ota_0
  sdkconfig.defaults.esp32c6          # target overlay (USB-Serial-JTAG, ESP-NOW)
  components/
    hive_core/                        # PURE, host-testable, zero esp deps
      CMakeLists.txt
      include/hive_chip.h
      include/hive_version.h
      include/hive_beacon.h
      include/hive_trickle.h
      include/hive_chunker.h
      include/hive_verify.h
      hive_version.c
      hive_beacon.c
      hive_trickle.c
      hive_chunker.c
      hive_verify.c
  main/                               # HARDWARE glue
    CMakeLists.txt
    app_main.c                        # state machine wiring
    hive_transport.c / .h             # ESP-NOW init + beacon/req/data TX-RX
    hive_ota.c / .h                   # esp_ota write/verify/boot/confirm/rollback
    hive_crypto.c / .h                # mbedTLS impl of hive_crypto_t (sha256 + HMAC)
    hive_led.c / .h                   # version -> blink
host_test/                            # ESP-IDF project, target=linux, runs Unity
  CMakeLists.txt
  main/CMakeLists.txt
  main/host_test_main.c               # UNITY_BEGIN/RUN_TEST/UNITY_END
  main/test_version.c
  main/test_beacon.c
  main/test_trickle.c
  main/test_chunker.c
  main/test_verify.c
test_scripts/
  run_host_tests.ps1 / .sh            # docker run … idf.py -T linux build + run
  flash.ps1                           # build once, esptool per COM port
```

---

### Task 0: Project scaffold + host-test harness + boots on a C6

**Files:**
- Create: `firmware/CMakeLists.txt`, `firmware/partitions_two_ota.csv`, `firmware/sdkconfig.defaults.esp32c6`, `firmware/main/CMakeLists.txt`, `firmware/main/app_main.c`
- Create: `firmware/components/hive_core/CMakeLists.txt`, `firmware/components/hive_core/include/hive_chip.h`, `firmware/components/hive_core/hive_version.c` (stub so the component builds)
- Create: `host_test/CMakeLists.txt`, `host_test/main/CMakeLists.txt`, `host_test/main/host_test_main.c`
- Create: `test_scripts/run_host_tests.sh`, `test_scripts/run_host_tests.ps1`

**Interfaces:**
- Produces: a buildable IDF project that boots and prints `HIVE boot vN` on a C6; a host-test project that runs Unity on the `linux` target and exits 0 with "0 Failures".

- [ ] **Step 1: Write the top-level project files**

`firmware/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hivemind)
```

`firmware/partitions_two_ota.csv` (2 MB flash, two OTA slots):
```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000
otadata,  data, ota,     0xf000,  0x2000
phy_init, data, phy,     0x11000, 0x1000
ota_0,    app,  ota_0,   0x20000, 0xE0000
ota_1,    app,  ota_1,   0x100000,0xE0000
```

`firmware/sdkconfig.defaults.esp32c6`:
```
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_two_ota.csv"
CONFIG_ESP_WIFI_ENABLED=y
CONFIG_ESP_TASK_WDT_INIT=y
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

`firmware/main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "app_main.c" INCLUDE_DIRS "." REQUIRES hive_core nvs_flash esp_wifi app_update esp_timer mbedtls)
```

`firmware/main/app_main.c` (minimal boot):
```c
#include <stdio.h>
#include "esp_log.h"
#include "hive_version.h"
static const char *TAG = "hive";
void app_main(void) {
    ESP_LOGI(TAG, "HIVE boot v%lu", (unsigned long)HIVE_APP_VERSION);
}
```

- [ ] **Step 2: Write the hive_core component skeleton**

`firmware/components/hive_core/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "hive_version.c" INCLUDE_DIRS "include")
```

`firmware/components/hive_core/include/hive_chip.h`:
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define HIVE_CHIP_C6 1
#ifndef HIVE_APP_VERSION
#define HIVE_APP_VERSION 1u   /* overridden at build time by flash.ps1 */
#endif
static inline bool hive_chip_matches(uint8_t chip_id) { return chip_id == HIVE_CHIP_C6; }
```

`firmware/components/hive_core/hive_version.c`:
```c
/* real logic arrives in Task 1; file exists so the component links */
```

- [ ] **Step 3: Write the host-test harness (linux target)**

`host_test/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
set(COMPONENTS main)
list(APPEND EXTRA_COMPONENT_DIRS "../firmware/components/hive_core")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(host_test)
```

`host_test/main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "host_test_main.c"
    INCLUDE_DIRS "." REQUIRES hive_core unity)
```

`host_test/main/host_test_main.c`:
```c
#include "unity.h"
void app_main(void) {
    UNITY_BEGIN();
    /* RUN_TEST lines added per module in later tasks */
    UNITY_END();
}
```

- [ ] **Step 4: Write the host-test runner scripts**

`test_scripts/run_host_tests.sh`:
```bash
#!/usr/bin/env bash
set -e
docker run --rm -v "$(pwd)":/p -w /p/host_test espressif/idf:release-v5.2 \
  bash -c "idf.py --preview set-target linux && idf.py build && ./build/host_test.elf"
```

`test_scripts/run_host_tests.ps1`:
```powershell
docker run --rm -v "${PWD}:/p" -w /p/host_test espressif/idf:release-v5.2 `
  bash -c "idf.py --preview set-target linux && idf.py build && ./build/host_test.elf"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

- [ ] **Step 5: Run the host tests to prove the harness works**

Run: `bash test_scripts/run_host_tests.sh`
Expected: builds, runs, prints `0 Tests 0 Failures 0 Ignored` and `OK`, exit 0.

- [ ] **Step 6: Build + flash the firmware to one C6 to prove it boots**

Run:
```bash
cd firmware && idf.py set-target esp32c6 && idf.py -p COM20 flash monitor
```
Expected: serial prints `HIVE boot v1`. (Ctrl-`]` to exit monitor.)

- [ ] **Step 7: Commit**
```bash
git add firmware host_test test_scripts
git commit -m "scaffold: hive_core component, host-test harness, boots on C6"
```

---

### Task 1: Version policy (anti-rollback)

**Files:**
- Modify: `firmware/components/hive_core/hive_version.c`
- Create: `firmware/components/hive_core/include/hive_version.h`
- Create: `host_test/main/test_version.c`
- Modify: `host_test/main/host_test_main.c` (register tests), `host_test/main/CMakeLists.txt` (add src)

**Interfaces:**
- Produces: `bool hive_version_should_accept(uint32_t current, uint32_t incoming);` — true iff `incoming > current`.

- [ ] **Step 1: Write the failing test**

`host_test/main/test_version.c`:
```c
#include "unity.h"
#include "hive_version.h"
void test_accepts_strictly_newer(void)   { TEST_ASSERT_TRUE(hive_version_should_accept(1, 2)); }
void test_rejects_same_version(void)      { TEST_ASSERT_FALSE(hive_version_should_accept(2, 2)); }
void test_rejects_older_version(void)     { TEST_ASSERT_FALSE(hive_version_should_accept(5, 3)); }
void test_accepts_big_jump(void)          { TEST_ASSERT_TRUE(hive_version_should_accept(1, 100)); }
```

Add to `host_test/main/host_test_main.c` inside `app_main`, between BEGIN/END:
```c
RUN_TEST(test_accepts_strictly_newer);
RUN_TEST(test_rejects_same_version);
RUN_TEST(test_rejects_older_version);
RUN_TEST(test_accepts_big_jump);
```
Add `test_version.c` to `host_test/main/CMakeLists.txt` SRCS.

- [ ] **Step 2: Run to verify it fails**

Run: `bash test_scripts/run_host_tests.sh`
Expected: FAIL — `undefined reference to 'hive_version_should_accept'`.

- [ ] **Step 3: Write minimal implementation**

`firmware/components/hive_core/include/hive_version.h`:
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
bool hive_version_should_accept(uint32_t current, uint32_t incoming);
```
`firmware/components/hive_core/hive_version.c`:
```c
#include "hive_version.h"
bool hive_version_should_accept(uint32_t current, uint32_t incoming) {
    return incoming > current;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `bash test_scripts/run_host_tests.sh`
Expected: `4 Tests 0 Failures`.

- [ ] **Step 5: Commit**
```bash
git add firmware/components/hive_core host_test
git commit -m "feat(core): version anti-rollback policy"
```

---

### Task 2: Beacon / request / data frame packing

**Files:**
- Create: `firmware/components/hive_core/include/hive_beacon.h`, `firmware/components/hive_core/hive_beacon.c`, `host_test/main/test_beacon.c`
- Modify: `firmware/components/hive_core/CMakeLists.txt`, `host_test/main/CMakeLists.txt`, `host_test/main/host_test_main.c`

**Interfaces:**
- Produces:
  - `hive_beacon_t` (magic, type, chip_id, version, chunk_count, chunk_size, sha256[32]).
  - `size_t hive_beacon_pack(const hive_beacon_t*, uint8_t* out, size_t cap);` → bytes written, 0 on overflow.
  - `bool hive_beacon_unpack(const uint8_t* in, size_t len, hive_beacon_t* out);` → false on bad magic/short.
  - `#define HIVE_MAGIC 0x48495645`, enum `HIVE_MSG_BEACON=1, HIVE_MSG_REQ=2, HIVE_MSG_DATA=3`.

- [ ] **Step 1: Write the failing test**

`host_test/main/test_beacon.c`:
```c
#include "unity.h"
#include <string.h>
#include "hive_beacon.h"
void test_beacon_roundtrip(void) {
    hive_beacon_t b = { .magic=HIVE_MAGIC, .type=HIVE_MSG_BEACON, .chip_id=1,
                        .version=7, .chunk_count=42, .chunk_size=200 };
    for (int i=0;i<32;i++) b.sha256[i]=(uint8_t)i;
    uint8_t buf[250];
    size_t n = hive_beacon_pack(&b, buf, sizeof buf);
    TEST_ASSERT_TRUE(n > 0 && n <= 250);
    hive_beacon_t out;
    TEST_ASSERT_TRUE(hive_beacon_unpack(buf, n, &out));
    TEST_ASSERT_EQUAL_UINT32(7, out.version);
    TEST_ASSERT_EQUAL_UINT16(42, out.chunk_count);
    TEST_ASSERT_EQUAL_UINT16(200, out.chunk_size);
    TEST_ASSERT_EQUAL_UINT8(1, out.chip_id);
    TEST_ASSERT_EQUAL_MEMORY(b.sha256, out.sha256, 32);
}
void test_beacon_rejects_bad_magic(void) {
    uint8_t buf[64]={0}; hive_beacon_t out;
    TEST_ASSERT_FALSE(hive_beacon_unpack(buf, sizeof buf, &out));
}
void test_beacon_rejects_short(void) {
    uint8_t buf[4]={0}; hive_beacon_t out;
    TEST_ASSERT_FALSE(hive_beacon_unpack(buf, 3, &out));
}
```
Register the three `RUN_TEST(...)` lines; add `test_beacon.c` to CMake SRCS; add `hive_beacon.c` to the component SRCS.

- [ ] **Step 2: Run to verify it fails**

Run: `bash test_scripts/run_host_tests.sh` → FAIL (`hive_beacon_pack` undefined).

- [ ] **Step 3: Write minimal implementation**

`firmware/components/hive_core/include/hive_beacon.h`:
```c
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define HIVE_MAGIC 0x48495645u   /* "HIVE" */
enum { HIVE_MSG_BEACON=1, HIVE_MSG_REQ=2, HIVE_MSG_DATA=3 };
typedef struct {
    uint32_t magic; uint8_t type; uint8_t chip_id;
    uint32_t version; uint16_t chunk_count; uint16_t chunk_size;
    uint8_t sha256[32];
} hive_beacon_t;
#define HIVE_BEACON_WIRE_LEN 46  /* 4+1+1+4+2+2+32 */
size_t hive_beacon_pack(const hive_beacon_t* b, uint8_t* out, size_t cap);
bool   hive_beacon_unpack(const uint8_t* in, size_t len, hive_beacon_t* out);
```
`firmware/components/hive_core/hive_beacon.c`:
```c
#include "hive_beacon.h"
#include <string.h>
static void wr32(uint8_t*p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static void wr16(uint8_t*p,uint16_t v){p[0]=v;p[1]=v>>8;}
static uint32_t rd32(const uint8_t*p){return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t rd16(const uint8_t*p){return p[0]|p[1]<<8;}
size_t hive_beacon_pack(const hive_beacon_t* b, uint8_t* out, size_t cap){
    if (cap < HIVE_BEACON_WIRE_LEN) return 0;
    uint8_t*p=out; wr32(p,b->magic);p+=4; *p++=b->type; *p++=b->chip_id;
    wr32(p,b->version);p+=4; wr16(p,b->chunk_count);p+=2; wr16(p,b->chunk_size);p+=2;
    memcpy(p,b->sha256,32);p+=32;
    return (size_t)(p-out);
}
bool hive_beacon_unpack(const uint8_t* in, size_t len, hive_beacon_t* out){
    if (len < HIVE_BEACON_WIRE_LEN) return false;
    if (rd32(in) != HIVE_MAGIC) return false;
    const uint8_t*p=in; out->magic=rd32(p);p+=4; out->type=*p++; out->chip_id=*p++;
    out->version=rd32(p);p+=4; out->chunk_count=rd16(p);p+=2; out->chunk_size=rd16(p);p+=2;
    memcpy(out->sha256,p,32);
    return true;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `bash test_scripts/run_host_tests.sh` → all beacon tests pass.

- [ ] **Step 5: Commit**
```bash
git add firmware/components/hive_core host_test
git commit -m "feat(core): beacon frame pack/unpack with magic + bounds"
```

---

### Task 3: Trickle timing (self-quieting beacons)

**Files:**
- Create: `firmware/components/hive_core/include/hive_trickle.h`, `firmware/components/hive_core/hive_trickle.c`, `host_test/main/test_trickle.c`
- Modify: component + host_test CMake, `host_test_main.c`

**Interfaces:**
- Produces:
  - `trickle_cfg_t { uint32_t imin_ms, imax_ms; uint8_t k; }`
  - `trickle_t` state.
  - `void trickle_init(trickle_t*, const trickle_cfg_t*);` (interval = imin, counter = 0)
  - `void trickle_hear_consistent(trickle_t*);` (counter++)
  - `void trickle_hear_inconsistent(trickle_t*);` (interval = imin, counter = 0)
  - `bool trickle_should_transmit(const trickle_t*);` (counter < k)
  - `uint32_t trickle_next_interval(trickle_t*);` (double, cap at imax, reset counter=0, returns new interval)

- [ ] **Step 1: Write the failing test**

`host_test/main/test_trickle.c`:
```c
#include "unity.h"
#include "hive_trickle.h"
static trickle_cfg_t CFG = { .imin_ms=100, .imax_ms=800, .k=2 };
void test_trickle_starts_at_imin(void){
    trickle_t t; trickle_init(&t,&CFG);
    TEST_ASSERT_EQUAL_UINT32(100, t.interval_ms);
    TEST_ASSERT_TRUE(trickle_should_transmit(&t)); /* counter 0 < k 2 */
}
void test_trickle_suppresses_when_k_consistent(void){
    trickle_t t; trickle_init(&t,&CFG);
    trickle_hear_consistent(&t); trickle_hear_consistent(&t); /* counter=2 == k */
    TEST_ASSERT_FALSE(trickle_should_transmit(&t));
}
void test_trickle_doubles_and_caps(void){
    trickle_t t; trickle_init(&t,&CFG);
    TEST_ASSERT_EQUAL_UINT32(200, trickle_next_interval(&t));
    TEST_ASSERT_EQUAL_UINT32(400, trickle_next_interval(&t));
    TEST_ASSERT_EQUAL_UINT32(800, trickle_next_interval(&t));
    TEST_ASSERT_EQUAL_UINT32(800, trickle_next_interval(&t)); /* capped */
}
void test_trickle_inconsistent_resets_to_imin(void){
    trickle_t t; trickle_init(&t,&CFG);
    trickle_next_interval(&t); trickle_next_interval(&t); /* now 400 */
    trickle_hear_inconsistent(&t);
    TEST_ASSERT_EQUAL_UINT32(100, t.interval_ms);
    TEST_ASSERT_TRUE(trickle_should_transmit(&t));
}
```
Register the 4 tests; wire CMake.

- [ ] **Step 2: Run to verify it fails** — `bash test_scripts/run_host_tests.sh` → FAIL (undefined).

- [ ] **Step 3: Write minimal implementation**

`include/hive_trickle.h`:
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct { uint32_t imin_ms, imax_ms; uint8_t k; } trickle_cfg_t;
typedef struct { trickle_cfg_t cfg; uint32_t interval_ms; uint8_t counter; } trickle_t;
void trickle_init(trickle_t*, const trickle_cfg_t*);
void trickle_hear_consistent(trickle_t*);
void trickle_hear_inconsistent(trickle_t*);
bool trickle_should_transmit(const trickle_t*);
uint32_t trickle_next_interval(trickle_t*);
```
`hive_trickle.c`:
```c
#include "hive_trickle.h"
void trickle_init(trickle_t* t, const trickle_cfg_t* c){ t->cfg=*c; t->interval_ms=c->imin_ms; t->counter=0; }
void trickle_hear_consistent(trickle_t* t){ if (t->counter < 255) t->counter++; }
void trickle_hear_inconsistent(trickle_t* t){ t->interval_ms=t->cfg.imin_ms; t->counter=0; }
bool trickle_should_transmit(const trickle_t* t){ return t->counter < t->cfg.k; }
uint32_t trickle_next_interval(trickle_t* t){
    uint32_t n = t->interval_ms * 2u;
    if (n > t->cfg.imax_ms) n = t->cfg.imax_ms;
    t->interval_ms = n; t->counter = 0; return n;
}
```

- [ ] **Step 4: Run to verify it passes** — all trickle tests pass.

- [ ] **Step 5: Commit**
```bash
git add firmware/components/hive_core host_test
git commit -m "feat(core): trickle interval state machine"
```

---

### Task 4: Chunk accounting (transfer bookkeeping)

**Files:**
- Create: `include/hive_chunker.h`, `hive_chunker.c`, `host_test/main/test_chunker.c`
- Modify: CMake, `host_test_main.c`

**Interfaces:**
- Produces:
  - `uint16_t hive_chunk_count(uint32_t image_size, uint16_t chunk_size);`
  - `chunker_t` with `#define HIVE_MAX_CHUNKS 8192` bitmap.
  - `void chunker_init(chunker_t*, uint16_t count);`
  - `void chunker_mark(chunker_t*, uint16_t idx);`
  - `bool chunker_has(const chunker_t*, uint16_t idx);`
  - `bool chunker_complete(const chunker_t*);`
  - `int chunker_next_missing(const chunker_t*, uint16_t from);` (-1 if none)

- [ ] **Step 1: Write the failing test**

`host_test/main/test_chunker.c`:
```c
#include "unity.h"
#include "hive_chunker.h"
void test_chunk_count_rounds_up(void){
    TEST_ASSERT_EQUAL_UINT16(5, hive_chunk_count(1000, 200));
    TEST_ASSERT_EQUAL_UINT16(6, hive_chunk_count(1001, 200));
    TEST_ASSERT_EQUAL_UINT16(1, hive_chunk_count(1, 200));
}
void test_chunker_marks_and_completes(void){
    chunker_t c; chunker_init(&c, 3);
    TEST_ASSERT_FALSE(chunker_complete(&c));
    TEST_ASSERT_EQUAL_INT(0, chunker_next_missing(&c, 0));
    chunker_mark(&c,0); chunker_mark(&c,2);
    TEST_ASSERT_TRUE(chunker_has(&c,0));
    TEST_ASSERT_FALSE(chunker_has(&c,1));
    TEST_ASSERT_EQUAL_INT(1, chunker_next_missing(&c, 0));
    chunker_mark(&c,1);
    TEST_ASSERT_TRUE(chunker_complete(&c));
    TEST_ASSERT_EQUAL_INT(-1, chunker_next_missing(&c, 0));
}
```
Register tests; wire CMake.

- [ ] **Step 2: Run to verify it fails** — FAIL (undefined).

- [ ] **Step 3: Write minimal implementation**

`include/hive_chunker.h`:
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define HIVE_MAX_CHUNKS 8192
typedef struct { uint16_t count; uint8_t bits[HIVE_MAX_CHUNKS/8]; } chunker_t;
uint16_t hive_chunk_count(uint32_t image_size, uint16_t chunk_size);
void chunker_init(chunker_t*, uint16_t count);
void chunker_mark(chunker_t*, uint16_t idx);
bool chunker_has(const chunker_t*, uint16_t idx);
bool chunker_complete(const chunker_t*);
int  chunker_next_missing(const chunker_t*, uint16_t from);
```
`hive_chunker.c`:
```c
#include "hive_chunker.h"
#include <string.h>
uint16_t hive_chunk_count(uint32_t sz, uint16_t cs){ return cs ? (uint16_t)((sz + cs - 1)/cs) : 0; }
void chunker_init(chunker_t* c, uint16_t n){ c->count = n; memset(c->bits,0,sizeof c->bits); }
void chunker_mark(chunker_t* c, uint16_t i){ if (i < c->count) c->bits[i>>3] |= (uint8_t)(1u << (i&7)); }
bool chunker_has(const chunker_t* c, uint16_t i){ return i < c->count && (c->bits[i>>3] >> (i&7) & 1u); }
bool chunker_complete(const chunker_t* c){
    for (uint16_t i=0;i<c->count;i++) if (!chunker_has(c,i)) return false;
    return c->count > 0;
}
int chunker_next_missing(const chunker_t* c, uint16_t from){
    for (uint16_t i=from;i<c->count;i++) if (!chunker_has(c,i)) return i;
    return -1;
}
```

- [ ] **Step 4: Run to verify it passes** — all chunker tests pass.

- [ ] **Step 5: Commit**
```bash
git add firmware/components/hive_core host_test
git commit -m "feat(core): chunk accounting bitmap"
```

---

### Task 5: The verify seam (integrity + authenticity policy)

**Files:**
- Create: `include/hive_verify.h`, `hive_verify.c`, `host_test/main/test_verify.c`
- Modify: CMake, `host_test_main.c`

**Interfaces:**
- Produces:
  - `hive_verdict_t` enum: `HIVE_OK, HIVE_ERR_MAGIC, HIVE_ERR_CHIP, HIVE_ERR_VERSION, HIVE_ERR_INTEGRITY, HIVE_ERR_AUTH`.
  - `hive_crypto_t { void (*sha256)(const uint8_t*, size_t, uint8_t[32]); bool (*auth_ok)(const uint8_t*, size_t, const hive_beacon_t*, void*); void* ctx; }`.
  - `hive_verdict_t hive_verify_image(const hive_beacon_t* meta, uint32_t current_version, const uint8_t* image, size_t image_len, const hive_crypto_t* crypto);`
- Consumes: `hive_beacon_t` (Task 2), `hive_version_should_accept` (Task 1), `hive_chip_matches` (Task 0).

- [ ] **Step 1: Write the failing test** (mock crypto — no real hashing on host)

`host_test/main/test_verify.c`:
```c
#include "unity.h"
#include <string.h>
#include "hive_verify.h"
/* mock: sha256 = first 32 bytes of image (so tests are deterministic) */
static void mock_sha(const uint8_t* d, size_t n, uint8_t out[32]){
    memset(out,0,32); for(size_t i=0;i<n&&i<32;i++) out[i]=d[i];
}
static bool g_auth = true;
static bool mock_auth(const uint8_t* d, size_t n, const hive_beacon_t* m, void* c){ (void)d;(void)n;(void)m;(void)c; return g_auth; }
static hive_crypto_t CRYPTO = { .sha256=mock_sha, .auth_ok=mock_auth, .ctx=0 };

static hive_beacon_t good_meta(const uint8_t* img, size_t n){
    hive_beacon_t m = { .magic=HIVE_MAGIC, .type=HIVE_MSG_BEACON, .chip_id=1, .version=2 };
    mock_sha(img, n, m.sha256); return m;
}
void test_verify_accepts_good(void){
    uint8_t img[40]; for(int i=0;i<40;i++) img[i]=i;
    hive_beacon_t m = good_meta(img,sizeof img); g_auth=true;
    TEST_ASSERT_EQUAL_INT(HIVE_OK, hive_verify_image(&m, 1, img, sizeof img, &CRYPTO));
}
void test_verify_rejects_bad_magic(void){
    uint8_t img[40]={0}; hive_beacon_t m=good_meta(img,40); m.magic=0;
    TEST_ASSERT_EQUAL_INT(HIVE_ERR_MAGIC, hive_verify_image(&m,1,img,40,&CRYPTO));
}
void test_verify_rejects_wrong_chip(void){
    uint8_t img[40]={0}; hive_beacon_t m=good_meta(img,40); m.chip_id=9;
    TEST_ASSERT_EQUAL_INT(HIVE_ERR_CHIP, hive_verify_image(&m,1,img,40,&CRYPTO));
}
void test_verify_rejects_not_newer(void){
    uint8_t img[40]={0}; hive_beacon_t m=good_meta(img,40); /* version=2 */
    TEST_ASSERT_EQUAL_INT(HIVE_ERR_VERSION, hive_verify_image(&m,2,img,40,&CRYPTO));
}
void test_verify_rejects_bad_hash(void){
    uint8_t img[40]; for(int i=0;i<40;i++) img[i]=i;
    hive_beacon_t m=good_meta(img,40); m.sha256[0]^=0xFF; /* corrupt expected */
    TEST_ASSERT_EQUAL_INT(HIVE_ERR_INTEGRITY, hive_verify_image(&m,1,img,40,&CRYPTO));
}
void test_verify_rejects_bad_auth(void){
    uint8_t img[40]; for(int i=0;i<40;i++) img[i]=i;
    hive_beacon_t m=good_meta(img,40); g_auth=false;
    TEST_ASSERT_EQUAL_INT(HIVE_ERR_AUTH, hive_verify_image(&m,1,img,40,&CRYPTO));
}
```
Register the 6 tests; wire CMake.

- [ ] **Step 2: Run to verify it fails** — FAIL (undefined).

- [ ] **Step 3: Write minimal implementation** (order: magic → chip → version → integrity → auth)

`include/hive_verify.h`:
```c
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "hive_beacon.h"
typedef enum { HIVE_OK=0, HIVE_ERR_MAGIC, HIVE_ERR_CHIP, HIVE_ERR_VERSION,
               HIVE_ERR_INTEGRITY, HIVE_ERR_AUTH } hive_verdict_t;
typedef struct {
    void (*sha256)(const uint8_t* data, size_t len, uint8_t out[32]);
    bool (*auth_ok)(const uint8_t* data, size_t len, const hive_beacon_t* meta, void* ctx);
    void* ctx;
} hive_crypto_t;
hive_verdict_t hive_verify_image(const hive_beacon_t* meta, uint32_t current_version,
                                 const uint8_t* image, size_t image_len,
                                 const hive_crypto_t* crypto);
```
`hive_verify.c`:
```c
#include "hive_verify.h"
#include "hive_version.h"
#include "hive_chip.h"
#include <string.h>
hive_verdict_t hive_verify_image(const hive_beacon_t* m, uint32_t cur,
                                 const uint8_t* img, size_t len, const hive_crypto_t* c){
    if (m->magic != HIVE_MAGIC) return HIVE_ERR_MAGIC;
    if (!hive_chip_matches(m->chip_id)) return HIVE_ERR_CHIP;
    if (!hive_version_should_accept(cur, m->version)) return HIVE_ERR_VERSION;
    uint8_t digest[32]; c->sha256(img, len, digest);
    if (memcmp(digest, m->sha256, 32) != 0) return HIVE_ERR_INTEGRITY;
    if (!c->auth_ok(img, len, m, c->ctx)) return HIVE_ERR_AUTH;
    return HIVE_OK;
}
```

- [ ] **Step 4: Run to verify it passes** — all 6 verify tests pass.

- [ ] **Step 5: Commit**
```bash
git add firmware/components/hive_core host_test
git commit -m "feat(core): verify seam — magic/chip/version/integrity/auth policy"
```

---

### Task 6: mbedTLS crypto implementation (`hive_crypto_t` for the device)

**Files:**
- Create: `firmware/main/hive_crypto.h`, `firmware/main/hive_crypto.c`
- Modify: `firmware/main/CMakeLists.txt` (already REQUIRES mbedtls)

**Interfaces:**
- Consumes: `hive_crypto_t` (Task 5).
- Produces: `const hive_crypto_t* hive_crypto_get(void);` — a singleton wiring `sha256`→`mbedtls_sha256`, `auth_ok`→HMAC-SHA256 over the image compared against a tag the sender appended (see note), keyed by `CONFIG_HIVE_HMAC_KEY`.

**Note (auth for v1):** the sender appends `HMAC-SHA256(key, image)` as a 32-byte trailer conceptually carried alongside the beacon; for the MVP the shared key is a build-time string `CONFIG_HIVE_HMAC_KEY` in the gitignored `sdkconfig`. `auth_ok` recomputes the HMAC over the image and compares. (Design §7.2: this is the seam that later becomes ECDSA.)

- [ ] **Step 1: Write hive_crypto.h**
```c
#pragma once
#include "hive_verify.h"
const hive_crypto_t* hive_crypto_get(void);
```

- [ ] **Step 2: Write hive_crypto.c**
```c
#include "hive_crypto.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include <string.h>
#ifndef CONFIG_HIVE_HMAC_KEY
#define CONFIG_HIVE_HMAC_KEY "dev-only-change-me"
#endif
static void sha(const uint8_t* d, size_t n, uint8_t out[32]){ mbedtls_sha256(d, n, out, 0); }
static bool auth(const uint8_t* d, size_t n, const hive_beacon_t* m, void* ctx){
    (void)ctx;
    uint8_t tag[32];
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    const char* key = CONFIG_HIVE_HMAC_KEY;
    if (mbedtls_md_hmac(info, (const uint8_t*)key, strlen(key), d, n, tag) != 0) return false;
    /* sender put HMAC in the beacon's sha256 field? No — integrity uses sha256.
       For v1 we bind auth to the image hash: require sha256(image)==meta->sha256 (already
       checked) AND that meta->version's low byte matches tag[0]^tag[1] as a lightweight
       shared-secret proof. Replace with a real signature check in the ECDSA task. */
    return true; /* MVP: presence of correct key path; hardened in Task 13/ECDSA */
}
const hive_crypto_t* hive_crypto_get(void){
    static const hive_crypto_t c = { .sha256=sha, .auth_ok=auth, .ctx=0 };
    return &c;
}
```
> Implementer note: the MVP `auth` returns true after exercising the HMAC path; the real
> shared-secret comparison and later ECDSA verification land in the security-hardening task.
> Integrity (SHA-256) is fully enforced now via the verify seam.

- [ ] **Step 3: Build for device to confirm it compiles**

Run: `cd firmware && idf.py set-target esp32c6 && idf.py build`
Expected: build succeeds (links mbedTLS).

- [ ] **Step 4: Commit**
```bash
git add firmware/main/hive_crypto.c firmware/main/hive_crypto.h
git commit -m "feat(device): mbedTLS sha256 + hmac wiring for verify seam"
```

---

### Task 7: LED version indicator

**Files:**
- Create: `firmware/main/hive_led.h`, `firmware/main/hive_led.c`
- Modify: `firmware/main/CMakeLists.txt` (add src; REQUIRES driver esp_timer), `firmware/main/app_main.c`

**Interfaces:**
- Produces: `void hive_led_init(void);` and `void hive_led_show_version(uint32_t version);` (blinks the onboard LED `version` times, then a pause, repeating).

- [ ] **Step 1: Write hive_led.c/.h** (XIAO C6 onboard LED is GPIO15, active-low — adjust to the board)
```c
/* hive_led.h */
#pragma once
#include <stdint.h>
void hive_led_init(void);
void hive_led_show_version(uint32_t version);
```
```c
/* hive_led.c */
#include "hive_led.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define HIVE_LED_GPIO 15
#define ON 0   /* active-low */
#define OFF 1
void hive_led_init(void){
    gpio_reset_pin(HIVE_LED_GPIO);
    gpio_set_direction(HIVE_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(HIVE_LED_GPIO, OFF);
}
void hive_led_show_version(uint32_t version){
    for (uint32_t i=0;i<version;i++){
        gpio_set_level(HIVE_LED_GPIO, ON);  vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_level(HIVE_LED_GPIO, OFF); vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelay(pdMS_TO_TICKS(900));
}
```

- [ ] **Step 2: Call it from app_main** — spawn a task that loops `hive_led_show_version(HIVE_APP_VERSION)`.

- [ ] **Step 3: Flash + verify on hardware**

Run: `cd firmware && idf.py -p COM20 flash monitor`
Expected: onboard LED blinks once (v1), pauses, repeats. **Manual pass criterion:** blink count == version.

- [ ] **Step 4: Commit**
```bash
git add firmware/main/hive_led.c firmware/main/hive_led.h firmware/main/app_main.c firmware/main/CMakeLists.txt
git commit -m "feat(device): LED blinks the running version"
```

---

### Task 8: ESP-NOW transport — two boards hear each other's beacons

**Files:**
- Create: `firmware/main/hive_transport.h`, `firmware/main/hive_transport.c`
- Modify: `firmware/main/CMakeLists.txt` (REQUIRES esp_wifi esp_netif nvs_flash), `app_main.c`

**Interfaces:**
- Consumes: `hive_beacon_t`, `hive_beacon_pack/unpack` (Task 2).
- Produces:
  - `typedef void (*hive_rx_cb_t)(const uint8_t* data, size_t len, const uint8_t src_mac[6]);`
  - `void hive_transport_init(hive_rx_cb_t on_rx);` (nvs, wifi STA no-connect, channel-lock, ESP-NOW init, register broadcast peer)
  - `void hive_transport_broadcast(const uint8_t* data, size_t len);`

- [ ] **Step 1: Write hive_transport.c** (mirror the Wifi-vision ESP-NOW bring-up: STA, `esp_wifi_set_channel`, broadcast peer `FF:FF:FF:FF:FF:FF`, force MCS0 HT20). Recv callback forwards raw bytes + src MAC to `on_rx`.

```c
/* key calls, in order */
nvs_flash_init();
esp_netif_init(); esp_event_loop_create_default();
wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&wc);
esp_wifi_set_mode(WIFI_MODE_STA); esp_wifi_start();
esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
esp_now_init(); esp_now_register_recv_cb(recv_trampoline);
esp_now_peer_info_t p = { .channel=1, .ifidx=WIFI_IF_STA }; memset(p.peer_addr,0xFF,6);
esp_now_add_peer(&p);
/* broadcast: esp_now_send(bcast, data, len); */
```

- [ ] **Step 2: Wire a beacon-sender task in app_main** — every 1 s, pack a `hive_beacon_t{version=HIVE_APP_VERSION}` and `hive_transport_broadcast(...)`. In the rx callback, `hive_beacon_unpack` and `ESP_LOGI("HEARD v%lu from %02x:..%02x")`.

- [ ] **Step 3: Flash TWO boards and verify cross-reception**

Run: `test_scripts/flash.ps1 -Ports COM20,COM21` then monitor each.
Expected: each board logs `HEARD v1 from <other MAC>` at ~1 Hz. **Manual pass criterion:** both boards see each other.

- [ ] **Step 4: Commit**
```bash
git add firmware/main/hive_transport.c firmware/main/hive_transport.h firmware/main/app_main.c firmware/main/CMakeLists.txt
git commit -m "feat(device): ESP-NOW transport + beacon broadcast/receive"
```

---

### Task 9: OTA writer — receive an image and boot into it

**Files:**
- Create: `firmware/main/hive_ota.h`, `firmware/main/hive_ota.c`
- Modify: `firmware/main/CMakeLists.txt` (REQUIRES app_update), `app_main.c`

**Interfaces:**
- Consumes: `hive_verify_image` (Task 5), `hive_crypto_get` (Task 6), `chunker_t` (Task 4).
- Produces:
  - `esp_err_t hive_ota_begin(size_t total);` (esp_ota_begin on the inactive partition)
  - `esp_err_t hive_ota_write_at(uint32_t offset, const uint8_t* data, size_t len);`
  - `esp_err_t hive_ota_finish_and_boot(const hive_beacon_t* meta);` (esp_ota_end, then read back + `hive_verify_image`, then `esp_ota_set_boot_partition`, then `esp_restart`)
  - `uint32_t hive_ota_running_version(void);`

- [ ] **Step 1: Implement `hive_ota.c`** using `esp_ota_get_next_update_partition(NULL)`, `esp_ota_begin/write/end`, then verify the written slot via `esp_partition_read` + `hive_verify_image(meta, running_version, buf, len, hive_crypto_get())`, then `esp_ota_set_boot_partition` + `esp_restart`. Refuse to boot if verdict != `HIVE_OK` (log the verdict).

- [ ] **Step 2: Temporary bench harness** — add a debug command over serial (`node-config`-style) `ota_selftest` that copies the *running* image into the inactive slot via the OTA API and boots it, to exercise write+verify+boot without the network. (Delete in Task 11.)

- [ ] **Step 3: Flash + run the selftest on one board**

Expected: logs `OTA verify=OK`, reboots, comes back up on the other slot. **Manual pass criterion:** survives a reboot into the freshly-written slot.

- [ ] **Step 4: Commit**
```bash
git add firmware/main/hive_ota.c firmware/main/hive_ota.h firmware/main/app_main.c firmware/main/CMakeLists.txt
git commit -m "feat(device): OTA write + read-back verify + boot into new slot"
```

---

### Task 10: Confirm-or-rollback (anti-brick seatbelt)

**Files:**
- Modify: `firmware/main/hive_ota.c`, `firmware/main/app_main.c`

**Interfaces:**
- Produces:
  - `void hive_ota_mark_healthy(void);` (wraps `esp_ota_mark_app_valid_cancel_rollback`)
  - Behavior: on boot, if the running app is `ESP_OTA_IMG_PENDING_VERIFY`, a watchdog-backed timer requires `hive_ota_mark_healthy()` within N seconds or the bootloader rolls back on reset.

- [ ] **Step 1: Implement pending-verify handling** — in `app_main`, call `esp_ota_get_state_partition(running, &state)`; if `PENDING_VERIFY`, start a self-check (ESP-NOW init succeeded + one beacon TX ok) and then `hive_ota_mark_healthy()`. Ensure `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` (Task 0).

- [ ] **Step 2: Negative test on hardware** — build a deliberately-broken image (e.g. `abort()` before `mark_healthy`), flash it as an OTA update to a board, and confirm the bootloader **rolls back** to the previous slot on the next reset.

Expected: board returns to the previous good version after the broken update. **Manual pass criterion:** no brick; auto-recovers.

- [ ] **Step 3: Commit**
```bash
git add firmware/main/hive_ota.c firmware/main/app_main.c
git commit -m "feat(device): confirm-or-rollback with pending-verify self-check"
```

---

### Task 11: State-machine integration — the version wave (MVP win)

**Files:**
- Modify: `firmware/main/app_main.c` (assemble the full loop), `firmware/main/hive_transport.c` (add req/data frames)

**Interfaces:**
- Consumes: everything from Tasks 1–10.
- Produces: the full node loop — beacon (Trickle-timed) → on hearing `version > running` start a pull (send `HIVE_MSG_REQ` for missing chunks; sender answers `HIVE_MSG_DATA`) → assemble via `chunker_t` → `hive_ota_finish_and_boot` → confirm → now beaconing the new version.

- [ ] **Step 1: Implement request/data framing** in `hive_transport` (REQ carries `{version, from_chunk}`; DATA carries `{version, chunk_index, bytes[<=200]}`). A node that has the image serves DATA for chunks it's asked for. A node that hears a newer beacon becomes a receiver: `chunker_init`, loop `chunker_next_missing` → REQ → collect DATA → `hive_ota_write_at`.

- [ ] **Step 2: Remove the `ota_selftest` debug command** from Task 9.

- [ ] **Step 3: Two-board wave test**
  - Flash board A and board B at v1 (`flash.ps1 -Ports COM20,COM21`).
  - Re-flash **only board A** at v2 (`flash.ps1 -Port COM20 -Version 2`).
  - Watch board B: it hears v2, pulls, installs, reboots, and its LED goes from 1 blink → 2 blinks.
  - Expected serial on B: `HEARD v2 … PULL_START … PULL_DONE … OTA verify=OK … HIVE boot v2`.
  - **Manual pass criterion:** B ends on v2 with LED=2 with no cable touched.

- [ ] **Step 4: Three-board relay test**
  - A=v2, B=v1, C=v1, with C out of A's range but in B's range.
  - Expected: A→B→C; C reaches v2 via B. **Manual pass criterion:** epidemic relay works past one hop.

- [ ] **Step 5: Commit**
```bash
git add firmware/main
git commit -m "feat(device): full epidemic state machine — version wave across nodes"
```

---

### Task 12: Recovery-image layout (design §6 model)

**Files:**
- Create: `firmware/partitions_recovery.csv`, `firmware/recovery/` (minimal recovery app: ESP-NOW receive + OTA write into `ota_0` + verify)
- Modify: `firmware/sdkconfig.defaults.esp32c6` (point at the recovery CSV), build config to produce two images.

**Interfaces:**
- Produces: a `factory`(recovery) partition holding an immutable minimal receiver, and `ota_0` holding the spreader. Rollback target becomes the recovery app (not a second full slot).

- [ ] **Step 1:** Write `partitions_recovery.csv`:
```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000
otadata,  data, ota,     0xf000,  0x2000
phy_init, data, phy,     0x11000, 0x1000
factory,  app,  factory, 0x20000, 0x80000
ota_0,    app,  ota_0,   0xA0000, 0xF0000
```
- [ ] **Step 2:** Extract the transport + OTA-write + verify subset into the `recovery/` app (reuse `hive_core` + `hive_transport` + `hive_ota`; no LED choreography, no re-spread). Keep it small and immutable.
- [ ] **Step 3:** Point rollback at `factory` (recovery). Repeat the Task 10 negative test: a broken `ota_0` update drops to the recovery app, which then re-receives a good image over ESP-NOW and re-flashes `ota_0`.
- [ ] **Step 4:** Hardware verification: kill the radio in a bad `ota_0` build → board falls to recovery → recovery's radio works → healed over air.
  **Manual pass criterion:** un-brickable even when the main app breaks the radio.
- [ ] **Step 5: Commit**
```bash
git add firmware/partitions_recovery.csv firmware/recovery firmware/sdkconfig.defaults.esp32c6
git commit -m "feat(device): recovery-image layout — immutable heal-over-air parachute"
```

---

### Task 13: Security hardening — real shared-secret auth (+ ECDSA seam note)

**Files:**
- Modify: `firmware/main/hive_crypto.c` (real HMAC comparison), `firmware/components/hive_core/hive_beacon.c` (carry an auth tag or a detached-signature reference)

**Interfaces:**
- Produces: `auth_ok` that actually verifies an HMAC tag the sender computed over the image with the shared key, rejecting a wrong/absent tag. Documented seam for swapping HMAC → ECDSA (`esp_secure_boot`-style signature verification) later.

- [ ] **Step 1:** Extend the transfer so the sender includes `HMAC-SHA256(key, image)` (32 bytes) delivered with the final DATA/So the receiver can compare. Store the key in gitignored `sdkconfig` as `CONFIG_HIVE_HMAC_KEY`.
- [ ] **Step 2:** Implement the real comparison in `auth_ok`; add a host unit test in `test_verify.c` using a mock `auth_ok` that returns false for a flipped tag (policy already covered — assert `HIVE_ERR_AUTH`).
- [ ] **Step 3:** Negative hardware test: a board flashed with the wrong key must be **unable** to seed the fleet (its images are rejected). **Manual pass criterion:** wrong-key board cannot infect.
- [ ] **Step 4:** Document the ECDSA swap in `docs/design.md` §7.2 (already seam-ready) — no code beyond the note for v1.
- [ ] **Step 5: Commit**
```bash
git add firmware host_test docs/design.md
git commit -m "feat(security): real HMAC auth on the verify seam; document ECDSA swap"
```

---

## Self-Review (author checklist — completed)

**Spec coverage:** §5 OTA mechanics → Tasks 9–10; §6 recovery model → Task 12; §6.3 state machine → Task 11; §6.4 Trickle → Task 3; §7.1 integrity → Task 5/6; §7.2 verify seam → Tasks 5/6/13; §7.5 anti-brick guards → Tasks 1 (anti-rollback), 5 (chip guard), 10 (auto-rollback); observability LED → Task 7; ESP-NOW transport → Task 8. **Console (§8.4) is deliberately out of scope — separate plan.** WiFi fast-path, full hardware Secure Boot, CSI graft, mixed-chip → explicitly deferred per Global Constraints / spec §3.

**Placeholder scan:** hardware tasks use explicit esp-idf call sequences + manual pass criteria (the honest "test" for on-device behavior); pure-logic tasks carry full Unity tests + implementations. The one soft spot — `hive_crypto.c` `auth_ok` returning `true` in the MVP — is called out inline and closed in Task 13; integrity is fully enforced from Task 5.

**Type consistency:** `hive_beacon_t`, `hive_crypto_t`, `hive_verdict_t`, `trickle_t`, `chunker_t`, `hive_version_should_accept`, `hive_verify_image` names are used identically across the tasks that define and consume them.

## Note on testing philosophy

Pure decision logic (the parts that are easy to get subtly wrong — version policy, framing, Trickle, chunk accounting, verify order) is TDD'd on the host in Docker, fast and hardware-free. Hardware behavior (radio, flash, boot, rollback) is verified on real C6 boards with explicit pass criteria, because a green test that never touched flash proves nothing about OTA. This mirrors the Wifi-vision "pure functions + sim, hardware at the edges" split.
