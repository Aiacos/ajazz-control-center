# Stream Deck Parity — Requirements Quality Checklist

**Purpose**: Unit-test the *requirements* for the Stream Deck + plugin parity work — are they
clear, complete, consistent, and measurable BEFORE implementation? This does **not** test whether
the code works (that is `quickstart.md` + ctest); it tests whether the spec/plan/contracts are
well-written.
**Created**: 2026-06-18
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [contracts/](../contracts/)
**Focus**: Plugin/protocol parity · Device layer & resilience · Verification & testability
**Depth**: Standard (PR-review gate) · **Audience**: reviewer

## Requirement Completeness

- [ ] CHK001 Is the full set of inbound Elgato events that constitutes "parity" enumerated as *requirements*, rather than referenced generically? [Completeness, Spec §FR-012 vs contracts/elgato-plugin-ws]
- [ ] CHK002 Is the full set of outbound plugin→host commands required for parity enumerated, including which are MUST vs intentionally-unsupported (vendor `sendToDevice`, ~20 vendor actions)? [Completeness, Coverage, contracts/elgato-plugin-ws]
- [ ] CHK003 Is the Property Inspector dual-WebSocket requirement (separate connection, 5-arg `connectElgatoStreamDeckSocket`, instance-`context` identity) stated as a requirement, not just an implementation note? [Completeness, contracts/elgato-plugin-ws §Transport]
- [ ] CHK004 Is the `EncoderBinding::onRelease` behavior captured as a requirement (the sidecar emits `EncoderReleased`), or does it only exist as a discovered code gap? [Gap, Spec §FR-005]
- [ ] CHK005 Are the manifest-acceptance rules (normalize `Knob`→`Encoder`, accept `SDKVersion:1`, vendor/OpenDeck keys) specified as requirements? [Completeness, research C2]
- [ ] CHK006 Are owner-UUID resolution + cross-plugin denial requirements specified, including the dotted-component longest-prefix rule? [Completeness, data-model §3]

## Requirement Clarity

- [ ] CHK007 Is "same functionality as Elgato Stream Deck / OpenDeck" quantified as a specific event/command set and SDK version bound, rather than left as a vague goal? [Ambiguity, Spec §FR-012]
- [ ] CHK008 Is the required *end state* of `setTriggerDescription` and `showAlert`/`showOk` specified (e.g. a visual surface), as opposed to merely "routed"? [Clarity, Gap, contracts/elgato-plugin-ws]
- [ ] CHK009 Is the persistent-handle / "one `CRT DIS` for the session" requirement stated with observable acceptance criteria rather than as an implementation detail? [Clarity, Spec §FR-007]
- [ ] CHK010 Is "live-verified" defined as a distinct requirement (build→launch→drive→screenshot→read) separate from "ctest green"? [Clarity, Spec §FR-022]
- [ ] CHK011 Are the device families in scope vs deferred (sidecar AKP03/05/153, the AKP815 C++ carve-out, the retail-AKP05E gating) stated unambiguously? [Clarity, Scope, plan §Scope note]

## Requirement Consistency

- [ ] CHK012 Does the spec's generic FR-012 ("compatible with the event model") name the contracts/plan event list as the authoritative source, so the two do not silently diverge? [Conflict, Spec §FR-012]
- [ ] CHK013 Are the manifest *schema* (`docs/schemas/plugin_manifest.schema.json`) and the *runtime parser* requirements reconciled, given the documented over-strict-schema divergence? [Consistency, research C2]
- [ ] CHK014 Is the definition of "done per change" consistent across plan, tasks.md, and quickstart.md (tests + 3-compiler + debug-channel + objectName + docs)? [Consistency, plan/tasks/quickstart]
- [ ] CHK015 Is the parity-status source of truth consistent — i.e. does the spec/plan flag `docs/plugin-event-parity.md` as stale so it is not treated as authoritative? [Consistency, research C1]

## Acceptance Criteria Quality (Measurability)

- [ ] CHK016 Can the "never wedge" guarantee be objectively measured (an explicit idle threshold + a render-after-idle pass), rather than asserted qualitatively? [Measurability, Spec §FR-007, §US5]
- [ ] CHK017 Is "exactly one `propertyInspectorDidAppear` per open" expressed as a measurable acceptance criterion (count == 1)? [Measurability, contracts/elgato-plugin-ws]
- [ ] CHK018 Are Success Criteria SC-001…SC-008 each objectively verifiable without implementation knowledge, and do they cover the Stream-Deck stories specifically? [Measurability, Spec §SC]
- [ ] CHK019 Is the "every interactive control is debug-addressable" requirement measurable (e.g. an `objectName`-coverage criterion)? [Measurability, Spec §FR-022]

## Scenario & Edge-Case Coverage

- [ ] CHK020 Are the device-access-denied (Linux `uaccess`) and device-wedged-by-churn edge cases specified with required user-facing behavior? [Coverage, Spec §Edge Cases]
- [ ] CHK021 Are hot-plug coalescing requirements quantified (debounce window, stable final state, selection retention)? [Coverage, Spec §FR-020, §US5]
- [ ] CHK022 Are the "partial" events documented with explicit acceptance criteria — `titleParametersDidChange` default values (currently `[ASSUMED]`) and `applicationDidLaunch/Terminate` focus-approximation semantics? [Ambiguity, Coverage, contracts/elgato-plugin-ws]
- [ ] CHK023 Are the harness gaps that block headless verification (KeyCell `objectName`, modal Drawer open, `Switch.toggled`) captured as requirements, not just known limitations? [Gap, contracts/debug-control-rpc §D5]

## Dependencies & Assumptions

- [ ] CHK024 Are the PROVISIONAL hardware-gated values (encoder polarity, touch `tapPos`, zone geometry) explicitly bounded in requirements as "not guaranteed until hardware-confirmed"? [Assumption, Spec §Assumptions, research D6]
- [ ] CHK025 Are the deferred items (retail-AKP05E wire values, Windows VendorDll Wine launch, optional `SupportedDevices` SKU enforcement) recorded as explicit out-of-this-cycle assumptions rather than silently dropped? [Assumption, plan §Complexity Tracking, research D6]
- [ ] CHK026 Is the "completion, not green-field rewrite" framing (the ~80%-landed baseline) stated as an explicit assumption so requirements are not mistaken for net-new work? [Assumption, plan §Summary]

## Traceability

- [ ] CHK027 Is there an acceptance-criteria ID scheme linking spec FR/SC ↔ plan research decisions (A1–D6, C1–C2) ↔ tasks (T0xx), so each parity gap is traceable end-to-end? [Traceability]

## Notes

- Mark `[x]` when the requirement-quality question is satisfied (the spec/plan answers it), or record
  the gap inline and feed it back into `spec.md` / `plan.md` before implementing.
- ≥80% of items carry a traceability reference (`Spec §…`, contract/plan section, or a `[Gap]` /
  `[Ambiguity]` / `[Conflict]` / `[Assumption]` marker).
- This is a *requirements* gate. Behavioral verification lives in `quickstart.md` (debug-channel
  scenarios) and the ctest suite.
