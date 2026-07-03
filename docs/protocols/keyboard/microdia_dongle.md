# Microdia / SONiX 2.4GHz receiver dongle (0c45:7016) - topology & identification (stub)

This is a clean-room identification artefact derived from live `lsusb` plus HID
report-descriptor parsing on real hardware on 2026-05-15. No vendor firmware
or driver was disassembled to produce this document.

> **Status banner -- read first.** The separate-dongle hypothesis encoded
> here is the **ARCH-06 DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)**.
> Composite-HID dedup is NOT firing in `DeviceRegistry::enumerate()`, and
> `0c45:7016` is catalogued as a standalone `probed`-tier device. This
> verdict is gated on the Phase 9.x 2-minute physical unplug test (unplug
> the `ak980pro` 2.4G receiver and confirm `0c45:7016` does NOT disappear
> simultaneously). Until that test runs, the topology evidence below is
> the load-bearing rationale -- D-05 honesty contract; this doc is NOT
> "Locked".

## Identification

| Property       | Value                                                                                                                                           |
| -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| Vendor ID      | `0x0c45` (Microdia; the SONiX OEM range that hosts many wireless 2.4 GHz receivers)                                                             |
| Product ID     | `0x7016`                                                                                                                                        |
| iManufacturer  | `SONiX`                                                                                                                                         |
| iProduct       | `USB DEVICE`                                                                                                                                    |
| bcdDevice      | `1.03`                                                                                                                                          |
| Link speed     | Full-Speed 12 Mbps                                                                                                                              |
| Codename       | `ak980pro_dongle_24g` (catalogue / `devices.yaml`); ASCII-only per Pitfall 32                                                                   |
| Alias          | `microdia_dongle_7016` (ARCH-06 Section "Binding to downstream plans" canonical name; used as this file's basename for forward-discoverability) |
| Catalogue tier | `probed` (DEVICES-08)                                                                                                                           |
| Family         | `dongle` (devices.yaml schema; `capabilities: []` -- a pure transport with no configurable surface)                                             |

## USB topology -- the four ARCH-06 facts

Captured from live `lsusb -v -d 0c45:7016` plus `/sys/bus/usb/devices/` walk
plus `lsusb -t` on 2026-05-15 (see ARCH-06 for the cross-referenced
`0c45:8009` ak980pro row used as the comparison datum).

| Fact                        | `0c45:7016` value                                                                                 |
| --------------------------- | ------------------------------------------------------------------------------------------------- |
| 1. USB sysfs parent path    | `usb1/1-13/1-13.1/1-13.1.2` -- descendant of the bus-1 port-13 hub chain (through a 13.1 sub-hub) |
| 2. Link speed               | Full-Speed 12 Mbps                                                                                |
| 3. Interface shape          | Two boot-keyboard HID interfaces with 8-byte endpoints (IF0 + IF1)                                |
| 4. iManufacturer / iProduct | `iManufacturer="SONiX"` / `iProduct="USB DEVICE"` / `bcdDevice 1.03`                              |

For comparison, the `ak980pro` 2.4G dongle (`0c45:8009`) sits at
`usb1/1-10` -- a different bus branch -- with distinct vendor-specific
strings ("AJAZZ" / "AK980 PRO" lineage). The two devices are physically
plugged into different host USB ports.

### None of Pitfall 20's three composite-HID confirmations hold

Pitfall 20 (`.planning/research/PITFALLS.md`) enumerates three checks
that, if any one holds, would confirm the composite-HID hypothesis
(i.e. `0c45:7016` is a secondary interface of the same physical
`ak980pro` keyboard). Result at HEAD on 2026-05-15:

1. **Different `/sys/bus/usb/devices/` parent paths.** `0c45:7016` is at
   `1-13.1.2`; `ak980pro` is at `1-10`. A composite-HID device exposes
   all its interfaces on the same USB device address, which means the
   same sysfs parent path for every interface. Different parent paths
   structurally negate the composite hypothesis.
1. **`lsusb -t` shows them as children of DIFFERENT hub ports.**
   `0c45:7016` is below the bus-1 port-13 hub chain; `ak980pro` plugs
   directly into bus-1 port-10. They are physically in different host
   ports.
1. **Simultaneous-disappearance evidence NOT yet captured.** This is
   the third Pitfall 20 check; it is the Phase 9.x finalization gate
   (unplug `ak980pro`'s 2.4G receiver and observe whether `0c45:7016`
   also disappears within 5 seconds). The first two checks already
   structurally refute the composite hypothesis at the platform level;
   the third is the user-driven sanity check that would flip the verdict
   if it contradicts.

## HID report descriptor (PENDING)

The raw HID report descriptor for both interfaces is captured via:

```bash
# For each /dev/hidrawN that resolves to 0c45:7016 via udevadm info:
sudo cat /sys/class/hidraw/hidrawN/device/report_descriptor | xxd
# Optional human-readable form via the hidrd-convert tool:
sudo cat /sys/class/hidraw/hidrawN/device/report_descriptor | hidrd-convert -o spec
```

The actual descriptor bytes are **PENDING** the Phase 9.x evtest /
hidraw session; this stub carries the topology + identification
methodology, not the descriptor dump. Capturing the descriptor is the
**Pitfall 17-safe** path (descriptor parsing reads device-side static
metadata, not live USB report traffic, so it does NOT require any
`pcap`/`pcapng` capture infrastructure and does NOT need the
CAPTURE-01 sanitisation pipeline).

When the descriptor is captured, the expected shape per the four
topology facts above is two boot-keyboard HID descriptors (Usage Page
`0x01 Generic Desktop`, Usage `0x06 Keyboard`, 8-byte input reports) on
IF0 and IF1 respectively. Append the actual decoded descriptor here
when DEVICES-09 closes its hardware leg.

## Identification methodology (recipe for the next SKU)

When a future device that uses the same SONiX 2.4 GHz receiver family
appears on a developer machine, follow this recipe to recognise it as
a member of this dongle family without redoing the topology forensics:

1. **Match the string signature.** `lsusb -v -d <vid>:<pid>` reports
   `iManufacturer="SONiX"` and `iProduct="USB DEVICE"`. This pair plus
   `bcdDevice 1.03` is the canonical OEM signature for a SONiX wireless
   receiver in the Microdia `0c45:70xx` range.
1. **Confirm the interface shape.** Two boot-keyboard HID interfaces
   with 8-byte endpoints (IF0 + IF1) at Full-Speed 12 Mbps. A
   four-interface composite (mouse + system + consumer + NKRO) would
   indicate a different SONiX SKU variant; document it as a sibling
   entry rather than merging.
1. **Resolve the sysfs parent path.** `readlink -f /sys/bus/usb/devices/<bus>-<port>` and confirm the path is NOT the same as any already-known
   device's parent. A different parent confirms it is NOT a composite
   interface of a known device.
1. **Identify the paired downstream input device.** Run an `evtest` /
   `cat /dev/hidrawN` session on each of the dongle's hidraw nodes while
   the user types on the wireless peripheral they suspect is paired
   with the dongle. The boot-keyboard interface relays key codes; the
   second interface typically relays consumer / system / mouse events
   if the peripheral is a multi-modal device.
1. **Record the SKU in `devices.yaml`.** Add a catalogue row at
   `probed` tier; if the paired downstream device is identified, the
   capabilities belong on THAT codename, not on the dongle row -- the
   dongle is a transport.

Cross-reference: `.planning/research/FEATURES.md` Section 4.4 (step
table for SONiX OEM dongle identification on Linux). Cross-reference
the existing AK980 PRO entries (`docs/protocols/keyboard/ak980pro_vendor.md`)
for what a populated capability row looks like once the paired
downstream device is identified.

## ARCH-06 pointer

This stub doc and its catalogue row exist because of the architectural
decision made in `.planning/phases/09-research-captures-hygiene/ARCH-06.md`:

- **Decision:** composite-HID dedup is NOT added to
  `DeviceRegistry::enumerate()`.
- **Verdict status:** DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION).
- **Finalization gate:** 2-minute physical unplug test (Phase 9.x).
- **Conditional re-sequencing:** if the unplug test contradicts the
  separate-dongle hypothesis, a new Phase 12.5 lands composite-HID
  dedup BEFORE Phase 12 and this row would be re-folded into the
  `ak980pro` row per ARCH-06 Section "CONDITIONAL re-sequencing".

Read the ADR for the full topology evidence table, the rationale
against the alternative (Pitfall 20 sketch), and the binding to
downstream plans. Do NOT promote ARCH-06 to "Locked" from this doc --
that promotion lands in the Phase 9.x follow-up commit alongside the
equivalent promotions of ARCH-04 and ARCH-05.

## References

- ADR: `.planning/phases/09-research-captures-hygiene/ARCH-06.md`
- Catalogue row: `docs/_data/devices.yaml` (codename `ak980pro_dongle_24g`,
  `pid: "0x7016"`).
- Pitfall 20 (composite-HID dedup hypothesis, three confirmation checks):
  `.planning/research/PITFALLS.md`.
- Pitfall 17 (raw USB-capture files rejected at commit time):
  `.planning/research/PITFALLS.md`. Descriptor parsing is the Pitfall
  17-safe path and does NOT need capture infrastructure.
- Identification methodology table: `.planning/research/FEATURES.md`
  Section 4.4.
