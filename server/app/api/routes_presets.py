import uuid
from typing import List
from fastapi import APIRouter, HTTPException
from app.db.database import get_db
from app.models.preset_models import PresetModel, PresetCreate, PresetUpdate, MachineConfigModel
from app.core.laser import pct_to_duty

router = APIRouter(prefix="/api/presets", tags=["presets"])

@router.get("", response_model=List[PresetModel])
def list_presets():
    with get_db() as conn:
        cursor = conn.execute("SELECT * FROM presets ORDER BY is_builtin DESC, name ASC;")
        rows = cursor.fetchall()
        return [PresetModel.model_validate(dict(r)) for r in rows]

@router.post("", response_model=PresetModel)
def create_preset(p: PresetCreate):
    preset_id = f"preset-{uuid.uuid4().hex[:8]}"
    # Single source of truth for the pct -> duty safety mapping
    duty = pct_to_duty(p.laser_power_pct)

    with get_db() as conn:
        try:
            conn.execute("""
                INSERT INTO presets (id, name, description, travel_speed_sps, engrave_speed_sps, laser_power_pct, laser_duty, passes, is_builtin)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0);
            """, (preset_id, p.name, p.description, p.travel_speed_sps, p.engrave_speed_sps, p.laser_power_pct, duty, p.passes))
        except Exception as e:
            raise HTTPException(status_code=400, detail=f"Failed to create preset: {e}")

        cursor = conn.execute("SELECT * FROM presets WHERE id = ?;", (preset_id,))
        row = cursor.fetchone()
        return PresetModel.model_validate(dict(row))

@router.delete("/{preset_id}")
def delete_preset(preset_id: str):
    with get_db() as conn:
        cursor = conn.execute("SELECT is_builtin FROM presets WHERE id = ?;", (preset_id,))
        row = cursor.fetchone()
        if not row:
            raise HTTPException(status_code=404, detail="Preset not found")
        if row["is_builtin"]:
            raise HTTPException(status_code=400, detail="Cannot delete built-in system presets")

        conn.execute("DELETE FROM presets WHERE id = ?;", (preset_id,))
        return {"ok": True, "deleted": preset_id}

# Machine configuration routes
@router.get("/config", response_model=MachineConfigModel)
def get_machine_config():
    with get_db() as conn:
        cursor = conn.execute("SELECT * FROM machine_config WHERE id = 'default';")
        row = cursor.fetchone()
        if not row:
            raise HTTPException(status_code=404, detail="Machine configuration not found")
        return MachineConfigModel.model_validate(dict(row))

@router.put("/config", response_model=MachineConfigModel)
def update_machine_config(cfg: MachineConfigModel):
    with get_db() as conn:
        conn.execute("""
            UPDATE machine_config SET
                steps_per_unit_x = ?,
                steps_per_unit_y = ?,
                work_area_width_mm = ?,
                work_area_height_mm = ?,
                max_travel_steps_x = ?,
                max_travel_steps_y = ?,
                default_speed_sps = ?,
                max_speed_sps = ?,
                updated_at = CURRENT_TIMESTAMP
            WHERE id = 'default';
        """, (
            cfg.steps_per_unit_x, cfg.steps_per_unit_y,
            cfg.work_area_width_mm, cfg.work_area_height_mm,
            cfg.max_travel_steps_x, cfg.max_travel_steps_y,
            cfg.default_speed_sps, cfg.max_speed_sps
        ))
        cursor = conn.execute("SELECT * FROM machine_config WHERE id = 'default';")
        row = cursor.fetchone()
        return MachineConfigModel.model_validate(dict(row))
