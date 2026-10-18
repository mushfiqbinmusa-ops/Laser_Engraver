# Agent Build Guide — Laser XY Scanner Production System

**Audience:** an autonomous coding agent starting this task fresh, with no prior chat history.
**Goal:** turn this repository into the v1.0 production machine-control system described in
`plans/laser-engraver-production-roadmap.md` — by **first deleting the existing prototypes**,
then building firmware, server, and UI from the verified hardware facts and the milestone plan.

---

## 0. How to use this guide

1. Read these files before writing any code, in this order:
   - `plans/laser-engraver-production-roadmap.md` — **the authority.** It defines the 12 milestones, contracts, safety model, and the definition of "production-ready". If this guide and the roadmap ever disagree, the roadmap wins.
   - `report.md` — baseline critique of what the prototypes got wrong.
   - This file — the execution layer: what to delete, what to trust, and the constraints.
2. Check `git status` and the current worktree before every change. **Preserve any pre-existing user changes** you did not make.
3. Do not rely on conversation history or assumptions. Read the actual source and tests each time.
4. Work one roadmap milestone per reviewable change set. Do not batch unrelated work.

---

## 1. Non-negotiable safety invariants

Violating any of these is a defect, regardless of features delivered.

- **Laser is OFF at boot, reset, watchdog, fault, and disconnect.** Both the software writes *and* the reset/boot electrical state must be verified.
- **The firmware independently validates every command.** Bounds, state legality, homing prerequisites, and laser limits are enforced on-device. The server and UI never become the only guard.
- **No success-shaped fallbacks.** A timeout, disconnect, or parse failure is an explicit error — never translated into `"ok"`.
- **Unknown / unsupported commands are rejected explicitly** — never accepted as a no-op.
- **Position is marked invalid the moment its physical trust is lost** (unhome, fault, abrupt halt).
- **Every terminal state ends with laser off and motion stopped.**
- **Do not commit credentials.** The existing `stepper_ir_homing_test/stepper_ir_homing_test.ino` contains a hardcoded Wi-Fi SSID/password; do **not** copy it. Use local ignored config or provisioning.

Until roadmap Step 5's hardware safety gate passes, all back-end and UI work runs against a **fake controller** or an **explicitly laser-disabled profile**. No route — API or UI — may energize the real laser.

---

## 2. Phase 0 — Delete the prototypes (do this first)

The current firmware and server/UI are throwaway prototypes with known correctness defects
(blocking loops, silent timeouts, coil-toggle motion that never truly homes, an SVG→G-code
pipeline the firmware cannot parse). Delete them before building v1.0 so no defect is inherited.

**Delete:**

```bash
git rm -r laser-engraver-firmware/
git rm -r laser-engraver-server/
git commit -m "chore: remove prototype firmware and server/UI"
```

**Keep as read-only reference evidence** (do not copy as production source):

| Path | Why keep |
| --- | --- |
| `laser_pwm_test/` | Verified laser PWM pin/config + safety-cap learnings. |
| `stepper_ir_homing_test/` | Verified A4988 pin map, homing state machine, sensor filter, non-blocking transport. |
| `stepper_motor_test/`, `ir_sensor_test/` | Bench characterization guides and wiring. |
| `Laser_XY_Scanner/`, `tests/` | Original sketch and host-side homing logic test. |
| `resources/`, `BOM.md`, `report.md` | Reference docs and baseline critique. |
| `plans/` | Authority documents. |

Do **not** delete or edit the bench sketches — they are the hardware source of truth for the next step.

---

## 3. Source of truth — verified hardware facts

These values were confirmed on real hardware in the two test sketches. Treat them as the
starting point for the Step 1 hardware profile — **but see the conflict callout below.**

### 3.1 Pin map (from `stepper_ir_homing_test/stepper_ir_homing_test.ino`)

| Signal | GPIO | Notes |
| --- | --- | --- |
| X STEP (Motor A) | 5 | A4988 rising-edge step |
| X DIR | 6 | A4988 direction |
| X ENABLE | 7 | A4988 enable, **active LOW** |
| Y STEP (Motor B) | 9 | A4988 rising-edge step |
| Y DIR | 10 | A4988 direction |
| Y ENABLE | 11 | A4988 enable, active LOW |
| MS1 / MS2 / MS3 (shared) | 1 / 2 / 4 | Microstep select bits |
| Sensor X | 13 | `INPUT_PULLUP`, active LOW (FC-51 style). **Input only — never drive as output.** |
| Sensor Y | 8 | `INPUT_PULLUP`, active LOW |
| TCP port | — | 8888 |
| Serial baud | — | 115200 |

Unused in the combined fixture: GPIO 3.

### 3.2 Laser (from `laser_pwm_test/laser_pwm_test.ino`)

| Setting | Value |
| --- | --- |
| Laser PWM pin | GPIO 10 |
| LEDC channel | 0 |
| Resolution | 12-bit (duty 0–4095) |
| Default frequency | 5000 Hz (valid 100–80000 Hz) |
| Safety ceiling | `SAFETY_MAX_PCT 90` (duty clamp during commissioning) |
| Boot state | Hard pull LOW / laser off |

### 3.3 ⚠ GPIO 10 conflict — resolve before firmware work

- `laser_pwm_test` uses **GPIO 10 for laser PWM**.
- `stepper_ir_homing_test` uses **GPIO 10 for Y DIR**.

Both cannot be true. The legacy prototype's `config.h` used an entirely different ULN2003-style
coil mapping and is **not** authoritative. This is exactly the kind of unresolved item that
roadmap **Step 1** exists to close: produce one pin table where every GPIO has exactly one
production use, with boot-strapping/USB-sensitive pins checked on the actual ESP32-S3 module.
**Do not guess. If it cannot be confirmed from hardware, block downstream hardware work.**

### 3.4 Reusable engineering learnings (adapt, do not copy-paste)

From the verified sketches, carry these patterns into production firmware:

- **Non-blocking transport:** bounded line reader with an explicit `COMMAND_TOO_LONG` rejection; never parse a truncated prefix. Handle Serial and TCP with the same parser.
- **Debounced sensor sampling:** stable-detected state after N consistent samples over a time window (`HOME_STABLE_SAMPLES = 3`, `HOME_DEBOUNCE_MS = 3`), with raw vs. filtered state distinguished.
- **Explicit busy semantics:** long operations return `error: BUSY` rather than silently queueing.
- **Multi-phase homing state machine:** `Validate → ClearInitial → FastSeek → PullOff → SlowLatch`, with per-phase timeouts and distinct limit vs. timeout failures. Use this as the shape of real homing — not the prototype's "return true without moving".
- **Bounded motion:** coordinated dual-axis (Bresenham-style) stepping with min/max speed clamps and periodic abort checks that service serial/TCP.
- **Driver lifecycle:** A4988 enable is active LOW; assert a wake delay after enabling; auto-disable after idle.
- **Compatibility:** use the LEDC v2/v3 helper pattern (`ledcAttachChannel` vs `ledcSetup/ledcAttachPin`) so the firmware compiles on both Arduino core majors.

Also mine the sketch command surface (STATUS/HELP tables) for ideas, but **define the production protocol in Step 1** — do not adopt the bench command set wholesale.

---

## 4. Build order — follow the roadmap milestones

Implement strictly in this order. Step 1 gates all hardware-specific work.

| Step | Milestone | Gate / exit criteria (abridged) |
| --- | --- | --- |
| 1 | Hardware profile, safety boundaries, protocol contract | One pin table, no overlaps (fix GPIO 10), frozen versioned protocol with shared golden valid/invalid corpus; unknown commands rejected by both ends. |
| 2 | Fail-safe firmware foundation + command lifecycle | Safe outputs before Wi-Fi; bounded input; typed parser; explicit state machine; request IDs; snapshot on connect; serial works with no Wi-Fi. |
| 3 | Axis driver abstraction + bounded motion primitives | Validated, bounded moves; abort-safe. |
| 4 | Sensor diagnostics + real sensor-driven homing | Physical homing repeatable within tolerance; bounded failure. |
| 5 | Laser PWM behind a fail-safe output policy | Hardware safety gate passes; no browser path can hold laser on. |
| 6 | Server schemas, SQLite migrations, presets, machine config | Versioned migrations; immutable presets. |
| 7 | Reliable controller bridge, API, live status channel | Single serialized dispatcher; no silent fallbacks; reconnection snapshots. |
| 8 | SVG/shape preparation as validated immutable job plans | Deterministic, calibrated, preview-equivalent, bounded, rejects out-of-range. |
| 9 | Accessible web application shell + machine dashboard | Keyboard/AT usable; truthful readiness/freshness. |
| 10 | Calibration, homing, settings, presets, manual tests | Full operator workflows. |
| 11 | Engrave workflow, job lifecycle, run history | Immutable job snapshots; history. |
| 12 | Production hardening, system acceptance, deployment, release | Meets roadmap §18 definition of done. |

Steps 6–9 may proceed in parallel with later firmware steps **only** against a fake controller / laser-disabled profile.

---

## 5. Firmware requirements (server/webapp-optimized, with serial failsafe)

- **Transports:** **TCP over Wi-Fi is the primary control path** for the server/webapp. **USB serial is retained as a failsafe and local-recovery channel** — same parser, same command set, always available even when Wi-Fi is down or unconfigured.
- Boot to a **safe output state before** starting Wi-Fi or long startup work.
- One **typed** parser for both transports; validate field presence and range before any side effect (`MISSING` ≠ `0`).
- **Request IDs / operation IDs** with separate acceptance-ack vs. completion/failure events; replaying an ID must not repeat a move.
- **Snapshot on connect:** firmware version, protocol version, hardware profile id, safe-output state, machine state, homed flags, sensor levels, laser state, fault.
- One cooperative, non-blocking event loop for motion, homing, transport input, and stop. Status and stop stay responsive while any operation runs.
- Deterministic fault/reset handling: Wi-Fi loss and serial disconnect have defined abort/safe/invalidate behavior.
- Keep free-form diagnostics out of the command response channel; machine protocol frames are typed.

---

## 6. Frontend requirements (Vite + React)

- **Stack:** Vite + React (TypeScript preferred). Keep a small shared **design-token layer** (CSS custom properties) so the monochrome identity is consistent and easy to enforce.
- **Look:** minimal, professional, non-flashy. **Monochrome palette** — greys plus at most one restrained accent. No gradients-as-decoration, no animation for its own sake, no emoji, no playful styling. Density and clarity over flourish.
- **Accessibility:** full keyboard operation, labelled controls, visible focus, sufficient contrast within the monochrome scale, and an explicit, readable explanation for every disabled or failed action.
- **Truthfulness:** show machine readiness and data freshness with timestamps; never present a cached/stale state as live. Distinguish persisted config from temporary overrides and from the frozen job snapshot.
- **Pages:** Engrave, Calibration/Homing, Settings/Presets, Tests — per roadmap §12.
- The browser **never** opens a TCP socket to the ESP32 and never sends raw serial data; it talks only to the FastAPI server over HTTP + one WebSocket event stream.

---

## 7. Definition of done

The system is production-ready only when **all** of roadmap §18 holds: one reviewed hardware
profile with no overlapping pins; verified electrical safe-states; real, repeatable, bounded
homing; physically tested motion bounds/stop latency; laser gated with every terminal state
laser-off; a versioned protocol with shared host/firmware contract tests; no server silent-success
or unbounded waits; deterministic, calibrated SVG preparation; versioned immutable jobs with
verified migrations/backup; an accessible UI that reports freshness and readiness; passing staged
hardware tests with a known rollback; and docs that state exactly what was and was not tested.

If any critical requirement is unmet, **stay in laser-disabled dry-run / simulator mode** — that is a legitimate release, not a failure.

---

## 8. Never do

- Copy a bench sketch as production firmware, or adopt its pin map without Step 1 sign-off.
- Guess a pin, a max duty, or a step-per-mm value. Block instead.
- Commit credentials or private network details (the existing sketch's Wi-Fi creds must not be reused).
- Enable production laser output before the Step 5 hardware safety gate.
- Use unbounded reads/waits or success-shaped fallbacks.
- Recreate a removed prototype component "for compatibility".
- Report completion on mere command acceptance rather than actual operation outcome.

---

## 9. Per-milestone checklists

**Before coding**
- [ ] Read current branch/worktree; preserve pre-existing changes.
- [ ] Read the targeted source and tests.
- [ ] Confirm the relevant hardware/profile decision is closed.
- [ ] Identify user-visible behavior and safety state transitions.
- [ ] Agree the protocol/data contract before editing both ends.
- [ ] Write a test plan: success, failure, bounds, abort, reconnect.

**Before merging**
- [ ] Run the smallest relevant test set, then cross-layer contract tests.
- [ ] Compile firmware with the pinned ESP32 core.
- [ ] Tests are hermetic and clean up temp resources.
- [ ] No credential/secret committed.
- [ ] Inspect the full diff; stage only intended files.
- [ ] Every new API/command has an error path and a safe-state gate.
- [ ] Record which hardware tests were and were not performed.
