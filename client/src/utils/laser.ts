export const LASER_MAX_DUTY = 4095;
export const LASER_SAFETY_PCT = 90;
export const LASER_SAFETY_DUTY = 3685;

export function powerPctToDuty(powerPct: number): number {
  const pct = Math.min(LASER_SAFETY_PCT, Math.max(0, powerPct));
  return Math.round((pct / LASER_SAFETY_PCT) * LASER_SAFETY_DUTY);
}
