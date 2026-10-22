export interface Preset {
  id: string;
  name: string;
  description?: string;
  travel_speed_sps: number;
  engrave_speed_sps: number;
  laser_power_pct: number;
  laser_duty: number;
  passes: number;
  microstep_mode: number;
  is_builtin: boolean;
}

export interface MachineConfig {
  id: string;
  steps_per_unit_x: number;
  steps_per_unit_y: number;
  work_area_width_mm: number;
  work_area_height_mm: number;
  max_travel_steps_x: number;
  max_travel_steps_y: number;
  default_speed_sps: number;
  max_speed_sps: number;
  default_microstep_mode: number;
  laser_ceiling_pct: number;
  laser_ceiling_duty: number;
}

export interface Job {
  id: string;
  name: string;
  source_filename?: string;
  source_hash: string;
  status: 'draft' | 'prepared' | 'running' | 'completed' | 'canceled' | 'failed' | 'unknown';
  total_segments: number;
  completed_segments: number;
  has_preview: boolean;
  created_at: string;
  updated_at: string;
}
