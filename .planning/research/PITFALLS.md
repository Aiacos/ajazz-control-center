# Pitfalls Research — v1.2 Connected-Device Capability Parity

**Domain:** Brownfield Qt 6 / C++20 / hidapi_hidraw control-center adding real capability impls + USB protocol RE for 4 physically-connected devices
**Researched:** 2026-05-15
**Confidence:** HIGH on repo-anchored claims and load-bearing OSS-corpus references; MEDIUM on Microdia/SONiX-specific wireless-stall claims (no authoritative public docs exist; flagged inline).

This file deliberately does **not** re-state v1.1 pitfalls. Generic Qt/hidapi/Win32 gotchas already catalogued at `.planning/milestones/v1.1-research/PITFALLS.md` (Pitfalls 1-16 there) are referenced by index and not duplicated. The v1.1 cross-cutting "Linux-CI-blind Windows breakage" pitfall and the v1.1 "Looks Done But Isn't" checklist remain in force and apply to v1.2 unchanged.

Numbering continues from v1.1 (which ended at 16) to make cross-milestone references unambiguous: v1.2 pitfalls start at **17**.

______________________________________________________________________

## Critical Pitfalls

### Pitfall 17: USB capture corpus contains user keystrokes / passwords (privacy + security incident)

**What goes wrong:**
`usbmon` (Linux) / Wireshark USBPcap (Windows) captures of the AK980 PRO keyboard during normal operation contain every keypress made while the capture was running. USB HID boot-protocol keyboards emit 8-byte interrupt reports with a modifier byte + up to six keycodes (verified at [HackTricks: USB Keystrokes](https://hacktricks.wiki/en/generic-methodologies-and-resources/basic-forensic-methodology/pcap-inspection/usb-keystrokes.html), [USB-Keyboard-Parser](https://github.com/bolisettynihith/USB-Keyboard-Parser)) — *any* CTF-grade tool reconstructs the keystrokes deterministically. If a researcher commits a `.pcap` to git for replay tests "to capture the moment I pressed Fn+F5 to switch profile," they also commit whatever password / private message / SSH passphrase they happened to type around it. Mouse captures leak click patterns + paste targets; encoder captures leak nothing beyond rotation timing but the encoder capture from a debug session may *coincidentally* contain a keyboard report if both devices share a USB hub captured at the bus level.

**Why it happens:**

- `sudo modprobe usbmon; tcpdump -i usbmon0 -w cap.pcap` captures **all** USB traffic on bus 0, not just the device under test. Filtering at capture time (`-i usbmon3` selects bus 3) requires knowing which bus the device is on; researchers default to "capture everything, filter later" — except "later" is after the commit.
- HID keyboard reports are not encrypted at the HID layer even for 2.4G wireless devices — the dongle decrypts the RF link and presents plaintext HID to the host, so usbmon sees plaintext keycodes regardless of wireless link encryption.
- `.pcap` files are binary and don't surface secrets in plain `git diff` review — a reviewer skimming a PR sees "Added 12 MB capture file" but not "added recoverable password to public repo."
- Branding the file as a "wire-format reference capture" gives a false sense that it's only protocol data.

**How to avoid:**

- **NEVER commit raw `.pcap` files to the repo.** Per CLAUDE.md's "no system-level mutations" + privacy posture, captures live under `.planning/research/captures/` which is **`.gitignore`-d at the directory level**. The directory contains a `README.md` (committed) explaining the layout convention and a `.gitignore` (committed) excluding everything else.
- **Reference captures by SHA-256 hash + short metadata blob** (committed) — not by content. Format: `captures-index.md` with `| sha256 | device | what-was-captured | timestamp | researcher |` rows. The capture itself is shared out-of-band (encrypted scratch, signal attachment, in-person USB stick) when verification is needed.
- **For replay tests, extract the device-of-interest control-channel reports** into a small `.hex` fixture (see existing pattern `tests/integration/fixtures/` + `hex_loader.hpp`). The fixture is plain text, reviewable, and contains *only* the device-control bytes — no keyboard report frames, no timestamps that could fingerprint user activity. Strip:
  - All `URB_INTERRUPT IN` packets from any device that is not the device under test (filter: keep only `usb.device_address == <DUT>`)
  - All boot-protocol reports (filter: `usb.bInterfaceProtocol != 1` to drop keyboard boot)
  - Timestamps to relative-time-from-zero (no wall clock)
- **Capture filter at capture time**, not at replay time: `tcpdump -i usbmon3 -w cap.pcap 'ifindex == <devnum>'` (Linux) or Wireshark USBPcap "Capture from this device only" (Windows). Document the exact filter command in the capture metadata.
- **Pre-commit hook addition:** `*.pcap` / `*.pcapng` extension match → reject the commit with a "captures live in `.planning/research/captures/` (gitignored)" message. Pair with the existing `lfs` check.

**Warning signs:**

- A PR adds files in `tests/fixtures/` or anywhere under git tracking with `.pcap` / `.pcapng` suffix.
- A binary blob >1 MB lands in a commit whose message references "capture" or "trace."
- A researcher proposes "let me just commit the capture so others can repro" — that proposal is itself the warning sign.

**Phase to address:**
**Phase 9 (Research) deliverable: capture-data-hygiene policy doc + `.planning/research/captures/.gitignore` + pre-commit hook.** Must land BEFORE any researcher does their first capture, not after. Plan slug: `09-01-capture-data-hygiene-policy`.

______________________________________________________________________

### Pitfall 18: AKP03 image-upload chunking — wrong chunk size hangs the device until power-cycle

**What goes wrong:**
The AKP03 wire protocol uses 1024-byte HID output reports for image chunks (`docs/protocols/streamdeck/akp03.md:127` — "1024-byte packets (`is_v2_api` in `[ajazz-sdk]`)"). The Elgato Stream Deck family does the same — verified at [Elgato HID API: General Reference](https://docs.elgato.com/streamdeck/hid/general/): *"The maximum size of an output report is 1024 bytes. ... The image file must be split into chunks defined by the output report's payload."* Last-chunk semantics: [Elgato HID API: Module 15/32](https://docs.elgato.com/streamdeck/hid/module-15_32/): *"The output report of the last chunk should be marked with 0x01 in the Transfer is Done flag; otherwise, it must be zero."* If a naive implementation sends a 512-byte chunk (because they copy-pasted from the v1-API AKP153 backend, which uses 512-byte v1 packets per the cross-reference at `docs/protocols/streamdeck/akp03.md:125`), the device's firmware accumulates bytes but never sees the expected report-size boundary — it hangs waiting for the rest of the chunk. Subsequent `hid_write` calls return success (queued in hidapi) but the device LCD stays frozen on the last successfully-uploaded image until power-cycle (USB unplug). Worse, on Linux some `hidraw` backends will block the application thread on the next write because the device's IN endpoint queue is full.

**Why it happens:**

- The AKP03 backend shares `akp03.cpp` family code with `akp05e` (`mirabox_n3`, `akp03e`, etc.) — see `docs/_data/devices.yaml:264-274`. A bug in chunk size affects all 6 family members.
- The "Transfer is Done" flag at the last chunk is easy to forget — it's a single byte change relative to non-terminal chunks. The chunked-upload helper in [python-elgato-streamdeck (StreamDeck.py)](https://github.com/abcminiuser/python-elgato-streamdeck/blob/master/src/StreamDeck/Devices/StreamDeck.py) makes this explicit; a hand-rolled C++ version may not.
- AKP03R rev. 2 uses 64×64 `Rot90` images (`docs/protocols/streamdeck/akp03.md:178`) while AKP03 / AKP03E / AKP03R use 60×60 `Rot0` — wrong image dimensions cause the chunk count to mismatch the firmware's expectation, surfacing the same hang shape.
- The encoder of the JPEG (Pillow default vs `libjpeg-turbo` default) affects compressed size — too-large images can exceed the firmware's per-key buffer and silently truncate. Image-side validation belongs in the backend, not the QML layer.

**How to avoid:**

- **Always** send exactly 1024-byte HID output reports for AKP03 family image chunks. Wrap in `Akp03Display::sendChunk(std::span<const uint8_t, 1024> chunk, bool isLast)` so the signature itself enforces the size. The `bool isLast` parameter sets the Transfer-is-Done flag — caller cannot forget it.
- **Always** size-validate the encoded JPEG before chunking. Per-key budget for AKP03: 60×60 JPEG quality ~85 ≈ 1-3 KB; reject (with `Result::ImageTooLarge`) if encoded image exceeds 8 KB. AKP03R rev. 2: 64×64 Rot90 ≈ 1-3 KB; same cap.
- **Always** emit a `Result::UploadStalled` after a `hid_write` returns -1 (or after `hid_write_timeout` times out) and surface it to the user via the toast/glyph (D-02 honesty contract from v1.1 Phase 5). Do NOT retry blindly — retrying on a hung device makes the hang worse.
- **Test pattern:** every AKP03-family image-upload code path has a Catch2 TEST_CASE using `MockHidTransport` that asserts:
  1. Total bytes written equals `ceil(payload_size / 1024) * 1024` (chunk-size invariant);
  1. The last chunk has the "Transfer is Done" flag = 0x01; all prior chunks = 0x00;
  1. Chunk index field is sequential from 0;
  1. A simulated `-1` return from chunk N halts the upload (no further `hid_write` calls).
- **Real-hardware guard:** before promoting an AKP03 family member to `functional`, run the manual upload + power-cycle test (upload 100 distinct images in sequence, replug, upload 100 more). If the device hangs once, the chunking is wrong — do NOT promote.

**Warning signs:**

- LCD freezes on the last-uploaded image; `hid_write` returns success forever (queued).
- First image after a fresh plug-in works; the second hangs. (First upload happens before any pending-chunk state in the firmware; second one triggers it.)
- `udevadm monitor` shows the device staying enumerated but unresponsive.
- Power-cycle "fixes" it. (Every hang fixed by replug = firmware state machine wedged = chunking bug.)

**Phase to address:**
**Phase 10 (AKP03 display+encoder capability impl)**. Pre-condition: Phase 9 capture annotated with confirmed chunk size + last-chunk flag bit position for variant `0x3004`. Promotion gate: real-hardware power-cycle test green.

______________________________________________________________________

### Pitfall 19: `clock` capability false-positive — shipping a "time-sync works" lie

**What goes wrong:**
A researcher annotating a `usbmon` capture from the native AJAZZ app sees a byte sequence that *looks* like time:

```
CRT...TIM 65 e8 18 67 ...   ← could be 1985-... no wait, 2024-...
```

— a 4-byte little-endian Unix-ish epoch in a packet whose 3-byte ASCII command word is `TIM` (or any other plausible mnemonic). They conclude "AKP05E 0x3004 supports clock!" and implement `Akp03Device::setTime` to send that byte sequence. The device accepts the packet (firmware doesn't STALL — it just discards unknown commands or treats them as a brightness write that happens to compute a no-op for in-range values). User sees the per-row glyph flip from exclamation to ok. UI now lies: device clock is *not* being set; the per-row glyph claims it is. PROJECT.md's explicit "no AJAZZ device exposes a host-settable RTC" non-goal (`Out of Scope` table, line 68) is silently broken.

**Why it happens:**

- USB capture data is post-hoc evidence: it shows bytes went out and the device didn't visibly fail. **Visible non-failure ≠ semantic success.** The classic confirmation-bias trap: researcher hopes for a positive finding, finds a byte pattern that fits the hypothesis, ships it.
- AKP03 family firmware (per `docs/protocols/streamdeck/akp03.md:148`) emits NOP frames (`action code 0x00`) at idle — so "device didn't go silent after we sent the command" is the default behaviour, not evidence of acceptance.
- The 3-byte ASCII command-word framing (`docs/protocols/streamdeck/akp03.md:125-127`) is human-readable, which makes false positives extra-seductive: a researcher sees `TIM` and pattern-matches "TIMe!" — but it could be `TIMer` (sleep timer for the display backlight), `LIG` brightness, etc.
- The v1.1 D-02 honesty contract (`reference: TIMESYNC-05`) explicitly forbids "lying success UX" — and the per-row glyph flip from exclamation to ok is exactly that lying success.
- The AKP03 protocol doc itself currently has "Time sync — Status: scaffolded — not yet implemented" (`docs/protocols/streamdeck/akp03.md:214-220`). Promoting that to "functional" requires evidence stronger than "bytes went and device didn't crash."

**How to avoid:**

- **Three-witness rule for promoting a capability from `scaffolded` to `functional`:**
  1. **Capture witness:** the native AJAZZ app provably sends this byte sequence when the user takes the corresponding UI action (and only then — not at idle / startup / arbitrary timer ticks).
  1. **Round-trip witness:** the device exhibits an *observable* state change. For `clock`: the device's own clock display advances by a measurable amount that matches the timestamp we sent (visible via separate readback, OR by waiting 5 min and observing wall-clock drift on a face-shown clock widget — if neither readback nor a visible clock widget exists, **the capability is unverifiable; ship `partial` not `functional`**).
  1. **Negative witness:** sending a deliberately *wrong* value (e.g. epoch `0`) produces a different observable state — proving the device is actually parsing the field, not no-op-ing.
- **For `clock` specifically on the AKP03 family:** there is no LCD clock widget rendered by firmware (`docs/protocols/streamdeck/akp03.md:113-114` lists features; clock-on-keyface is not among them — and it's separately listed as Out of Scope in PROJECT.md). Therefore the round-trip witness is **structurally unavailable**. The default promotion verdict for AKP03 `clock` is therefore: **stays `partial` or `NotImplemented`; cannot reach `functional` without a firmware-side reverse-engineering breakthrough.** Document this in the phase plan as the *expected* outcome — not as failure.
- **Code-review checklist for capability promotions:** the PR description must explicitly tick all three witnesses or state which is missing and why `partial` is correct. A PR that promotes to `functional` without all three witnesses is a request-changes by default.
- **ARCH-04 ratification trigger:** if Phase 9 research surfaces real wire-format evidence for `IClockCapable::setTime` on any device, file an ARCH decision doc (named `ARCH-04-clock-wire-format-<device>`) **before** Phase 10+ implements it. PROJECT.md currently lists this as Out of Scope with a documented rationale; flipping that requires a written decision, not a passing test.

**Warning signs:**

- Phase 9 research note says "I think this is the time-set command" without a corresponding "and here's the readback / observable state change."
- A capability impl ships and the same researcher who wrote it also signs off on the promotion.
- The maturity tier flips from `scaffolded` to `functional` in the same PR as the impl lands — no intermediate `partial` step.
- The `s_warned_<device>` `std::once_flag` from v1.1 Phase 5 is removed (signalling "NotImplemented is gone") without a corresponding negative-witness test.

**Phase to address:**
**Phase 9 (Research)** establishes the three-witness rule as a milestone-wide protocol. **Each implementation phase (10+)** must apply it per capability promotion. **Phase 9 deliverable: explicit `clock` capability decision per device** — either ARCH-04 ratifying real wire format, or PROJECT.md confirmation that `NotImplemented` stays.

______________________________________________________________________

### Pitfall 20: `0c45:7016` is the AK980 PRO's secondary HID interface → duplicate sidebar entry

**What goes wrong:**
`0c45:7016` is the unknown PID one of the four currently-connected devices presents. The AK980 PRO (`0c45:8009`, `docs/_data/devices.yaml:297-305`) is a Microdia-chipset wireless mechanical keyboard. Microdia composite-HID devices commonly expose two USB *interface* descriptors with distinct PIDs at the USB layer when enumerated through certain wireless dongles: one for boot keyboard, one for vendor-control. `DeviceRegistry::enumerate()` (post-v1.1 `shared_ptr<IDevice>` migration) currently keys on `(vid, pid, serial)` — so two interfaces of the same physical keyboard with different PIDs but the same serial produce **two** rows in `DeviceModel`. The sidebar shows "AK980 PRO" and "(unknown 0c45:7016)" side by side; user is confused, and any per-device state (selected key, pending image upload) is split across two rows.

**Why it happens:**

- Composite HID is the norm for wireless gaming peripherals (one interface for HID-class boot kbd/mouse for OS compatibility, a second for vendor-specific control reports). The hidapi enumerator returns each interface separately because that's how the OS reports them.
- The repo's existing dedup logic (per `docs/_data/devices.yaml:307-323`) is at the **VID/PID enumeration level**, not the **same-physical-device-multiple-interfaces level**. The AJ-series mouse entries handle this by registering each PID separately and treating them as distinct rows on purpose — but that's the *wrong* model for a single physical keyboard with two interfaces.
- Linux: `udev` populates `ID_MODEL` + `ID_SERIAL_SHORT` consistently across interfaces of the same physical device; Windows: hidapi's `serial_number` field may be empty on some Microdia wireless dongles, removing the dedup key.
- The v1.1 hot-plug debouncer (`HotplugDebouncer`, 300 ms trailing-edge per `(vid,pid,serial)` per v1.1 Phase 4) coalesces *within* a tuple but does not collapse *across* PIDs for the same device.

**How to avoid:**

- **Phase 9 deliverable: confirm physical identity of `0c45:7016`.** Three checks, in order:
  1. `udevadm info -a /dev/hidrawN` for both `0c45:7016` and `0c45:8009`; if `ID_USB_SERIAL` matches (or both refer to the same parent `/sys/bus/usb/devices/X-Y/`), it's the same physical device.
  1. Unplug AK980 PRO dongle, observe that BOTH `0c45:7016` and `0c45:8009` disappear simultaneously. If they do, same device.
  1. `lsusb -t` — if both are children of the same hub port, same physical device.
- **If confirmed: introduce a composite-HID dedup step in `DeviceRegistry::enumerate()` BEFORE Phase 11 (AK980 PRO rgb/macros/layers impl) lands.** Pattern:
  ```cpp
  // In DeviceRegistry::enumerate() — composite-HID grouping:
  // Group hid_device_info entries by (path-parent OR serial-if-non-empty)
  // and pick the interface that advertises the vendor-control Usage Page
  // (typically 0xFF00..0xFFFF) — that's the row we surface.
  ```
  The vendor-control interface is the one we'll send capability commands to; the boot-keyboard interface is the OS's concern, not ours.
- **If NOT confirmed (separate device):** add a new entry to `devices.yaml` with a separate codename, treat as a distinct row. Acceptable — but verify the negative case isn't a false negative caused by missing/empty serials.
- **Test pattern:** `MockHidEnumerator` gains a "composite device" fixture (two `hid_device_info` entries with same parent path / serial, different PIDs). `DeviceRegistry::enumerate()` returns exactly one descriptor for that fixture. Catch2 TEST_CASE in `tests/integration/test_composite_hid_dedup.cpp`.
- **Pre-condition for the AK980 PRO impl phase:** composite-HID dedup must land first. If it doesn't, the UI shows duplicate cards and the impl phase regresses Phase 8's per-row tooltip.

**Warning signs:**

- Sidebar shows two cards that respond to the same physical keypress / disconnect.
- One card always shows `scaffolded`; the other shows... also `scaffolded` (because neither has an impl, but the right answer is one row).
- `hid_open` succeeds on `0c45:7016` but every write returns immediately (it's the boot-keyboard interface, which doesn't accept vendor reports).

**Phase to address:**
**Phase 9 (Research) identifies the physical identity.** **Phase 10/cross-cutting infrastructure phase (composite-HID dedup) MUST precede the AK980 PRO impl phase.** Plan slug: `XX-composite-hid-dedup-infrastructure` — could be Phase 10 if AK980 PRO is the first impl phase.

______________________________________________________________________

### Pitfall 21: Wireless 2.4G captures hit the wrong protocol layer

**What goes wrong:**
The AJAZZ 2.4G 8K mouse (`3151:5007`, `docs/_data/devices.yaml:369-378`) and the AK980 PRO (`0c45:8009`) both communicate over their own 2.4G dongle radios. A researcher who wants to RE "the mouse's protocol" sets up a Wireshark capture and... captures the USB traffic between *dongle* and *host*. That captures **post-decryption HID reports** — exactly what we want for implementation. But a researcher chasing the wrong rabbit might set up an SDR or a CC2531-sniffer to capture the **RF link** between mouse and dongle, on the assumption that "wireless = need to capture wireless." Hours wasted; the RF link is dongle-firmware-decrypted before host sees it, and decoding it requires reverse-engineering the dongle firmware (out of scope per CLAUDE.md "no wine / innoextract / vendor installers" — analogous prohibition: no dongle-firmware extraction).

A secondary failure: capturing the right layer (USB-host-to-dongle) but at the wrong moment. The 8KHz polling rate means the mouse sends ~8000 position reports per second of movement; capture buffers fill fast, the actual control-channel events (DPI cycle, LED change) are buried in megabytes of position data. Filter at capture time or drown in noise.

**Why it happens:**

- "Wireless" misleads — researchers think they need to capture air, when in fact dongle-decrypts-RF and presents normal USB HID.
- Marketing rhetoric "8KHz polling" implies a fast firehose; researchers either over-prepare for "wireless complexity" or under-prepare for "USB volume."
- No standard guidance in this repo's existing capture notes (`docs/research/vendor-protocol-notes.md` is mouse-centric but not noise-management-centric).

**How to avoid:**

- **Phase 9 capture protocol:** for the wireless devices, capture USB between dongle and host (`usbmon` on the bus the dongle is plugged into), NEVER attempt to capture air. Document this as a standing rule in the capture-hygiene policy doc (Pitfall 17).
- **Capture-time filtering for the 2.4G 8K mouse:**
  - Capture only when DPI button / control keys are being pressed (operator-driven event window), not during sustained movement.
  - Drop `URB_INTERRUPT IN` packets that match the standard 8-byte HID mouse report pattern (`tcpdump 'len != 8'` or Wireshark filter); keep only feature reports + non-standard interrupt reports.
  - Cap capture file size at 50 MB; if it fills in \<10 seconds, the filter is wrong.
- **Capture-time filtering for the AK980 PRO:**
  - Boot-protocol keystrokes are 8-byte reports on a specific interface — filter them out before write-to-disk (also addresses Pitfall 17 privacy). Keep only the vendor-control interface (the one identified per Pitfall 20).
- **OSS corpus first, captures second:** if `python-elgato-streamdeck`, `opendeck-akp03`, or any wired-equivalent device's protocol is already documented, the wireless device's protocol is almost certainly an extension of the wired one with a dongle-side adaptation header. Look at the OSS doc first; capture only the *delta*.

**Warning signs:**

- Capture file >100 MB for a \<60-second session.
- Capture contains thousands of identical-looking 8-byte mouse-position reports and nothing else.
- Researcher proposes "let me capture the RF" — kill that idea immediately; out of scope per the no-vendor-RE rule.
- A `.pcap` from a wireless device contains zero feature reports (only IN interrupts). Then we have only telemetry, not control-channel.

**Phase to address:**
**Phase 9 (Research) — capture protocol document is a Phase 9 deliverable, not a per-device-phase concern.** Same plan as Pitfall 17 (`09-01-capture-data-hygiene-policy`) extended with capture-mechanics guidance.

______________________________________________________________________

### Pitfall 22: OSS corpus is for a different firmware revision — silent wire-format drift

**What goes wrong:**
`opendeck-akp03` (and the [4ndv/opendeck-akp03](https://github.com/4ndv/opendeck-akp03) Rust source, and the [mirajazz crate](https://crates.io/crates/mirajazz) referenced in `docs/_data/devices.yaml:262`) targets specific firmware revisions of AKP03 / Mirabox N3 family devices. The user's `0300:3004` device (codename `akp05e`, `devices.yaml:264-274`) is registered as "Ajazz HOTSPOTEKUSB HID DEMO" — a string that suggests pre-production / dev firmware. Wire format on a dev/HID-demo firmware can differ in subtle ways: chunk size off-by-one, command-word case (`TIM` vs `tim`), `is_v2_api` flag location, last-chunk-flag bit position, image-format-Rot value. Implementation pulled verbatim from `opendeck-akp03` works on production AKP03 / N3 / N3EN but silently corrupts uploads on `0300:3004`: device displays garbled image fragments, or last upload appears to "stick" to the wrong key, or brightness command is interpreted as "set sleep timer."

**Why it happens:**

- OSS reverse-engineering corpora document the firmware their author had access to. They don't claim coverage of pre-production / dev variants.
- "Same VID/PID family" feels like "same protocol" — but the AKP03 protocol doc itself flags 3 distinct wire-format generations (`docs/protocols/streamdeck/akp03.md:54-100` lists v1, v2 1024-byte, and AKP03R rev. 2 protocol_version 3 with full press/release + GIF).
- The `0300:3004` PID is **not** in any of the three corpora cited (`docs/protocols/streamdeck/akp03.md:80-83` is explicit: "not present in either `[ajazz-sdk]` or `[opendeck-akp03]`").
- Confirmation bias when the first few packets *look* like they work — a 1024-byte chunk that produces *any* image, even garbled, looks like progress.

**How to avoid:**

- **Phase 9 deliverable per device: a wire-format diff doc.** For each connected device, capture a baseline interaction (one image upload + one brightness change + one event) and compare byte-for-byte against the OSS corpus's predicted output. Document deltas. If zero deltas: corpus applies. If 1+ deltas: per-deviation-explained protocol delta in the device's protocol .md.
- **Always parameterise the wire-format constants** (chunk size, last-chunk flag offset, image-Rot value, command-word case) as fields on `Akp03Descriptor` rather than hard-coded. Variants `akp05e` get their own descriptor row; the impl reads from the descriptor.
- **Test pattern:** `tests/unit/test_akp03_descriptor_variant.cpp` — table-driven test with one row per AKP03 family member, asserting the descriptor's chunk size + image format + Rot match the protocol .md table. Catches drift if someone adds a new family member without updating the descriptor.
- **Pessimistic default for `0300:3004`:** until a real-device capture confirms wire format, the descriptor copies AKP03 (60×60 Rot0, 1024-byte v2 packets) and the maturity stays `scaffolded`. Phase 10's first commit is the capture-driven descriptor update.

**Warning signs:**

- First image upload "works" but the image looks faded / pixelated / wrong-color — partial wire-format match, not full.
- Image works but encoder events don't (or vice versa) — protocol header offset shifted between revisions.
- Code works on AKP03 (canonical) but not on `0300:3004` — should have been a `Descriptor` field, was hardcoded somewhere.

**Phase to address:**
**Phase 9 (Research) per-device deliverable: wire-format diff against OSS corpus.** Each impl phase (10+) consumes the diff doc, parameterises any deltas in the descriptor.

______________________________________________________________________

## Moderate Pitfalls

### Pitfall 23: Encoder events — quadrature vs delta misclassification + signal storm

**What goes wrong:**
AKP03 encoder events arrive as discrete action codes (`0x90`/`0x91` for encoder 0 CCW/CW, etc., per `docs/protocols/streamdeck/akp03.md:140-148`) — one event per detent. That's *delta* encoding (each event = "+1 detent"). If a future device emits *quadrature* (two-bit state changes that must be diffed against the prior state to derive direction), an implementer who copies AKP03's `EncoderRotated(direction=CW, delta=1)` shape against a quadrature device double-counts (or zero-counts) every rotation. Conversely: if the AKP03 encoder spins fast (~30 detents/sec on a wrist-flick) and each event fires a `Q_SIGNAL`-coupled QML update + a profile-store write, the result is a QML repaint storm — 30 Hz wouldn't normally be a problem but `DeviceModel::dataChanged(roleIndex)` on a list-view item triggers more than just the row repaint (the model emits a `QVariant` boxing per role; QML re-binds bindings).

**Why it happens:**

- "Encoder" is a homonym across hardware: rotary encoder (quadrature, hardware-level), HID-encoder (delta, firmware-cooked), absolute encoder (position-reporting). Same word, three protocols.
- Existing AKP03 code only handles the delta case (`docs/protocols/streamdeck/akp03.md:140-148`). A new device that emits raw quadrature has nowhere obvious to land in the parser.
- Encoder press-event has its own action code (`0x33`/`0x34`/`0x35`) that's distinct from the rotation codes — but shares a HID button range with the LCD keys on some firmwares. AKP03's `0x33` (encoder 0 press) is just barely outside the `0x01..0x06` LCD-key range, but a future device's encoder press at `0x05` would alias.
- Coalescing has no obvious place in the dispatch path: the backend parses one event per `hid_read`, hands it to `DeviceModel`, which hands it to QML.

**How to avoid:**

- **Always** classify encoder events at the parser layer, not the model: `EncoderRotated{which, delta}` (firmware-cooked, delta) or `EncoderQuadrature{which, state_bits}` (raw, parser computes delta). Parser knows which device emits which.
- **Always** check that encoder-press action codes don't overlap with the LCD-key range. Add a `static_assert` per device descriptor: `static_assert(EncoderPressCodes_disjoint_from_KeyCodes(Akp03Descriptor))`.
- **Coalesce encoder events at the model layer**, not the backend. Pattern: backend emits every event (lossless), `DeviceModel` accumulates deltas in a `QHash<QPersistentModelIndex, int>` and flushes on a 16 ms `QTimer` (one repaint per frame at 60 FPS). User-visible behaviour: smooth scroll; signal volume: bounded.
- **Profile-store writes** triggered by encoder events: debounce to 500 ms quiet period after the last event. A wrist-flick produces 30 events; one profile write, not 30.
- **Test pattern:** Catch2 TEST_CASE that injects 1000 encoder events back-to-back and asserts the model emits ≤16 `dataChanged` signals per second (mocked timer).

**Warning signs:**

- QML profiler shows encoder-row rebinds at >100 Hz.
- Profile-save toast fires repeatedly during a single wrist-flick.
- Encoder direction "reverses" intermittently (quadrature misclassified as delta — the second bit of the state pair is interpreted as direction).

**Phase to address:**
**Phase 10 (AKP03 display+encoder impl)** establishes the pattern; **Phase 11+ (AK980 PRO, 2.4G 8K)** reuses it.

______________________________________________________________________

### Pitfall 24: AK980 PRO RGB writes — wireless dongle queue overflow stalls keystrokes

**What goes wrong:**
The AK980 PRO (`0c45:8009`, Microdia chipset) has per-key RGB. Setting per-key RGB on a 60-key keyboard naively means 60 HID feature-report writes (one per key) at app startup, or on every profile-switch. On a 2.4G wireless link with a finite radio bandwidth budget shared between vendor-control reports and keystroke reports, sending 60 control reports back-to-back can saturate the dongle's TX queue. Symptoms: keystrokes typed during the RGB write are dropped or arrive late (10-200 ms latency spike); on some firmwares, the dongle's USB endpoint stalls and the keyboard goes offline for ~1 second.

**Why it happens:**

- Wired keyboards have effectively unlimited bandwidth between dongle (= host USB controller) and the device. Wireless dongles share a single half-duplex 2.4G channel; vendor reports compete with keystroke reports.
- Firmware-side: most Microdia-chipset wireless keyboards (no authoritative public docs — MEDIUM confidence from community reports per `docs/_data/devices.yaml:305` "captures welcome" + general 2.4G HID community knowledge) batch RGB updates internally only if the host sends a "begin batch / end batch" command pair. Without the pair, each RGB write triggers an immediate radio frame.
- The naive "60 writes, one per key" approach is the obvious first attempt — and the only one that works on wired devices.

**How to avoid:**

- **Capture witness first:** Phase 9 capture of the native AJAZZ app changing RGB on AK980 PRO must show whether the app sends N writes or 1 batched write. Implementation matches the captured pattern; do NOT improvise.
- **If the wire format supports batching:** always use it. The batch command sets all keys in one report; one radio frame; no overflow.
- **If the wire format requires per-key writes:** rate-limit at ≤10 writes/sec for wireless devices. RGB changes during typing are visually imperceptible at that rate; the user can't tell a 6-second-fade animation from a 4-second one but they *can* tell when their keystrokes are dropped.
- **Always** detect "wireless vs wired" mode at descriptor creation. For Microdia 0c45 PIDs we currently don't distinguish; add a `bool isWireless` field on `KeyboardDescriptor`. AK980 PRO = true. The rate-limiter activates only when true.
- **Never** write RGB during high-frequency typing detection. Optional hardening: if a keystroke report arrives during an RGB batch, pause the batch, wait 100 ms, resume.
- **Test pattern:** `MockHidTransport` with simulated keystroke-report injection during an RGB write batch — assert that keystroke reports are not dropped (delivered to the listener in order, with bounded latency).

**Warning signs:**

- User reports: "keyboard misses keys when I open the AJAZZ app's RGB tab."
- Logs show a burst of `hid_write` calls followed by a 500+ ms gap before the next interrupt-IN report from the keyboard.
- LED change appears smooth but typing latency spikes during the change.

**Phase to address:**
**Phase 11 (AK980 PRO RGB+macros+layers impl).** Confidence: MEDIUM on the specific failure mode (no authoritative Microdia/SONiX wireless docs surfaced in web search; general 2.4G HID community knowledge supports the shape). Phase 9 capture is the witness.

______________________________________________________________________

### Pitfall 25: Macro/layer write to NVM — wear, blocking, brick risk

**What goes wrong:**
Macros and layer assignments on most AJAZZ keyboards (including the AK980 PRO per the proprietary backend per `docs/protocols/keyboard/proprietary.md`) are stored in firmware NVM (flash). Writes are:

1. **Slow** — a flash erase + program cycle can take 50-500 ms during which the keyboard may be unresponsive on the radio link;
1. **Limited write-cycle count** — typical SPI flash specs are 100,000 program/erase cycles per block (per generic SONiX SN32F datasheet category; no AK980 PRO-specific datasheet surfaced — LOW confidence on exact number, HIGH confidence on the shape). At 10 writes/sec from a buggy app, that's exhausted in ~3 hours. Past the wear limit, the block becomes unreliable and the keyboard's persistent state stops persisting correctly — silent brick.

A naive "save macro on every keystroke in the macro recorder" implementation, or a "auto-save on every profile-page tab-switch," is a fast path to wear. Even an "auto-save every 60 seconds while macro editor is open" is bad — if the user leaves the editor open for 8 hours, that's 480 writes for one editing session.

**Why it happens:**

- Desktop apps reflexively auto-save user input. That's correct for filesystem-backed state. It's catastrophic for flash-backed state.
- "Save" semantics in the UI may be ambiguous: does "Save profile" mean "save to host disk" (cheap, unlimited) or "flash to keyboard NVM" (expensive, limited)? If conflated, users save 100x more than necessary.
- Wear is invisible until failure. There's no progress bar saying "this flash block has 1234 writes left."

**How to avoid:**

- **Separate "host-disk save" from "device-flash commit"** as distinct user actions. UI: "Save profile" (instant, to host disk) vs "Push to device" (deliberate, to flash). Default to host-disk save; require an explicit user click for the flash commit.
- **Rate-limit device-flash commits** to ≤1 per minute (and ≤100 per device-lifetime under typical assumptions — surface a counter in Settings if real users hit it).
- **Never** auto-flash on profile-switch. Profile switching is a runtime layer-toggle (HID byte write, RAM only, no NVM cost), not a flash commit.
- **Always** show user-visible feedback for the flash commit: "Writing to device... (1-2 sec)" — the latency is real, hiding it makes the UI feel broken.
- **Quota tracking (optional, hardening):** persist a per-device "flash writes" counter in `QSettings`; warn at 50,000 writes / device. This is a soft guard, not a hard cap.

**Warning signs:**

- Profile-switch is observably slow (>500 ms) — auto-flashing on every switch.
- Macro editor produces N flash commits per editing session — auto-saving on every change.
- User reports "my macros stopped persisting after a few weeks" — possible wear-out; verify by reading back vs newly-written values.

**Phase to address:**
**Phase 11 (AK980 PRO macros+layers).** Confidence on AK980 PRO-specific NVM wear numbers: LOW (no datasheet surfaced). Confidence on the general principle: HIGH (universal for SPI-flash-backed firmware).

______________________________________________________________________

### Pitfall 26: Layer-switch ↔ RGB-indicator race leaves indicator stuck

**What goes wrong:**
AK980 PRO has both `layers` and `rgb` capabilities. Many keyboards visually indicate the current layer via the RGB backlight (e.g. layer 1 = solid blue, layer 2 = solid green). The app's "switch to layer N" command must do two things: write the layer-select HID byte AND update the RGB to the layer's indicator color. If the two writes go to firmware in the order [RGB update, then layer switch], there's a brief window where the indicator is wrong. More importantly, if the layer-switch write succeeds and the RGB write fails (radio frame lost), the indicator is *permanently* wrong until the next manual RGB write — and the user thinks they're on layer 1 (blue) while they're actually on layer 2 (now also blue).

**Why it happens:**

- The two capability surfaces (`IRgbCapable`, `ILayerCapable` — assuming the mix-in shape, per Pitfall 27) are independent in the C++ interface. Naive impl writes each independently.
- Wireless link can drop frames; no app-layer ack/retry on vendor reports unless explicitly designed.
- Firmware may support an "atomic layer-and-color" command — or it may not. Captures will tell.

**How to avoid:**

- **Phase 9 capture witness:** observe whether the native AJAZZ app sends a single combined report or two sequential reports for layer-switch. If single: use that. If two: implement the sequence with rollback semantics (see below).
- **If two writes are required:** layer-switch first, RGB update second. If RGB write fails: retry once. If retry fails: log WARN + emit `IndicatorOutOfSync` signal; UI shows a small warning glyph. Do NOT silently leave the indicator wrong.
- **If firmware supports atomic combined command:** use that exclusively. The single-write path eliminates the race.
- **Test pattern:** `MockHidTransport` injects an error on the second of two writes; assert the backend emits `IndicatorOutOfSync` and retries once.

**Warning signs:**

- "Layer indicator is wrong after I switch profiles" — bug report shape.
- Logs show a successful layer-write followed by a failed RGB-write with no retry.

**Phase to address:**
**Phase 11 (AK980 PRO).** Depends on Phase 9 capture for atomic-vs-sequential decision.

______________________________________________________________________

### Pitfall 27: Capability mix-in inheritance — diamond if any mix-in inherits QObject

**What goes wrong:**
AKP03 has 4 mix-in capabilities (display + encoder + clock + macros). AK980 PRO has 4 (rgb + macros + layers + clock). Once `IDevice` + `IDisplayCapable` + `IEncoderCapable` + `IClockCapable` + `IMacrosCapable` all live in the inheritance graph of a single backend class, if **any** of those interfaces (including `IDevice`) inherits from `QObject` — or if a backend implementer thoughtlessly adds `QObject` to a mix-in to get signals — C++ multiple-inheritance produces a diamond: two `QObject` bases via two paths, ambiguous `metaObject()`, broken `qobject_cast`, broken signal-slot.

**Why it happens:**

- `QObject` is the default base for "anything that emits signals" — and capability mix-ins look like things that emit signals (e.g. `IEncoderCapable::encoderRotated(int which, int delta)` is the natural signature).
- C++ allows multiple inheritance silently; the failure surfaces at the first `qobject_cast` or signal emission, often runtime.
- The v1.1 `IClockCapable` (per Phase 5) is a plain interface (no `QObject`) — sets the precedent. New researchers may not realise the precedent matters.

**How to avoid:**

- **Hard rule, documented in `src/core/include/ajazz/core/capability.hpp` (or wherever the mix-ins land):** capability mix-ins are **plain interfaces** — no `QObject` inheritance, no `Q_OBJECT` macro, no signals. They expose pure-virtual methods + an `enum class Capability` bit.
- **Only `IDevice` inherits from `QObject`** (or whichever single base needs it). The concrete backend class (`Akp03Device : public IDevice, public IDisplayCapable, public IEncoderCapable, ...`) inherits from one `QObject` base + N plain interfaces — no diamond.
- **Signals go on the concrete backend, not the mix-in.** `Akp03Device::encoderRotated(int, int)` is the Q_SIGNAL; `IEncoderCapable` declares the pure-virtual setter and the *contract* the backend honours.
- **Runtime capability discovery via `dynamic_cast<IClockCapable*>(device)`** (the v1.1 Phase 5 pattern) — works because the mix-ins are polymorphic-via-IDevice's vtable.
- **Static check:** add a `static_assert(!std::is_base_of_v<QObject, IDisplayCapable>, ...)` per mix-in. Build break if anyone "fixes" the mix-in to inherit QObject.

**Warning signs:**

- Build error: "ambiguous conversion from `Akp03Device*` to `QObject*`."
- Runtime: `qobject_cast<Akp03Device*>(...)` returns nullptr against a `QObject*` that should be the right type.
- Signal emitted from one path, slot connected via the other path — silently no-op.

**Phase to address:**
**Phase 10 (first device-impl phase that adds new mix-in interfaces).** ARCH decision doc: `ARCH-04-capability-mixin-inheritance-graph`. Lock the no-QObject-in-mix-ins rule with `static_assert`s before any concrete backend pulls in a third mix-in.

______________________________________________________________________

### Pitfall 28: DPI stage cycling — order is vendor-defined, not +1

**What goes wrong:**
The AJAZZ 2.4G 8K mouse has 6 DPI stages (`docs/_data/devices.yaml:377`). The DPI button on the mouse cycles through them. Naive impl: "active_stage = (active_stage + 1) % 6, write stage 0..5 cyclically." But the vendor's cycle order may be {1, 2, 3, 4, 5, 6} OR {3, 5, 1, 6, 2, 4} OR "user-configured order persisted in NVM." If the wrong order is implemented, a user who presses DPI-down on their mouse expects to drop one stage and instead jumps to an unexpected DPI — a real annoyance during gaming or precision pointing.

Secondary: setting a DPI may be a single HID report (instant, RAM-only) OR a 3-step sequence (set-stage-active + flash-to-NVM + push-to-dongle for wireless). Wrong sequence: DPI changes work for the session but don't persist across mouse-power-cycle.

**Why it happens:**

- `docs/research/vendor-protocol-notes.md` Finding 11.B notes the AJ199 family wire-format split (AJ199 Max struct-offsetted vs AJ199 V1.0 flat report). The AJAZZ 2.4G 8K (different SKU, same shape of risk) is even less documented in OSS.
- Stage order is a UX choice the vendor made; it's not derivable from the wire format alone.
- "DPI works in this session, but reset after I unplugged the mouse" = correct stage-active write, missing flash-to-NVM step.

**How to avoid:**

- **Phase 9 capture witness:** drive the native AJAZZ app's DPI tab, change each stage, cycle the on-mouse DPI button, capture each interaction. Document the byte sequence per action: "stage select," "stage value set," "stage cycle order," "flash commit."
- **Always** treat DPI stage list as ordered + capacity-limited. Vendor cycle order is a separate field, not implicit from list position.
- **Always** include the flash-commit step in the impl. If the wire format requires it: do it. If it doesn't: document that the device flashes on every write.
- **Test pattern:** capture-replay test — sequence of captured byte arrays from the native app, replayed against `MockHidTransport`, asserts the same sequence is produced.

**Warning signs:**

- DPI changes don't persist across power-cycle.
- DPI button cycles in an order the user finds confusing or unexpected.
- "DPI works but only the first stage takes effect" — missing stage-select-active write.

**Phase to address:**
**Phase 12 (2.4G 8K mouse dpi+rgb).** Confidence: MEDIUM (extrapolation from AJ199 finding 11.B; no direct AJAZZ 2.4G 8K wire format public).

______________________________________________________________________

### Pitfall 29: `MaturityRole` lying after partial-capability-impl ships

**What goes wrong:**
AKP03 (variant `0300:3004`) has 3 advertised capabilities (display + encoder + clock). If Phase 10 ships display + encoder both `functional` but Phase 9's three-witness rule (Pitfall 19) confirms `clock` is **not** real → the device must stay `partial`, not `functional`. If a PR promotes to `functional` because "2 of 3 work and clock returns `NotImplemented` honestly" — that's the v1.1 D-02 honesty contract being violated at the *device* level instead of the per-capability level. The sidebar tooltip then says "all advertised capabilities work in practice" (per `devices.yaml:21`), which is a lie.

**Why it happens:**

- Promotion is gated on "all advertised capabilities work" but the *advertised* set is a YAML field that's easy to silently shrink to make the promotion legal. ("Just remove `clock` from the AKP03 capabilities list and we can promote!")
- Removing a capability from the advertised list is itself a regression — Phase 5's TimeSyncService stops showing the per-row glyph for that device, which is *better* honesty than a fake glyph but *worse* discoverability for users wondering "why doesn't this device have clock?"

**How to avoid:**

- **Rule (document in `docs/_data/devices.yaml` header comment):** removing a capability from `capabilities:` list requires either (a) the capability was provably wrong from the start (misclassified, factory-only, etc.) — document in `notes:`; or (b) a deprecation note in the per-family `feature_summary.pending:` block with a tracking ticket.
- **Promotion to `functional` requires:** all advertised capabilities pass the three-witness rule (Pitfall 19). If one fails: stay `partial`, document the failing capability in `feature_summary.partial:` or `feature_summary.pending:`.
- **`feature_summary` per-device block is mandatory for `partial` and `probed` tiers** (Phase 8 already established the convention for `akp815` and `mirabox_n3` per `devices.yaml:122-132, 205-214`). v1.2 promotions follow suit: `feature_summary.works: / partial: / pending:` lists per device.
- **Code-review checklist:** for any maturity tier flip (in `devices.yaml` PR), the diff must show *either* all capabilities in the works list (→ functional) or an explicit pending/partial entry per non-working capability (→ partial). The reviewer's job is to verify, not to take the PR author's word.

**Warning signs:**

- A PR flips maturity from `scaffolded` to `functional` without going through `partial`.
- A PR removes a capability from the advertised list (with no `feature_summary.pending:` entry justifying it) at the same time as flipping maturity to `functional`.
- The per-row tooltip in the UI says one thing while the device's actual behaviour says another.

**Phase to address:**
**Each impl phase (10, 11, 12).** Per-device promotion gate.

______________________________________________________________________

## Minor Pitfalls

### Pitfall 30: AKP03 family-shared code — cross-family regression risk

**What goes wrong:**
AKP05E (`0x3004`) shares Stream Dock backend code with akp03 / akp03e / akp03r / akp03_legacy / mirabox_n3 / mirabox_n3_rev3 / mirabox_n3en (per `docs/_data/devices.yaml:134-238`). A fix for `0300:3004` that lives in `akp03.cpp` ships to all 7 other PIDs simultaneously. A wire-format delta that's correct for `0300:3004` may break the canonical AKP03 (`0x1001`).

**How to avoid:**

- **Variant-specific behaviour goes in the descriptor**, never in the shared backend. Per Pitfall 22, descriptor-parameterise everything that could differ between firmware revisions.
- **Test pattern:** `tests/unit/test_akp03_family_descriptor_coverage.cpp` — one TEST_CASE per family member, asserts each behaves per its declared descriptor against `MockHidTransport`.
- **Code-review checklist:** any patch to `akp03.cpp` shared logic that doesn't add a corresponding family-coverage test is rejected.

**Phase to address:** Phase 10.

______________________________________________________________________

### Pitfall 31: `TimeSyncService` per-row glyph state transition

**What goes wrong:**
When AKP03 `clock` starts actually working (if it does), the per-row glyph for `0300:3004` transitions from "exclamation/NotImplemented" to "ok/synced." The v1.1 Phase 5 QML observer assumed the glyph is *set once* on first sync attempt and *replaced* on subsequent attempts. The observer must handle: (a) device disconnects → glyph removed; (b) device reconnects → glyph re-applied with last-known result; (c) capability changes between two same-VID/PID reconnects (e.g. firmware update). The last case is rare but not impossible; if the observer caches the "first state" forever, it shows stale glyph.

**How to avoid:**

- **Always** drive glyph state from the current per-device `TimeSyncResult`, not from a cached "first observed result."
- **On `Capability::Clock` advertised state change** (descriptor's `hasClock` flips): emit `Q_SIGNAL clockCapabilityChanged(deviceId)`; QML re-evaluates glyph from scratch.

**Phase to address:** Phase 10 (and any subsequent phase that flips a capability bit at runtime).

______________________________________________________________________

### Pitfall 32: ASCII-only test names — new device codenames check

**What goes wrong:**
v1.1 Phase 4 hit "test names must be ASCII-only" because em-dash / right-arrow Unicode characters got mangled by the Win32 CMD codepage (CLAUDE.md "Cross-platform build strictness"). v1.2 will add tests that name devices like `akp05e`, `ak980pro`, `ajazz_24g_8k`. All current codenames are ASCII (verified by grep against `devices.yaml`). If the unknown `0c45:7016` gets a codename when identified, it must also be ASCII.

**How to avoid:**

- **Grep check** in CI: `grep -P '[^\x00-\x7f]' docs/_data/devices.yaml` must return zero lines.
- **Code-review checklist:** any new test name with a non-ASCII character is rejected.

**Phase to address:** Phase 9 (codename assignment for `0c45:7016`).

______________________________________________________________________

### Pitfall 33: QSignalSpy on Windows — granularity loop pattern

**What goes wrong:**
v1.1 Phase 4 found that `QSignalSpy::wait(...)` on Windows has timer granularity that can fire before all expected signals have arrived. Catch2 tests using QSignalSpy loop until N emissions or wall-time limit, not single-shot. Any new v1.2 test that uses QSignalSpy for capability-event observation needs the same pattern.

**How to avoid:**

- **Copy the v1.1 pattern** from the existing Phase 4 hot-plug integration tests. Idiom:
  ```cpp
  while (spy.count() < expected && elapsed < timeout_ms) {
      spy.wait(50);
      elapsed += 50;
  }
  REQUIRE(spy.count() >= expected);
  ```
- **Code-review checklist:** any `QSignalSpy::wait()` followed by `REQUIRE(spy.count() == N)` is suspect — must be `>=` with a loop.

**Phase to address:** any phase adding QSignalSpy-based tests (10, 11, 12).

______________________________________________________________________

### Pitfall 34: `hid_open()` CI grep gate regression

**What goes wrong:**
v1.1 Phase 4 added a CI grep gate forbidding production code from calling `hid_open()` directly (must go through `DeviceRegistry`). A new capability impl that wants to "just open the device once to read its serial" may bypass `DeviceRegistry::open()` and call `hid_open()` directly — silently breaks the grep gate, fails CI.

**How to avoid:**

- **Always** go through `DeviceRegistry::open(deviceId)`. If you need a transient open: that's what `DeviceRegistry::probe(deviceId)` is for (or should be — add it if missing).
- **Pre-commit awareness:** the CI grep gate IS the test. If a researcher gets a green local build but red CI, this is the likely cause.

**Phase to address:** any impl phase. Verification: existing CI grep gate.

______________________________________________________________________

### Pitfall 35: `--no-verify` temptation under mdformat reformat

**What goes wrong:**
Pre-commit's mdformat reformats markdown on commit, fails the commit (because the formatted output differs from staged), and the standard fix is "re-stage the formatted file + retry." A frustrated researcher (especially under 2-agent parallel pressure per CLAUDE.md cap) may reach for `--no-verify`. CLAUDE.md explicitly says: `--no-verify` is only acceptable when the **hook itself** is broken. Mdformat reformat is not broken; it's working as designed.

**How to avoid:**

- **Always re-stage + retry.** No `--no-verify`. The retry succeeds because the formatted output is now the staged content.
- **Document the convention in v1.2 plan files** so subsequent researchers don't relearn this the hard way.

**Phase to address:** every phase. Reminder, not a new control.

______________________________________________________________________

## Technical Debt Patterns (v1.2-specific)

| Shortcut                                                  | Immediate Benefit                    | Long-term Cost                                                                                 | When Acceptable                                                                            |
| --------------------------------------------------------- | ------------------------------------ | ---------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| Commit `.pcap` to repo "for replay tests"                 | Captures shareable across machines   | **Privacy + security incident** — keystroke recovery is well-documented (Pitfall 17)           | **Never.** Use hash + out-of-band sharing + extracted `.hex` fixtures.                     |
| Hard-code wire-format constants in shared backend         | "Same family, same constants"        | Variant regression across 8 PIDs at once (Pitfall 22, 30)                                      | Never for inter-variant deltas; OK for cross-family invariants documented in protocol .md. |
| Promote `scaffolded` → `functional` skipping `partial`    | "Capability impl shipped"            | Lying maturity tier when one of N capabilities is fake (Pitfall 19, 29)                        | Never when any advertised capability fails the three-witness rule.                         |
| Auto-flash macros/layers on every UI change               | "Saves user from forgetting to save" | NVM wear → silent brick after ~3-8 hours of editing (Pitfall 25)                               | Never. Always separate host-disk save from device-flash commit.                            |
| Single-PID dedup logic for composite-HID devices          | "It works for non-composite devices" | Duplicate sidebar entries + split state for AK980 PRO if it's composite (Pitfall 20)           | Never once any composite device exists in the catalogue.                                   |
| Implement `clock` because the bytes "look like time"      | Capability count goes up             | Shipping a lie; D-02 honesty contract violation; UX regression on every reconnect (Pitfall 19) | Never. Three-witness rule first.                                                           |
| Capture air-side RF "because wireless"                    | Feels thorough                       | Hours wasted on wrong protocol layer; firmware extraction is out-of-scope (Pitfall 21)         | Never. USB-side dongle capture is the right layer.                                         |
| 3+ concurrent execute agents because "v1.2 has 4 devices" | Faster parallel exec                 | Git-coordination failures + `--no-verify` workarounds (v1.1 retro)                             | Never. Cap at 2.                                                                           |
| `--no-verify` because mdformat reformatted my markdown    | One fewer retry                      | Bypasses content-equivalence guarantees (Pitfall 35)                                           | Never. Re-stage + retry.                                                                   |

______________________________________________________________________

## Integration Gotchas (v1.2-specific)

| Integration                               | Common Mistake                                        | Correct Approach                                                                                                        |
| ----------------------------------------- | ----------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| Capture file → repo                       | Commit raw `.pcap`                                    | Extract control-channel `.hex` fixture; gitignore captures; reference by SHA-256 (Pitfall 17)                           |
| `dynamic_cast<I*Capable*>` mix-ins        | Forget `nullptr` check (v1.1 Pitfall 2 still applies) | Always `if (!cap) { emit failed(); return; }`                                                                           |
| AKP03 chunked image upload                | Reuse AKP153 v1-API 512-byte helper                   | Use 1024-byte chunks; last-chunk flag = 0x01 (Pitfall 18)                                                               |
| Composite-HID `(vid, pid, serial)` keying | Treat each interface as separate device               | Dedup by parent USB path or same-serial-across-PIDs (Pitfall 20)                                                        |
| Wireless RGB writes                       | Naive per-key burst                                   | Batch-write if firmware supports it, rate-limit if not (Pitfall 24)                                                     |
| Macros/layers persist                     | Auto-flash on every change                            | Separate host-save from device-flash; rate-limit (Pitfall 25)                                                           |
| Capability mix-ins                        | Inherit QObject for signals                           | Mix-ins are plain interfaces; signals on concrete backend; `static_assert(!is_base_of_v<QObject, IMixin>)` (Pitfall 27) |
| DPI stage cycling                         | Implement +1 cyclic order                             | Capture-witness vendor order; persist active stage + cycle order separately (Pitfall 28)                                |
| OSS corpus consumption                    | "Copy mirajazz crate's bytes verbatim"                | Phase 9 per-device wire-format diff doc; descriptor-parameterise deltas (Pitfall 22)                                    |

______________________________________________________________________

## Performance Traps (v1.2-specific)

| Trap                        | Symptoms                                   | Prevention                                                                                       | When It Breaks                                 |
| --------------------------- | ------------------------------------------ | ------------------------------------------------------------------------------------------------ | ---------------------------------------------- |
| Encoder signal storm        | QML repaint storm; profile-save toast spam | Coalesce at model layer (16 ms QTimer); debounce profile writes (500 ms) (Pitfall 23)            | Fast wrist-flick on AKP03 (30 detents/sec)     |
| AKP03 image upload latency  | Slow profile-switch on Stream Dock         | Cache JPEG-encoded image per key; only re-upload on actual change                                | Profile with 6 distinct images on first switch |
| Wireless RGB burst          | Keystroke loss during RGB transition       | Batch-write or rate-limit ≤10 writes/sec for `isWireless = true` (Pitfall 24)                    | 60-key per-key RGB change                      |
| Capture-replay test latency | CI runtime balloons                        | Trim replay sequences to capability-relevant frames only; ≤100 frames per fixture                | Replay file >1 MB                              |
| NVM-flash storm             | Macro editor latency spike + wear          | Decouple host-save (instant) from device-flash (user-deliberate); rate-limit ≤1/min (Pitfall 25) | Auto-flash on tab-switch in macro editor       |

______________________________________________________________________

## Security / Privacy Mistakes (v1.2-specific)

| Mistake                                            | Risk                                                                                     | Prevention                                                                                  |
| -------------------------------------------------- | ---------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| Commit raw USB capture containing keyboard reports | Recoverable user passwords / private text in public git history (Pitfall 17)             | Captures gitignored at directory level; pre-commit hook rejects `*.pcap`; reference by hash |
| Capture file shared in a GitHub issue attachment   | Same risk as above, broader exposure                                                     | Strip keyboard reports before sharing; share extracted `.hex` only                          |
| Mouse capture contains click-pattern timing        | Mild fingerprinting / activity inference                                                 | Strip timestamps to relative-from-zero; document the strip in capture-hygiene policy        |
| Air-side RF capture extracted into repo            | Out-of-scope vendor-firmware-adjacent material                                           | Don't capture air-side; out-of-scope per CLAUDE.md "no wine / vendor installers" analog     |
| `0c45:7016` writes blindly                         | Could brick the AK980 PRO if it's the boot-keyboard interface and we send vendor reports | Identify the interface (Pitfall 20); only write to vendor-control interface                 |

______________________________________________________________________

## "Looks Done But Isn't" Checklist

- [ ] **Capture corpus:** every committed file under `tests/fixtures/captures/` (if any) is verified to contain ZERO keyboard reports from interfaces other than the device-under-test. Grep for `usb.bInterfaceProtocol == 1` matches in `.pcap` files → reject.
- [ ] **AKP03 chunked upload:** test asserts (a) total written = `ceil(payload / 1024) * 1024`, (b) last chunk's transfer-done flag = 0x01, (c) chunk index sequential, (d) failure on chunk N halts the upload.
- [ ] **AKP03 `clock` promotion:** three-witness rule applied. If clock stays `NotImplemented`: AKP03 maturity is `partial`, not `functional`. Per-row glyph remains exclamation.
- [ ] **Composite-HID dedup:** `MockHidEnumerator` "composite device" fixture returns exactly one `DeviceDescriptor` from `DeviceRegistry::enumerate()`. Sidebar shows one row.
- [ ] **AK980 PRO RGB:** keystroke-injection-during-RGB-batch test passes (no dropped keystrokes); rate-limiter active when `isWireless = true`.
- [ ] **Macros/layers flash:** UI distinguishes "Save profile" (host) from "Push to device" (flash). No auto-flash on profile-switch or tab-switch.
- [ ] **Layer-switch atomic:** if firmware supports combined report, used; if not, layer-first-then-RGB with retry-on-RGB-fail; `IndicatorOutOfSync` emitted on permanent failure.
- [ ] **Capability mix-in graph:** `static_assert(!std::is_base_of_v<QObject, IDisplayCapable>)` (and one per mix-in) compiles. ARCH-04 decision doc ratified.
- [ ] **DPI stages:** persistence verified across mouse-power-cycle; cycle order matches captured vendor order (not hard-coded +1).
- [ ] **`MaturityRole`:** flipping any device past `partial` to `functional` requires `feature_summary.works:` covering ALL advertised capabilities.
- [ ] **AKP03 family coverage:** family-coverage test passes for all 7 PIDs sharing `akp03.cpp` (existing 6 + variant 3004).
- [ ] **TimeSyncService glyph:** disconnect → glyph cleared; reconnect → re-evaluated from current result (not cached).
- [ ] **ASCII codenames:** `0c45:7016`'s codename, once identified, is ASCII. Grep gate green.
- [ ] **QSignalSpy on Windows:** all new v1.2 tests using QSignalSpy use the granularity-loop pattern (v1.1 Phase 4 idiom).
- [ ] **`hid_open()` CI grep gate:** no new direct calls outside `DeviceRegistry`. Existing gate green.
- [ ] **`--no-verify` count:** zero in milestone commit log (excluding documented hook-broken cases, of which we expect zero).

______________________________________________________________________

## Recovery Strategies

| Pitfall                                                 | Recovery Cost                | Recovery Steps                                                                                                                                                                                        |
| ------------------------------------------------------- | ---------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 17. Pcap committed with keystrokes                      | **HIGH (security incident)** | `git filter-repo` to scrub history; force-push (with all collaborators notified); rotate any credentials potentially captured; assume worst case until proven otherwise. **Prevent, do not recover.** |
| 18. AKP03 chunked upload hangs device                   | LOW (per-incident)           | Power-cycle device; fix chunk size; re-test. No data loss.                                                                                                                                            |
| 19. Shipped `clock = functional` lie                    | MEDIUM                       | Demote to `partial`; restore `NotImplemented` + WARN-once; UI per-row glyph reverts to exclamation; CHANGELOG entry naming the regression.                                                            |
| 20. Composite-HID dedup missed → duplicate sidebar rows | LOW                          | Add dedup logic; test fixture; hotfix release. State held in the "ghost" row is lost (acceptable — it was never user-visible as separate).                                                            |
| 21. Capture is wrong protocol layer                     | LOW (operator-time)          | Discard capture; re-capture USB-side. No code shipped.                                                                                                                                                |
| 22. OSS corpus wire-format drift                        | LOW-MEDIUM                   | Per-deviation: parameterise in descriptor; add per-variant test row; promote slowly.                                                                                                                  |
| 23. Encoder signal storm                                | LOW                          | Add coalescing QTimer at model layer; ship as hotfix.                                                                                                                                                 |
| 24. Wireless RGB burst → keystroke loss                 | MEDIUM                       | Add rate-limiter for `isWireless`; user-visible "RGB applying..." indicator; ship as hotfix.                                                                                                          |
| 25. NVM wear → silent brick                             | **HIGH (hardware)**          | If a real user hits the wear ceiling, the device is field-bricked. **Prevent via rate-limit; do not recover.**                                                                                        |
| 26. Layer-indicator stuck                               | LOW                          | Add retry-once + `IndicatorOutOfSync` signal; hotfix.                                                                                                                                                 |
| 27. Mix-in QObject diamond                              | LOW (build break catches)    | Remove QObject from mix-in; rebuild.                                                                                                                                                                  |
| 28. DPI cycle order wrong                               | LOW                          | Capture-witness; parameterise; hotfix.                                                                                                                                                                |
| 29. `MaturityRole` lying                                | LOW                          | YAML edit + tooltip recompute; honest CHANGELOG entry; ship as patch release.                                                                                                                         |
| 30. AKP03 cross-family regression                       | LOW                          | Descriptor-parameterise the deltaed value; family-coverage test; hotfix.                                                                                                                              |

______________________________________________________________________

## Pitfall-to-Phase Mapping

| Pitfall                               | Prevention Phase                                               | Verification                                                                                        |
| ------------------------------------- | -------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| 17. USB capture privacy               | Phase 9 (capture-hygiene policy)                               | Pre-commit hook rejects `*.pcap`; `.gitignore` in `.planning/research/captures/`; policy doc landed |
| 18. AKP03 chunked upload hangs        | Phase 10 (AKP03 display impl)                                  | Catch2 chunk-size invariant test; real-device 100-image power-cycle smoke                           |
| 19. `clock` false-positive lie        | Phase 9 + 10 (three-witness rule + per-device decision)        | Per-PR checklist; capability-level three-witness ticked or `partial` accepted                       |
| 20. `0c45:7016` composite-HID dedup   | Phase 9 (identify) + cross-cutting phase before AK980 PRO impl | `MockHidEnumerator` composite fixture; single sidebar row asserted                                  |
| 21. Wrong protocol layer capture      | Phase 9 (capture-mechanics doc)                                | Capture metadata records bus + filter; air-side capture explicitly forbidden                        |
| 22. OSS corpus drift                  | Phase 9 (wire-format diff doc per device)                      | Per-variant descriptor + per-PID family-coverage test                                               |
| 23. Encoder signal storm              | Phase 10 (encoder impl)                                        | QSignalSpy injection of 1000 events; ≤16 dataChanged/sec asserted                                   |
| 24. Wireless RGB queue overflow       | Phase 11 (AK980 PRO RGB)                                       | Keystroke-during-RGB-batch test; rate-limiter active when isWireless                                |
| 25. NVM wear from auto-flash          | Phase 11 (AK980 PRO macros/layers)                             | UI separates host-save from device-flash; rate-limit ≤1/min                                         |
| 26. Layer-RGB indicator race          | Phase 11 (AK980 PRO layers+RGB)                                | Atomic-if-supported or retry-once-with-`IndicatorOutOfSync`-signal test                             |
| 27. Mix-in QObject diamond            | Phase 10 (first multi-mix-in device)                           | `static_assert(!is_base_of_v<QObject, IMixin>)` per mix-in; ARCH-04 doc                             |
| 28. DPI cycle order                   | Phase 12 (2.4G 8K dpi impl)                                    | Capture-replay test against captured vendor sequence                                                |
| 29. `MaturityRole` lying              | All impl phases (10, 11, 12)                                   | Per-device promotion gate: three-witness per advertised capability                                  |
| 30. AKP03 cross-family regression     | Phase 10 (AKP03 family)                                        | family-coverage test per `akp03.cpp` family member                                                  |
| 31. TimeSync glyph stale              | Phase 10 (any capability flip at runtime)                      | `Q_SIGNAL clockCapabilityChanged` + QML re-evaluation                                               |
| 32. Non-ASCII codenames               | Phase 9 (0c45:7016 naming)                                     | CI grep gate on `devices.yaml`                                                                      |
| 33. QSignalSpy granularity on Windows | All test-adding phases                                         | Idiom-grep + Windows CI run                                                                         |
| 34. `hid_open()` CI grep              | All impl phases                                                | Existing v1.1 grep gate                                                                             |
| 35. `--no-verify` under mdformat      | All phases                                                     | Zero count in commit log                                                                            |

______________________________________________________________________

## Cross-Cutting Notes

**v1.1 pitfalls that still apply unchanged to v1.2** (do not re-verify, just respect):

- **v1.1 Pitfall 1** (`IDevice` UAF during disconnect) — `shared_ptr<IDevice>` migration is done; new mix-in interfaces inherit the lifetime contract. Every new capability backend must look up via `DeviceRegistry` immediately before each HID call.
- **v1.1 Pitfall 2** (`dynamic_cast` nullptr silent no-op) — every new `dynamic_cast<I*Capable*>` site needs the null-check + `emit failed(...)` pattern.
- **v1.1 Pitfall 3** (toast-flood) — D-02 honesty contract; user-initiated = toast + glyph, auto = glyph only.
- **v1.1 Pitfall 4** (QML_SINGLETON dual-instance) — any new QML-exposed service for v1.2 (none currently planned) needs `static_assert(!is_default_constructible_v<T>)`.
- **v1.1 Pitfall 9** (HID Report ID byte 0) — every new device backend's first `transport.write` site documents the Report ID convention.
- **v1.1 Pitfall 10** (endianness on packed structs) — byte-wise writes only.
- **v1.1 Pitfall 13** (`Capability::Clock` bit-renumber lock) — adding new capability bits (none currently planned for v1.2 unless `IRgbCapable`/`IMacrosCapable`/`ILayerCapable`/`IDpiCapable`/`IDisplayCapable`/`IEncoderCapable` need new bits — likely yes) must append, never renumber.
- **v1.1 Cross-cutting "Linux-CI-blind Windows breakage"** — v1.2 explicitly avoids Windows-specific code paths but every test added needs to pass on the windows-2022 matrix.

**v1.2 explicitly does NOT need:**

- New `_putenv_s` / `Win32EnvBlock` work (v1.1 closed it).
- New trust-roots parser work (v1.1 closed it).
- New `QML_SINGLETON` services (no current plan; flag if one emerges).

______________________________________________________________________

## Sources

**Repo-internal (HIGH confidence):**

- `.planning/PROJECT.md` — v1.2 scope, Out-of-Scope rationale (no host-settable RTC), Key Decisions, parallel-agent cap.
- `CLAUDE.md` — hard rules, Qt 6/QML gotchas, cross-platform strictness, ASCII test names, mdformat workflow, `--no-verify` policy.
- `.planning/RETROSPECTIVE.md` — v1.0 + v1.1 cross-milestone lessons; parallel-execution cap; `static_assert` invariant lock pattern; D-02 honesty contract; tech_debt verdict shape.
- `.planning/MILESTONES.md` — v1.1 deferred items (real-hardware verifies, AKB980 PRO blocked on wine, captures pending for AKP815 + Mirabox N3).
- `.planning/milestones/v1.1-research/PITFALLS.md` — v1.1 pitfalls 1-16 (referenced, not duplicated).
- `docs/protocols/streamdeck/akp03.md` — AKP03 family wire-format details, 1024-byte v2 packets, per-variant image format table, action codes, NOP frames, `0x3004` unknown sibling caveat.
- `docs/_data/devices.yaml` — device catalogue, maturity tiers, capability lists, AJ-series VID/PID drift history.
- `tests/CMakeLists.txt` + `tests/integration/test_oop_plugin_host_win32_env.cpp` — existing test patterns (`MockHidEnumerator`, `Win32EnvBlock` integration test shape, hex fixture loader).

**External (HIGH confidence — verified at research time):**

- [Elgato Stream Deck HID API: General Reference](https://docs.elgato.com/streamdeck/hid/general/) — "max output report size is 1024 bytes; image must be split into chunks defined by the output report's payload." Directly corroborates Pitfall 18's chunk-size invariant.
- [Elgato Stream Deck HID API: Module 15/32](https://docs.elgato.com/streamdeck/hid/module-15_32/) — "output report of the last chunk should be marked with 0x01 in the Transfer is Done flag." Last-chunk semantics confirmed.
- [python-elgato-streamdeck (abcminiuser)](https://github.com/abcminiuser/python-elgato-streamdeck/blob/master/src/StreamDeck/Devices/StreamDeck.py) — reference chunked-upload implementation; non-AJAZZ but same protocol family shape.
- [4ndv/opendeck-akp03](https://github.com/4ndv/opendeck-akp03) — OSS corpus for AKP03 / Mirabox N3 family Rust impl; named in `docs/_data/devices.yaml:262` and `docs/protocols/streamdeck/akp03.md`.
- [mirajazz crate (crates.io)](https://crates.io/crates/mirajazz) — Rust HW library for the AJAZZ device family; referenced by `devices.yaml` for protocol_version 3 designation.
- [Calini/opendeck-akp](https://github.com/calini/opendeck-akp) — AKP05 / Mirabox N4 family OSS corpus.
- [HackTricks: USB Keystrokes from PCAP](https://hacktricks.wiki/en/generic-methodologies-and-resources/basic-forensic-methodology/pcap-inspection/usb-keystrokes.html) — corroborates Pitfall 17's privacy risk; HID boot protocol = 8-byte interrupt reports; deterministic keystroke recovery.
- [USB-Keyboard-Parser (bolisettynihith)](https://github.com/bolisettynihith/USB-Keyboard-Parser) — same shape; tool that does the recovery.
- [CTF-Usb_Keyboard_Parser](https://github.com/TeamRocketIst/ctf-usb-keyboard-parser) — pcap/pcapng-native, no tshark dependency.

**External (MEDIUM confidence):**

- [SonixQMK Docs](https://sonixqmk.github.io/SonixDocs/) — SONiX SN32F family general reference; supports Pitfall 25's NVM-wear shape (community-level knowledge, no per-AJAZZ-SKU specifics).
- [Mirajazz library description (lib.rs)](https://lib.rs/crates/mirajazz) — protocol_version 3 designation referenced in `devices.yaml`.

**External (LOW confidence — no authoritative source found, flagged inline):**

- AK980 PRO / Microdia 0c45:8009 specific RGB queue / NVM wear behaviour: web search returned no authoritative documentation. Pitfalls 24 and 25 lean on community-level 2.4G-HID knowledge + SPI-flash-generic specs. Phase 9 captures are the authoritative witness.
- AJAZZ 2.4G 8K mouse specific DPI stage cycling order: web search returned no authoritative doc. Pitfall 28's prevention strategy (capture-witness) is the right answer regardless of confidence.

**v1.0/v1.1 retrospective lessons reinforced as v1.2 guards:**

- Cap 2 concurrent execute agents (CLAUDE.md + v1.1 retrospective; Pitfall sidebar).
- `--no-verify` only when hook itself broken (Pitfall 35).
- ASCII-only test names (CLAUDE.md "Cross-platform build strictness"; Pitfall 32).
- `static_assert` invariant lock pattern (v1.0 origin; reuse in Pitfall 27 for mix-in QObject prohibition).
- D-02 honesty contract (v1.1 Phase 5 origin; reuse in Pitfalls 19 and 29 for capability and device maturity claims).
- `hid_open()` CI grep gate (v1.1 Phase 4 origin; Pitfall 34 just respects it).

**Confidence summary:**

- Repo-anchored claims (Pitfalls 18, 22, 27, 29, 30, 31, 32, 33, 34, 35): HIGH.
- Elgato Stream Deck protocol corroboration (Pitfall 18): HIGH (official Elgato docs + python-elgato-streamdeck).
- Privacy/keystroke-recovery risk (Pitfall 17): HIGH (multiple forensics tools reference identical methodology).
- USB capture layering (Pitfall 21): HIGH (dongle-decrypts-RF is a universal 2.4G-HID property).
- AKP03 `clock` false-positive (Pitfall 19): HIGH (PROJECT.md explicitly Out-of-Scope; D-02 contract; three-witness rule generalises from v1.1).
- Composite-HID dedup (Pitfall 20): MEDIUM-HIGH (composite HID is universal; AK980 PRO specifics need Phase 9 confirmation).
- Wireless 2.4G HID queueing (Pitfall 24): MEDIUM (community-level knowledge; no authoritative AJAZZ doc).
- NVM wear specifics (Pitfall 25): MEDIUM-LOW on numbers, HIGH on principle.
- Encoder quadrature-vs-delta (Pitfall 23): HIGH (universal pattern across rotary encoder hardware).
- DPI cycle order (Pitfall 28): MEDIUM (extrapolation from AJ199 family finding 11.B; specific to AJAZZ 2.4G 8K is unknown until captured).

______________________________________________________________________

*Pitfalls research for: v1.2 Connected-Device Capability Parity (brownfield C++20/Qt 6/hidapi_hidraw)*
*Researched: 2026-05-15*
*Numbering continues from v1.1 (1-16) → v1.2 (17-35) for cross-milestone reference uniqueness*
