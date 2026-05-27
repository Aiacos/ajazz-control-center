# Phase 9.x — Operator Capture Runbook

**Generated:** 2026-05-27 (hardware-free prep run)
**Status:** OPERATOR ACTION REQUIRED — gates honest `functional`/`partial` promotion of Phases 10–13
**Scope:** The capture-confirmation deliverables deferred from Phase 9's partial-scope
execution (CAPTURE-05, CAPTURE-06, ARCH-04/05/06 finalization).

> **Key reframing (from the 2026-05-27 readiness audit):** Captures **confirm and
> finalize** the provisional wire-format values — they do **not** block execution of
> most Phase 10–13 deliverables. The plans encode every provisional value as a named
> constant (default verdict / `Unresolved`) with MockTransport unit tests asserting the
> wire bytes. So the code + tests can be written hardware-free; what captures buy is the
> **honest maturity ceiling** — a device cannot be promoted to `functional` until the
> three-witness rule (capture + observed behaviour + negative test) is satisfied. Treat
> every value below flagged "DEFAULT VERDICT" / "provisional" as a hypothesis to verify
> against the physical device — **when the RE and the hardware disagree, the hardware
> wins, and the RE doc gets updated** (CLAUDE.md hard rule).

______________________________________________________________________

## Pre-flight (one-time, operator-only — project hard rule: no system mutations from tooling)

These are **your** actions; project tooling will not run them.

- [ ] Install capture stack: `sudo dnf install wireshark tshark` (Fedora) or
  `sudo apt install wireshark tshark dumpcap` (Debian/Ubuntu).
- [ ] Load the USB monitor module: `sudo modprobe usbmon`.
- [ ] (Optional, unprivileged capture) `sudo usermod -aG wireshark $USER` then re-login.
- [ ] Confirm device ACLs: `getfacl /dev/hidraw*` should show your uid `rw`. If a node is
  root-only after a re-enumeration storm (systemd ≥258 `uaccess` regression — see
  CLAUDE.md), recover with a **physical replug**, or transient dev-only
  `sudo setfacl -m u:$(id -u):rw /dev/hidrawN`.
- [ ] Already satisfied (verified 2026-05-27): `qt6-qtbase-private-devel` (`qzipreader_p.h`
  present), capture tooling (`scripts/hex-to-cpparray.py`, `scripts/reject-raw-captures.sh`),
  `docs/protocols/CAPTURING.md`, `docs/policies/capture-data-hygiene.md`, and the
  `.planning/research/captures/.gitignore` + pre-commit `reject-raw-captures` hook.

Follow `docs/protocols/CAPTURING.md` for the per-device `usbmon` filter + sanitisation
runbook. **Never** commit a raw `.pcap`/`.pcapng` (Pitfall 17 — the pre-commit hook will
reject it; sanitise to `std::array<uint8_t>` literals via `hex-to-cpparray.py` first).

______________________________________________________________________

## Per-device capture matrix — each row maps to the plan constant it confirms

### Device A — `akp05e` (`0300:3004`, fw `V3.AKP05E.01.007`) → confirms **Phase 10**

| Capture target                                          | Confirms                                                                                           | Plan / constant                                |
| ------------------------------------------------------- | -------------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| Key-image upload: first + last chunk                    | `PacketSize 512→1024`; `BAT`→chunk→`ULEND` framing; JPEG dims/rotation (64×64 Rot90 vs 60×60 Rot0) | 10-01 (ARCH-04 DEFAULT VERDICT)                |
| `CLE` clear-all                                         | clear opcode wire bytes                                                                            | 10-01                                          |
| `LIG` brightness                                        | brightness opcode wire bytes                                                                       | 10-01 / 16-01 (already shipped, retro-confirm) |
| Encoder rotate (CW/CCW) + press                         | half-step cadence; 16 ms trailing-edge coalescer window; synthesised-release model                 | 10-02 (INPUT-01/02)                            |
| `HAN` handle / held-open                                | poll-loop handle behaviour                                                                         | 10-03 / Phase 14 retro-confirm                 |
| **Negative test:** probe hypothetical `TIM`/time opcode | proves NO RTC → `hasClock=false` honest                                                            | 10-02 (DEVICES-05, ARCH-05)                    |

> Phase 10 verdict: 3/4 plan tasks are buildable + MockTransport-testable now; the
> `AJAZZ_REAL_HARDWARE` 100-image power-cycle smoke (10-03, DISPLAY-04) is the LIVE-HW
> witness that promotes `scaffolded → functional`.

### Device B — `ak980pro` (`0c45:8009`) → confirms **Phase 12**

| Capture target                                                        | Confirms                                                                     | Plan / constant                                  |
| --------------------------------------------------------------------- | ---------------------------------------------------------------------------- | ------------------------------------------------ |
| 20 RGB modes (cmd `0x13`), brightness/speed/direction                 | RGB cmd `0x13` byte layout (incl. direction byte 11); 20-mode table          | 12-02 (ARCH-05 DEFAULT VERDICT, CAPTURE-PENDING) |
| Sleep-timer (cmd `0x17`, 4-packet `0x18`→`0x17`→`0x02`→`0xF0`)        | sleep-timer enum (source says 10-min, capture shows none — resolve honestly) | 12-03 (Pitfall 19)                               |
| RTC round-trip (set time via opcode `0x28`, observe TFT clock widget) | **ARCH-05.1 FINAL** — the real `0x28` firmware RTC; promotes clock witness   | 12-04 (`hasClock=true`, already implemented)     |
| Per-key RGB path: `0x0A` legacy vs `0x20`/`0x04`                      | resolves the off-by-two `0x0A` vs Ghidra-confirmed `0x20/0x04` divergence    | deferred (Phase 12 CR-01)                        |
| `lsusb -v -d 0c45:8009` HID descriptor dump                           | wireless dongle topology; `isWireless=true`                                  | 12-01                                            |
| RGB-transition keystroke-stall observation                            | the `≤10 writes/sec` rate-limiter is sufficient on the 2.4G link             | 12-01 (Pitfall 24)                               |

> Phase 12 verdict: 4/4 plan tasks buildable + MockTransport-testable now; clock is FINAL
> (implemented). Captures confirm RGB byte layout + the wireless-stall behaviour for the
> honest `scaffolded → partial` promotion.

### Device C — `ajazz_24g_8k` (`3151:5007`) → confirms **Phase 11**

| Capture target                                                | Confirms                                                                            | Plan / constant                      |
| ------------------------------------------------------------- | ----------------------------------------------------------------------------------- | ------------------------------------ |
| DPI cycle (full 8-stage walk)                                 | **vendor cycle ORDER** (NOT naive `+1`); `dpi_stages: 8`                            | 11-02 (`kAj24g8kDpiCycleOrder`)      |
| Per-stage DPI (cmd `0x21`) + colour                           | cmd `0x21` byte positions                                                           | 11-02                                |
| LOD (cmd `0x23`)                                              | cmd `0x23` layout; envelope gating                                                  | 11-02 (`kAj24g8kEnvelope`)           |
| Polling-rate dropdown (cmd `0x22`: 1000/2000/4000/8000 Hz)    | cmd `0x22` byte positions; USB 2.0 SOF cap warning                                  | 11-03                                |
| Per-zone RGB (cmd `0x30`)                                     | zone count + colour positions (descriptor-driven)                                   | 11-03                                |
| Battery: `0xF7` status poll + `GET_FEATURE`                   | frame `[00,00,charge,01 01 01 02]`, charge @ idx 2 (Linux)/3 (Win) — **NOT `0x83`** | (verified 2026-05-22; retro-confirm) |
| **AJ199 V1.0 (17-byte OemDrv) vs Max (20-byte HIDUsb) probe** | resolves `kAj24g8kEnvelope` switch-point (default `Unresolved`)                     | 11-01                                |

> Phase 11 verdict: 4/4 plan tasks are HW-FREE (most unblocked of the four). Captures
> resolve the V1.0-vs-Max envelope split and the DPI cycle order; maturity stays `partial`
> until the round-trip witness lands.

### Device D — `microdia_dongle_7016` (`0c45:7016`) → confirms **Phase 13 + ARCH-06**

| Capture target                                                                            | Confirms                                                                     | Plan / constant                  |
| ----------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- | -------------------------------- |
| `lsusb -v -d 0c45:7016` + `udevadm info -a /dev/hidraw{5,6}`                              | topology: separate dongle, NOT a composite iface of AK980 PRO                | 13-01 (ARCH-06 negative verdict) |
| Paired-input identification via `evtest`                                                  | what the dongle actually emits                                               | 13-01                            |
| **ARCH-06 2-minute unplug test:** unplug `ak980pro`, watch whether `0c45:7016` disappears | NO simultaneous disappearance → separate dongle confirmed (dedup NOT firing) | ARCH-06 finalization             |

> Phase 13 has a HW-FREE slice (register the dongle at `probed` tier + doc stub) that can
> ship independently of the four v1.1 UI verifies (those need an operator at a screen).

______________________________________________________________________

## After captures land — finalization steps

- [ ] Run `scripts/hex-to-cpparray.py` per device → `tests/integration/fixtures/<codename>_*.h`
  \+ SHA-256 metadata in `.planning/research/captures/INDEX.md` (CAPTURE-06).
- [ ] Extend `docs/protocols/streamdeck/akp03.md` with `0300:3004` first-party findings.
- [ ] Create `docs/protocols/keyboard/ak980pro.md` + `docs/protocols/mouse/ajazz_24g_8k.md`
  where findings diverge from the OSS-corpus baseline (CAPTURE-06). *(Neither exists yet.)*
- [ ] Flip ARCH-04 / ARCH-05 / ARCH-06 from "DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)"
  to **FINAL** (or amend if contradicted) — update each ADR Status section + the
  PROJECT.md Key Decisions outcome column.
- [ ] For any provisional constant a capture **contradicts**: update the code constant AND
  the RE doc (hardware wins), then re-run the MockTransport wire tests.
- [ ] Re-run `/gsd-plan-phase 9 --gaps` (or a focused Phase 9.x run) to close CAPTURE-05/06
  \+ ARCH finalization as committed plans, then tick the Phase 9 ROADMAP line `[x]`.

## Then, the implementation phases unblock in dependency order

1. **Phase 10** (AKP05E) — canonical promotion template; the `PacketSize 512→1024` fix
   unblocks 13 sibling SKUs.
1. **Phase 11** (8K mouse) — most HW-free; can proceed in parallel with 10.
1. **Phase 12** (AK980 PRO) — clock already FINAL; needs RGB/sleep confirmation.
1. **Phase 13** — catalogue slice anytime; UI verifies after 10 & 12 (so the Sync-button
   visibility flips honestly per each device's real `hasClock`).
1. **Phase 25** — milestone-close gate: full AKP05E UAT + real `.sdPlugin` live. Plans
   reviewed 2026-05-27 (goal-backward): no gaps, no scope creep, ordering correct.
