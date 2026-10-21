# Laser XY Scanner Protocol v1 Specification

**Protocol Version:** `1`  
**Framing:** Line-delimited UTF-8 JSON (`\n` terminated)  
**Maximum Frame Size:** 512 bytes  
**Authority:** Shared across ESP32-S3 Firmware and Python FastAPI Service  

---

## 1. Framing and Transport Rules

1. **Strict Line Delimitation:** Every frame sent across USB Serial (115200 baud) or Wi-Fi TCP (Port 8888) is a single JSON object terminated by `\n` (`0x0A`).
2. **Buffer Limits:** The line buffer is strictly bounded to 512 bytes (`MAX_FRAME_SIZE`). Any line exceeding the buffer before `\n` triggers an immediate flush and emits an acknowledgment with `COMMAND_TOO_LONG`.
3. **Correlation:** Every request from the host includes an `id` string (e.g. `"req-001"`). The controller must reply with an acknowledgment echoing the same `id`.
4. **Operation Lifecycle:** `home.start` returns an immediate acceptance acknowledgment containing an `operation_id`, followed by asynchronous `operation.started` / `operation.completed` events. Job streaming is synchronous: `job.begin`, every `job.segment`, and `job.end` are acknowledged individually. `job.end` returns `BUSY` while the final segment is still moving and must be retried until accepted.
5. **Reconnection Snapshot:** Upon connecting via TCP or Serial, the host requests `system.snapshot` to obtain full machine state, coordinates, homing validity, and sensor status before issuing motion commands.

---

## 2. Command Surface Reference

| Command (`type`) | Parameters | Description |
|---|---|---|
| `system.ping` | none | Simple heartbeat query. Returns `ack` with `code: "OK"`. |
| `system.snapshot` | none | Requests full machine telemetry snapshot. |
| `system.stop` | none | Bounded deceleration stop. Turns off laser immediately. |
| `system.estop` | none | Immediate hardware halt. Disables steppers and laser immediately. |
| `system.reset_fault` | none | Clears recoverable faults. Requires re-homing before motion. |
| `driver.set_enabled` | `enabled: bool` | Powers stepper driver FETs on (`true`) or off (`false`). |
| `home.start` | `axis: "all" \| "x" \| "y"` | Initiates 2-stage optical homing state machine. |
| `motion.jog` | `axis: "x" \| "y"`, `steps: int`, `speed_sps: int` | Relative bounded step jog. |
| `motion.move_abs` | `x: int`, `y: int`, `speed_sps: int` | Absolute coordinate move (requires `homed = true`). |
| `laser.pulse` | `duty: int`, `duration_ms: int` | Guarded test pulse (max 500 ms, ≤ 3685 duty). |
| `laser.set_duty` | `duty: int` | Sets continuous laser duty (subject to 500 ms watchdog). |
| `job.begin` | `job_id: str`, `total_segments: int` | Initializes toolpath execution stream. |
| `job.segment` | `job_id: str`, `seg_idx: int`, `dx: int`, `dy: int`, `speed_sps: int`, `duty: int` | Relative coordinated toolpath segment. |
| `job.end` | `job_id: str` | Finalizes job execution; turns off laser. |

---

## 3. Standard Error Codes

- `OK`: Request accepted / executed.
- `PARSE_ERROR`: Malformed JSON or non-UTF8 input.
- `COMMAND_TOO_LONG`: Input line exceeded 512 bytes.
- `UNSUPPORTED_VERSION`: Frame protocol version (`v`) does not match 1.
- `UNKNOWN_COMMAND`: Command `type` is not recognized.
- `INVALID_ARGUMENT`: Missing required fields, out of range values, or wrong types.
- `OUT_OF_BOUNDS`: Commanded position or jog exceeds configured travel limits.
- `NOT_HOMED`: Absolute motion or job execution attempted before homing.
- `BUSY`: Controller is actively executing another non-preemptible operation.
- `LASER_DUTY_CLAMPED`: Requested duty exceeds the 90% safety clamp (3685).
- `SENSOR_STUCK_ACTIVE`: Endstop sensor did not clear during initial retreat.
- `HOME_SEEK_LIMIT`: Traveled maximum seek distance without triggering endstop.
- `HOME_TIMEOUT`: Homing phase exceeded the 60-second per-phase timeout.
- `WATCHDOG_TIMEOUT`: Laser watchdog expired without motion refresh.
