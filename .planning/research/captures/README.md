# `.planning/research/captures/` — Scratch sink for raw USB captures

This directory is the canonical scratch space for raw `.pcap` /
`.pcapng` captures **during** a capture session. Everything in here is
gitignored except this `README.md` and the sibling `.gitignore`.

## Why everything is gitignored

Raw USB packet captures of an AJAZZ device on a bus that also carries
an HID keyboard contain plaintext keystrokes — every password, private
message, or SSH passphrase typed while the capture was running is
trivially recoverable with `tshark` or `USB-Keyboard-Parser`. Committing
a `.pcap` to git is a security incident, not a cosmetic mistake. See
`docs/policies/capture-data-hygiene.md` for the full privacy threat
write-up and `.planning/research/PITFALLS.md` Pitfall 17 for the
research evidence.

The pre-commit hook `scripts/reject-raw-captures.sh` will refuse to let
`*.pcap` / `*.pcapng` files (case-insensitive) land in any commit, at
any path, regardless of this `.gitignore`. The gitignore is a
**convenience** so `git add .` does not even propose them; the hook is
the authoritative rejection layer.

## What gets committed instead

Only **sanitised hex fixtures** under
`tests/integration/fixtures/<codename>/<label>.h`. These are plain-text
`std::array<uint8_t>` C++ headers reviewable in a normal `git diff`,
containing only the device-of-interest control-channel bytes (no
keyboard reports, no mouse coordinates, no wall-clock timestamps).

## The workflow

```text
tshark -i usbmonN -w cap.pcap -a duration:5
usbrply -j cap.pcap > cap.json
scripts/hex-to-cpparray.py cap.json \
    --device akp03_variant_3004 \
    --capture image-upload-first-chunk \
  > tests/integration/fixtures/akp03_variant_3004/image_upload_first_chunk.h
git add tests/integration/fixtures/akp03_variant_3004/image_upload_first_chunk.h
rm cap.pcap cap.json
```

The `cap.pcap` and `cap.json` files stay here (gitignored) only as long
as you need them. Delete them as soon as the fixture is committed.

## Future: SHA-256 + metadata index

A `.planning/research/captures/INDEX.md` file will land in a Phase 9.x
follow-up run, providing a SHA-256 + device + label + fixture-path table
for out-of-band share-able captures. The schema is sketched in
`docs/policies/capture-data-hygiene.md` § "If you must share a raw
capture out-of-band". This `README.md` will be updated to cross-link
once that index lands.

## See also

- `docs/policies/capture-data-hygiene.md` — full policy doc.
- `docs/protocols/CAPTURING.md` — per-device capture runbook (lands in
  plan 09-02).
- `scripts/hex-to-cpparray.py` — the `usbrply` JSON → C++ header
  pipeline (lands in plan 09-03).
- `.planning/research/PITFALLS.md` Pitfall 17 — privacy threat write-up.
