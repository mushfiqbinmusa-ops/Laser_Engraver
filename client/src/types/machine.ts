export interface SensorReadout {
  x_raw: number;
  x_filtered: number;
  y_raw: number;
  y_filtered: number;
}

export interface LaserReadout {
  duty: number;
  active: boolean;
  hard_clamped: boolean;
}

export interface SystemSnapshot {
  v: number;
  type: string;
  state: string;
  firmware_version: string;
  protocol_version: number;
  profile_id: string;
  homed: {
    x: boolean;
    y: boolean;
  };
  pos: {
    x: number;
    y: number;
  };
  pos_valid: boolean;
  sensors: SensorReadout;
  laser: LaserReadout;
  drivers_enabled: boolean;
  microstep_mode?: number;
  fault: string | null;
}

export interface AckResponse {
  v: number;
  id: string;
  type: string;
  accepted: boolean;
  code: string;
  operation_id?: string;
}

export interface MachineStatusResponse {
  connected: boolean;
  fresh: boolean;
  snapshot: SystemSnapshot | null;
  last_seen_epoch: number;
}
