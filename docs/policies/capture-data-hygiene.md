# Capture-Data Hygiene Policy

## Why this exists

USB packet captures of an AJAZZ device — captured with `tshark`, `tcpdump`,
or Wireshark USBPcap from the `usbmon` kernel module — contain plaintext HID
reports for every device on the captured bus. If the bus includes the AK980
PRO keyboard (or any HID boot-protocol keyboard at all) during normal use,
the resulting `.pcap` file embeds **every keystroke** the user typed while
the capture was running. HID boot-protocol keyboards emit 8-byte interrupt
reports with a modifier byte and up to six concurrent keycodes; CTF-grade
tools (`tshark` itself, [USB-Keyboard-Parser](https://github.com/bolisettynihith/USB-Keyboard-Parser),
[CTF-Usb_Keyboard_Parser](https://github.com/TeamRocketIst/ctf-usb-keyboard-parser),
[HackTricks: USB Keystrokes](https://hacktricks.wiki/en/generic-methodologies-and-resources/basic-forensic-methodology/pcap-inspection/usb-keystrokes.html))
reconstruct the keystroke stream **deterministically** from raw `.pcap`
files. There is no encryption at the HID layer even for 2.4 GHz wireless
keyboards — the dongle decrypts the RF link before presenting HID to the
host, so `usbmon` sees plaintext keycodes regardless of wireless link
encryption. A `.pcap` checked in "for replay tests" leaks any password,
private message, or SSH passphrase the researcher happened to type during
the capture window. This is a security incident, not a cosmetic mistake —
see `.planning/research/PITFALLS.md` Pitfall 17 for the full risk write-up.

This policy is enforced at **commit time** by the
`scripts/reject-raw-captures.sh` pre-commit hook. The hook MUST land
*before* any researcher takes a first capture; that is why CAPTURE-01 is
the MUST-FIX-FIRST plan (09-01) of v1.2 Phase 9.

## What is forbidden in the repo

The following will be **rejected at commit time** by the pre-commit hook:

- **Any `*.pcap` or `*.pcapng` file** (case-insensitive — `*.PCAP`,
  `*.Pcapng`, etc. are all matched) at any path in the working tree.
  There is no carve-out for "sanitised" or "device-only" captures: if you
  need device-only bytes, extract them to a hex fixture (see the
  sanitised workflow below). If you need a raw capture for out-of-band
  analysis, keep it out of the repo entirely.
- **Any binary file larger than 10 KB under `.planning/research/captures/`.**
  The captures sink directory is intended as scratch space *during* a
  capture session — raw `.pcap`, intermediate `usbrply` JSON, and the
  like — all of which are gitignored. The 10 KB binary guardrail catches
  any binary blob with a non-`.pcap` extension (e.g. a raw `usbrply`
  C-output dump, a `/dev/hidrawN` recording, a `.cap`, `.dump`, or `.bin`)
  before it sneaks past the extension blocklist.
- **Any committed SHA-256 reference to a capture without the matching
  sanitised hex-fixture extracted from it.** A capture index that points
  to bytes nobody else can verify is worse than no index — it implies
  the bytes are recoverable when they are not. Future
  `.planning/research/captures/INDEX.md` entries (deferred to Phase 9.x)
  MUST be accompanied by the corresponding header file under
  `tests/integration/fixtures/<codename>/`.

The hook's rejection message points the offender at this document and at
`scripts/hex-to-cpparray.py` for the sanitised workflow.

## The sanitised workflow

Every committed capture-derived artifact is a plain-text C++ header
produced by `scripts/hex-to-cpparray.py`. The raw `.pcap` and the
intermediate `usbrply` JSON stay ephemeral and out-of-tree:

```text
# 1. Capture (raw .pcap is ephemeral, stays under .planning/research/captures/
#    which is gitignored; or /tmp for absolute safety).
tshark -i usbmonN -w /tmp/cap.pcap -a duration:5

# 2. Decode the USB-HID stream to JSON (usbrply is the agreed tool — see
#    research SUMMARY and PROJECT.md STACK § "Test-replay infrastructure").
usbrply -j /tmp/cap.pcap > /tmp/cap.json

# 3. Extract the device-of-interest control-channel bytes into a hex header.
#    The script filters by --device codename and labels the capture; the
#    output is a plain-text std::array<uint8_t> ready to feed MockTransport.
scripts/hex-to-cpparray.py /tmp/cap.json \
    --device akp03_variant_3004 \
    --capture image-upload-first-chunk \
  > tests/integration/fixtures/akp03_variant_3004/image_upload_first_chunk.h

# 4. Commit ONLY the header file.
git add tests/integration/fixtures/akp03_variant_3004/image_upload_first_chunk.h

# 5. Delete the raw + intermediate immediately. There is no reason to keep
#    them around once the fixture is committed and reviewable.
rm /tmp/cap.pcap /tmp/cap.json
```

The header file is plain text, reviewable in a normal `git diff`, contains
only the bytes of interest for the device under test, and carries no
boot-protocol keyboard reports, mouse-coordinate reports, or wall-clock
timestamps that could fingerprint user activity.

`scripts/hex-to-cpparray.py` lands in plan 09-03; this policy doc
references it by path so the workflow is documented as soon as the
hygiene boundary lands.

## Pre-commit enforcement

The pre-commit hook is `scripts/reject-raw-captures.sh`, registered as a
local hook in `.pre-commit-config.yaml` (search for `reject-raw-captures`).
It runs on every `git commit` and inspects each staged path:

- **Extension blocklist** (case-insensitive): any path whose basename ends
  in `.pcap` or `.pcapng` is rejected outright.
- **Size guardrail under the captures sink**: any path under
  `.planning/research/captures/` that is not the README or the
  `.gitignore`, is binary, and exceeds 10240 bytes, is rejected.

A rejection message of the following shape is printed to stderr:

```text
REJECTED: <path>
  Reason: <extension blocklist | captures-sink size guardrail>
  Remediation: See docs/policies/capture-data-hygiene.md and use
  scripts/hex-to-cpparray.py to produce a sanitised fixture under
  tests/integration/fixtures/<codename>/.
```

The hook does **not** rely on the global `exclude:` block in
`.pre-commit-config.yaml`. The global exclude suppresses lint *noise* on
legacy paths; this hook is the authoritative *rejection* layer.

## If you must share a raw capture out-of-band

A raw `.pcap` is occasionally legitimately useful — for example, when two
researchers want to confirm whether a vendor opcode is identical between
firmware revisions and one of them is on Windows running USBPcap. In
those cases:

- Share the file via an **encrypted scratch channel**: Signal attachment,
  encrypted email, in-person USB stick. **Never** through an issue
  tracker, public chat, gist, or any path that lands the file on a
  durable HTTP-accessible URL.

- Index the capture in a future `.planning/research/captures/INDEX.md`
  (deferred to Phase 9.x) by **SHA-256 only**, alongside the matching
  sanitised hex-fixture path. The schema is:

  | SHA-256 (full)      | Device codename    | Label                    | Date       | Researcher | Fixture path                                                             |
  | ------------------- | ------------------ | ------------------------ | ---------- | ---------- | ------------------------------------------------------------------------ |
  | abc123…64-char-hex… | akp03_variant_3004 | image-upload-first-chunk | 2026-05-15 | (initials) | tests/integration/fixtures/akp03_variant_3004/image_upload_first_chunk.h |

- The sanitised hex fixture MUST land in the same commit as the index
  row. An index row without a fixture is not reviewable and gets the
  reviewer no closer to verifying the bytes — it just announces that
  bytes exist somewhere.

## Anti-features (do NOT add)

The following are explicitly out of scope, will be rejected at code
review regardless of how they are framed, and need not be re-litigated:

- **No live in-app USB sniffer.** The control center does not bundle
  `usbmon` capture, `libpcap`, or any HID-traffic logging beyond what is
  necessary to debug the immediate request/response of the focused
  device. Even debug builds.
- **No telemetry upload of captures.** Bug reports do not include packet
  dumps. If a user-facing bug report wants to attach a capture, the bug
  report template instructs them to share the file out-of-band per the
  section above.
- **No auto-upload of pcap to a vendor.** The control center does not
  call home with USB traffic. Period.
- **No libpcap dependency in the agent.** `scripts/hex-to-cpparray.py`
  consumes `usbrply` JSON, not raw `.pcap` — see Pitfall 22 and the
  STACK § "Test-replay infrastructure" decision. `libpcap` stays out of
  the in-tree codebase and out of the wheel/distribution.

______________________________________________________________________

Source: `.planning/research/PITFALLS.md` Pitfall 17 ("USB capture corpus
contains user keystrokes / passwords"). Policy plan: 09-01 (CAPTURE-01).
