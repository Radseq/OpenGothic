-- Step92: human-readable admin views for UUID/BINARY identity columns.
-- These views are diagnostic only. Gameplay authority still uses the canonical
-- binary UUID PK/FK columns and stable engine keys.

CREATE OR REPLACE VIEW v_mmo_admin_item_instances_readable AS
SELECT
  BIN_TO_UUID(ii.item_instance_id, 1) AS item_instance_uuid,
  ii.item_instance_key,
  BIN_TO_UUID(ii.realm_id, 1) AS realm_uuid,
  BIN_TO_UUID(ii.item_template_id, 1) AS item_template_uuid,
  cit.item_template_key,
  cit.symbol_index AS item_symbol_index,
  cit.script_name AS item_script_name,
  cit.display_name AS item_display_name,
  ii.owner_type,
  BIN_TO_UUID(ii.owner_id, 1) AS owner_uuid,
  ii.quantity,
  ii.bind_state,
  ii.lifecycle_state,
  ii.created_at,
  ii.updated_at
FROM item_instances ii
LEFT JOIN content_item_templates cit ON cit.item_template_id = ii.item_template_id;

CREATE OR REPLACE VIEW v_mmo_admin_entity_templates_readable AS
SELECT
  BIN_TO_UUID(cet.entity_template_id, 1) AS entity_template_uuid,
  BIN_TO_UUID(cet.content_revision_id, 1) AS content_revision_uuid,
  cet.entity_kind,
  cet.engine_template_key,
  cet.symbol_index,
  cet.script_id,
  cet.script_name,
  cet.display_name,
  cet.visual_key,
  cet.created_at
FROM content_entity_templates cet;

CREATE OR REPLACE VIEW v_mmo_admin_world_entities_readable AS
SELECT
  BIN_TO_UUID(wes.world_entity_state_id, 1) AS world_entity_state_uuid,
  BIN_TO_UUID(wes.world_instance_id, 1) AS world_instance_uuid,
  COALESCE(cwt.world_name, rwi.world_instance_key) AS world_name,
  wes.entity_key,
  wes.entity_kind,
  BIN_TO_UUID(wes.entity_template_id, 1) AS entity_template_uuid,
  cet.engine_template_key,
  COALESCE(cet.symbol_index, CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.symbol_index')) AS SIGNED), CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.script_id')) AS SIGNED), CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.item_template_symbol')) AS SIGNED)) AS symbol_index,
  COALESCE(cet.script_id, CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.script_id')) AS SIGNED), CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.instance_symbol')) AS SIGNED)) AS script_id,
  COALESCE(cet.script_name, JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.script_name')), JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.symbol_name'))) AS script_name,
  COALESCE(cet.display_name, JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.display_name')), JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.name'))) AS display_name,
  wes.lifecycle_state,
  wes.pos_x,
  wes.pos_y,
  wes.pos_z,
  wes.rotation_yaw,
  wes.health_current,
  wes.health_max,
  wes.row_version,
  wes.updated_at
FROM world_entity_state wes
JOIN realm_world_instances rwi ON rwi.world_instance_id = wes.world_instance_id
LEFT JOIN content_world_templates cwt ON cwt.world_template_id = rwi.world_template_id
LEFT JOIN content_entity_templates cet ON cet.entity_template_id = wes.entity_template_id;
