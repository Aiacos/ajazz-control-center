# Feature Specification: AJAZZ Control Center — Product Baseline

**Feature Branch**: `001-control-center-baseline`

**Created**: 2026-06-18

**Status**: Draft

**Input**: User description: "Study the specify from the old planning and requirements analyzing the whole repo. Delete the previous planning and create new one following Specify rules"

> **Note**: This is a consolidation specification. It re-states the validated product
> requirements accumulated across the previous milestones (v1.0 → v2.0) and the live
> codebase as a single, forward-looking Spec Kit baseline. It supersedes the prior
> `.planning/` GSD artifacts as the source of truth for **what** the product delivers and
> **why**. Implementation specifics (frameworks, wire formats, file layout) are intentionally
> excluded — they live in `plan.md` and the existing `docs/` and reverse-engineering corpus.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Configure what a device's keys, dials, and touch zones do (Priority: P1)

A user connects a supported AJAZZ device (a Stream Dock panel such as the AKP05E, or another
supported controller), sees it represented on screen as a canvas that mirrors the physical
layout, drags an action onto a specific key / encoder-dial / touch zone, and the physical
device immediately reflects the binding — the key renders the action's image, and pressing the
key or turning the dial performs the action.

**Why this priority**: This is the core product loop. Without live, device-shaped binding of
actions to controls, the product has no reason to exist. Every other story builds on it.

**Independent Test**: With one supported device attached, a user can drag a built-in action
onto a key, observe the key image render on the hardware, trigger the control, and observe the
action fire — end-to-end, with no plugins or profiles involved.

**Acceptance Scenarios**:

1. **Given** a supported device is attached and selected, **When** the user drags an action
   onto a key on the on-screen canvas, **Then** the binding persists and the physical key
   renders the action's image.
1. **Given** a key is bound to an action, **When** the user presses that physical key, **Then**
   the bound action executes.
1. **Given** a device with encoder dials and a touch strip, **When** the user binds actions to
   a dial and a touch zone, **Then** rotating the dial and tapping the zone each trigger their
   bound action.
1. **Given** a device is selected, **When** the user adjusts the brightness control, **Then**
   the panel brightness changes live.

______________________________________________________________________

### User Story 2 - Install and use third-party plugins (Priority: P2)

A user browses an in-app catalog of plugins, installs one (or sideloads a Stream Deck
`.sdPlugin` package), and binds the plugin's actions to device controls. Plugin actions that
expose configuration present a Property Inspector panel where the user sets options, and those
options take effect on the device.

**Why this priority**: Plugin compatibility (Elgato Stream Deck / OpenDeck ecosystem) is the
primary driver of device usefulness beyond built-in actions. It is the largest differentiator
and the most-requested capability, but it depends on Story 1's binding loop existing first.

**Independent Test**: A user installs a known plugin (e.g. a system-monitor plugin) from the
in-app catalog, binds one of its actions to a key, and sees live data render on that key —
without writing any code.

**Acceptance Scenarios**:

1. **Given** the plugin catalog is open, **When** the user selects a plugin and installs it,
   **Then** the plugin's actions become available in the action picker.
1. **Given** a `.sdPlugin` package file, **When** the user sideloads it, **Then** it is
   extracted safely and its actions appear without overwriting unrelated files.
1. **Given** a plugin action with configurable settings, **When** the user opens its Property
   Inspector and changes a setting, **Then** the setting round-trips to the plugin and the
   device output updates.
1. **Given** an installed plugin, **When** its action is bound to a key and produces output,
   **Then** the output renders on the physical key in real time.
1. **Given** a plugin is unsigned or its manifest is untrusted, **When** the user attempts to
   load it, **Then** the system requires explicit user consent before running it.

______________________________________________________________________

### User Story 3 - Organize controls into profiles, including per-application switching (Priority: P2)

A user creates multiple profiles (different sets of bindings) for the same device and switches
between them. The user can also map a profile to a specific desktop application so that the
device automatically switches to that profile when the application comes to the foreground.

**Why this priority**: Profiles turn a single device into many context-specific control
surfaces, multiplying its value. Per-app auto-switching is the convergent expectation set by
competing tools. It depends on bindings (Story 1) existing.

**Independent Test**: A user creates two profiles with different bindings, switches between
them manually and sees the device repaint, then maps one profile to an application and confirms
the device switches when that application is focused.

**Acceptance Scenarios**:

1. **Given** a device with two profiles, **When** the user switches the active profile,
   **Then** the device's keys repaint to the new profile's bindings and the change persists.
1. **Given** a profile mapped to an application, **When** that application gains foreground
   focus, **Then** the active profile switches automatically.
1. **Given** a desktop environment that cannot report the foreground application, **When** the
   user opens per-app profile settings, **Then** the system clearly warns that auto-switching
   is unavailable rather than silently failing.

______________________________________________________________________

### User Story 4 - Compose advanced bindings: multi-action and toggle (Priority: P3)

A user binds a single control to either a sequence of actions that run in order on one press
(multi-action) or a multi-state action that advances to its next state on each press (toggle),
with each state carrying its own image, title, and settings.

**Why this priority**: Multi-action and toggle bindings are standard Stream Deck capabilities
that power real workflows (macros, on/off controls). They are an enhancement on top of the
single-action binding of Story 1.

**Independent Test**: A user binds a key to a two-step multi-action and confirms both steps run
on a single press; binds another key to a three-state toggle and confirms each press advances
the state and repaints the key.

**Acceptance Scenarios**:

1. **Given** a key bound to a multi-action of N steps, **When** the key is pressed once,
   **Then** all N steps execute in order.
1. **Given** a key bound to a toggle action of N states, **When** the key is pressed, **Then**
   the current state advances by one (wrapping after the last) and the key repaints to the new
   state's image; the new state persists across restart.

______________________________________________________________________

### User Story 5 - Trust the device list: honest capability and resilient hot-plug (Priority: P1)

A user sees only the controls a device genuinely supports, with each device's maturity honestly
labeled. When a device is unplugged the application does not crash; it marks the device offline,
retains the user's selection and scroll position, and silently reconnects when the device
returns.

**Why this priority**: "Never lie about what a device can do, never crash when a device is
yanked" is the product's stated core value and trust foundation. A control center that crashes
on unplug or advertises non-working features is unusable regardless of features. This is a
cross-cutting non-negotiable, hence P1 alongside Story 1.

**Independent Test**: With a device attached, unplug it during use and confirm the app stays
running, shows an offline badge, keeps the selection, and reconnects on replug; inspect a
device whose feature is unimplemented and confirm that feature is not falsely presented as
working.

**Acceptance Scenarios**:

1. **Given** a device is attached and selected, **When** it is physically unplugged, **Then**
   the application continues running, marks the device offline, and preserves the user's
   selection and list position.
1. **Given** a device was unplugged, **When** it is reconnected, **Then** it returns to the
   list and is usable again without a restart.
1. **Given** a rapid sequence of connect/disconnect events (e.g. a USB hub re-enumeration),
   **When** the events arrive, **Then** the list settles to the correct final state without
   flicker or duplicate entries.
1. **Given** a device whose capability is not implemented, **When** the user views that device,
   **Then** the capability is shown as unavailable (e.g. a marker/tooltip), never as a working
   feature, and invoking it never reports a false success.

______________________________________________________________________

### User Story 6 - Install and run across Linux, Windows, and macOS (Priority: P3)

A user on Linux, Windows, or macOS installs the application from a platform-native package and
runs it with the same core capabilities.

**Why this priority**: Cross-platform reach broadens the audience, but a single well-supported
platform already delivers the core value, so this is an amplifier rather than a prerequisite.

**Independent Test**: The application installs from its native package on each platform and
launches to a working device list.

**Acceptance Scenarios**:

1. **Given** a supported operating system, **When** the user installs the native package,
   **Then** the application launches and can enumerate attached supported devices.
1. **Given** the application runs on any supported platform, **When** the user performs the
   Story 1 binding loop, **Then** it behaves equivalently across platforms.

______________________________________________________________________

### Edge Cases

- **Device access denied (Linux)**: When the operating system grants no permission to open a
  device node, the device appears in the list but cannot be driven; the user is given a clear,
  actionable explanation rather than a silent failure.
- **Device wedged by churn**: Repeatedly opening and closing a panel must not put it into a
  state where it stops rendering; a persistent connection for the session is the expected
  behavior.
- **Plugin crashes mid-interaction**: When a plugin process dies, the application keeps running
  and the rest of the system is unaffected; a crashing plugin does not consume unrelated state.
- **Malicious plugin package**: A package that attempts to write outside its install directory
  (path traversal / "zip-slip") is rejected; extraction is contained.
- **Unimplemented capability invoked**: Triggering a capability a device lacks returns an honest
  "not implemented" outcome, never a fabricated success.
- **Provisional/unconfirmed hardware behavior**: Where device behavior is known only
  provisionally (not yet hardware-confirmed), the product must not present it as guaranteed.
- **Foreground-app detection unavailable**: On environments without a foreground-application
  signal, per-app auto-switching degrades to a clearly-communicated manual mode.

## Requirements *(mandatory)*

### Functional Requirements

**Device control (core)**

- **FR-001**: System MUST enumerate attached supported AJAZZ devices and present each as a
  selectable entry showing its identity and honest maturity/capability status.
- **FR-002**: System MUST present the selected device as an on-screen canvas whose layout
  mirrors the physical control surface (keys, encoder dials, touch zones).
- **FR-003**: Users MUST be able to bind an action to a specific key, encoder dial, or touch
  zone by dragging it directly onto that control on the canvas.
- **FR-004**: System MUST render bound-action imagery onto the physical device controls in real
  time and keep it correct after profile or state changes.
- **FR-005**: System MUST deliver physical input events (key press, dial rotation, dial press,
  touch) to the bound action.
- **FR-006**: Users MUST be able to adjust supported device output settings (e.g. brightness)
  with the change taking effect live.
- **FR-007**: System MUST maintain a persistent connection to a device for the session rather
  than opening/closing it per interaction, to avoid putting hardware into a non-rendering state.

**Plugins**

- **FR-008**: System MUST run third-party plugin code out-of-process, isolated from the main
  application, so that a plugin fault cannot crash or corrupt the application.
- **FR-009**: System MUST support installing plugins from an in-app catalog and sideloading
  Stream Deck `.sdPlugin` packages from a file.
- **FR-010**: System MUST extract plugin packages safely, rejecting any package that attempts to
  write outside its designated install location.
- **FR-011**: System MUST gate the loading of unsigned or untrusted plugins behind explicit user
  consent.
- **FR-012**: System MUST be compatible with the Stream Deck / OpenDeck plugin event model so
  that existing ecosystem plugins function (action lifecycle, settings, and Property Inspector
  events). The authoritative, exhaustive inbound/outbound event and command set this requirement
  refers to is defined in `contracts/elgato-plugin-ws.md`; that contract governs when this prose and
  the contract differ.
- **FR-013**: System MUST present a Property Inspector for plugin actions that expose
  configuration, and changes made there MUST round-trip to the plugin and update device output.
- **FR-014**: System MUST source plugin catalog content without requiring user accounts or
  emitting usage telemetry.

**Profiles & bindings**

- **FR-015**: Users MUST be able to create, switch between, and persist multiple profiles per
  device.
- **FR-016**: Users MUST be able to map a profile to a desktop application so the active profile
  switches automatically when that application gains foreground focus.
- **FR-017**: System MUST support multi-action bindings (a single press runs an ordered sequence
  of actions) and toggle bindings (each press advances through per-state image/title/settings),
  with state persisting across restarts.

**Honesty, resilience, and trust (cross-cutting, non-negotiable)**

- **FR-018**: System MUST advertise only capabilities a device genuinely supports and MUST mark
  each device with an honest maturity level; invoking an unsupported capability MUST return an
  explicit "not implemented" result, never a fabricated success.
- **FR-019**: System MUST remain running when a device is removed, marking it offline while
  preserving the user's current selection and list position.
- **FR-020**: System MUST silently reconnect a returning device without requiring an application
  restart, and MUST coalesce rapid connect/disconnect bursts into a stable final state.
- **FR-021**: System MUST never leak host-process environment or secrets into plugin child
  processes.

**Validation & quality (product-level)**

- **FR-022**: Every user-facing interactive control MUST be drivable and observable through an
  automated control/validation channel so that behavior can be verified without manual GUI
  interaction.
- **FR-023**: Device and protocol behavior MUST be grounded in the project's reverse-engineering
  corpus and confirmed against real hardware before being presented as supported; where hardware
  and documentation disagree, hardware governs.

**Cross-platform & packaging**

- **FR-024**: System MUST run on Linux (primary), Windows, and macOS, delivering the core device
  and plugin capabilities on each.
- **FR-025**: System MUST be distributable as platform-native packages for each supported
  operating system.

### Key Entities *(include if feature involves data)*

- **Device**: A physical AJAZZ controller (Stream Dock panel, keyboard, or mouse). Attributes:
  identity, family/SKU, control layout (keys/dials/touch zones), supported capabilities, maturity
  level, connection state (online/offline).
- **Profile**: A named set of bindings for a device. Attributes: device association, the set of
  control→binding assignments, optional application-association hints for auto-switching, active
  state.
- **Binding**: The assignment of an action to a single control. May be a single action, a
  multi-action sequence, or a multi-state toggle.
- **Action**: A unit of behavior bound to a control — built-in or provided by a plugin. May carry
  per-state image, title, and settings.
- **Plugin**: An installed, out-of-process extension providing actions and optional Property
  Inspector configuration UI. Attributes: identity/manifest, trust/signature status, install
  source, running state.
- **Catalog Entry**: A plugin available for installation from the in-app catalog. Attributes:
  identity, source, install metadata.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A new user can bind an action to a device control and see it work on the physical
  device in under 2 minutes from launch, without reading documentation.
- **SC-002**: Unplugging any supported device during use never crashes the application
  (0 crashes across repeated unplug/replug cycles), and the device reappears and is usable after
  replug with no restart.
- **SC-003**: A user can install a plugin from the in-app catalog and have its action render live
  on a key within 5 minutes, with no account creation and no manual file editing.
- **SC-004**: 100% of device capabilities presented as available are genuinely functional on the
  hardware; no capability is shown as working while returning a fabricated success.
- **SC-005**: A user can create a second profile and switch to it, with the device repainting
  correctly, in under 1 minute.
- **SC-006**: Configuration changes made in a plugin's Property Inspector are reflected in device
  output within 3 seconds, with the setting surviving an application restart.
- **SC-007**: Every interactive control in the application can be exercised and its result
  observed through the automated validation channel, enabling regression verification without
  manual GUI steps.
- **SC-008**: The core binding loop (Story 1) behaves equivalently on all three supported
  operating systems.

## Assumptions

- **Hardware availability for confirmation**: Some device behaviors (notably encoder/touch input
  decoding on certain Stream Dock units) are currently known only provisionally and require a
  retail unit to confirm. The spec treats such behaviors as "supported once hardware-confirmed,"
  not as guaranteed today.
- **Device families in scope**: The Stream Dock panel family is the primary target for the
  binding-and-plugin experience; supported keyboards and mice participate as devices but are not
  full stream-controller surfaces. The exact SKU support matrix is maintained in the device
  catalog, not frozen in this spec.
- **Plugin ecosystem reference**: Compatibility targets the established Stream Deck / OpenDeck
  plugin model; the product is the host, and existing ecosystem plugins are the content.
- **No accounts, no telemetry**: The product collects no usage telemetry and requires no user
  account; plugin catalog access is anonymous.
- **Clean-room only**: Device support is derived from clean-room reverse engineering; no vendor
  driver code or bundled vendor installers are used.
- **Out of scope for this baseline**: world-clock/NTP features, device→host time read-back,
  per-device timezone handling, vendor-driver bundling, and any "lying success" UX are explicitly
  excluded (carried forward from prior milestone scope decisions).
- **Forward direction (informative, not required here)**: Native support for additional
  stream-controller hardware (e.g. Elgato-class panels) is anticipated as a future milestone and
  is not a requirement of this baseline.
- **Reuse of existing implementation**: This baseline documents an existing, shipping product;
  it consolidates and re-states accumulated requirements rather than commissioning a rewrite.
