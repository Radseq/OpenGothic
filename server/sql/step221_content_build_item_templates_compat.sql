SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE DATABASE IF NOT EXISTS `mmo_content_build`
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_0900_ai_ci;

USE `mmo_content_build`;

CREATE TABLE IF NOT EXISTS `daedalus_item_templates` (
  `daedalus_item_template_id` binary(16) NOT NULL DEFAULT (uuid_to_bin(uuid(),1)),
  `content_build_import_id` binary(16) NOT NULL,
  `content_revision_key` varchar(191) NOT NULL,
  `item_instance` varchar(191) NOT NULL,
  `display_name` varchar(191) NOT NULL DEFAULT '',
  `item_category` varchar(96) NOT NULL DEFAULT '',
  `main_flag` int DEFAULT NULL,
  `flags_value` int DEFAULT NULL,
  `value_amount` int DEFAULT NULL,
  `damage_total` int DEFAULT NULL,
  `raw_payload` json NOT NULL DEFAULT (json_object()),
  `created_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  `updated_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (`daedalus_item_template_id`),
  UNIQUE KEY `daedalus_item_template_uk` (`content_revision_key`,`item_instance`),
  KEY `ix_daedalus_item_template_import` (`content_build_import_id`),
  KEY `ix_daedalus_item_template_category` (`content_revision_key`,`item_category`),
  KEY `ix_daedalus_item_template_flags` (`content_revision_key`,`main_flag`,`flags_value`),
  CONSTRAINT `daedalus_item_template_import_fk`
    FOREIGN KEY (`content_build_import_id`) REFERENCES `content_build_imports` (`content_build_import_id`) ON DELETE CASCADE,
  CONSTRAINT `daedalus_item_template_revision_fk`
    FOREIGN KEY (`content_revision_key`) REFERENCES `content_build_revisions` (`content_revision_key`) ON DELETE CASCADE,
  CONSTRAINT `daedalus_item_template_payload_json_ck` CHECK (json_valid(`raw_payload`))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS `v_daedalus_item_templates`;
CREATE VIEW `v_daedalus_item_templates` AS
SELECT
  bin_to_uuid(`item`.`daedalus_item_template_id`,1) AS `daedalus_item_template_uuid`,
  bin_to_uuid(`item`.`content_build_import_id`,1) AS `content_build_import_uuid`,
  `item`.`content_revision_key` AS `content_revision_key`,
  `item`.`item_instance` AS `item_instance`,
  `item`.`display_name` AS `display_name`,
  `item`.`item_category` AS `item_category`,
  `item`.`main_flag` AS `main_flag`,
  `item`.`flags_value` AS `flags_value`,
  `item`.`value_amount` AS `value_amount`,
  `item`.`damage_total` AS `damage_total`,
  `item`.`raw_payload` AS `raw_payload`,
  `item`.`created_at` AS `created_at`,
  `item`.`updated_at` AS `updated_at`
FROM `daedalus_item_templates` `item`;

DROP VIEW IF EXISTS `v_content_build_health`;
CREATE VIEW `v_content_build_health` AS
SELECT
  `r`.`game_code` AS `game_code`,
  `r`.`content_revision_key` AS `content_revision_key`,
  `r`.`build_status` AS `build_status`,
  `r`.`manifest_hash` AS `manifest_hash`,
  COUNT(DISTINCT `bi`.`content_build_import_id`) AS `build_import_count`,
  SUM(CASE WHEN `bi`.`import_status` = 'imported' THEN 1 ELSE 0 END) AS `imported_count`,
  SUM(CASE WHEN `bi`.`import_status` = 'failed' THEN 1 ELSE 0 END) AS `failed_count`,
  (SELECT COUNT(*) FROM `content_build_files` `f` WHERE `f`.`content_revision_key` = `r`.`content_revision_key`) AS `file_count`,
  (SELECT COUNT(*) FROM `content_build_files` `f` WHERE `f`.`content_revision_key` = `r`.`content_revision_key` AND `f`.`required_for_server_authority` = 1) AS `required_file_count`,
  (SELECT COUNT(*) FROM `content_build_parser_errors` `pe` WHERE `pe`.`content_revision_key` = `r`.`content_revision_key` AND `pe`.`severity` IN ('error','fatal')) AS `blocking_error_count`,
  (SELECT COUNT(*) FROM `world_zen_entities` `z` WHERE `z`.`content_revision_key` = `r`.`content_revision_key`) AS `zen_entity_count`,
  (SELECT COUNT(*) FROM `world_zen_entities` `z` WHERE `z`.`content_revision_key` = `r`.`content_revision_key` AND `z`.`entity_kind` = 'waypoint') AS `waypoint_count`,
  (SELECT COUNT(*) FROM `world_zen_entities` `z` WHERE `z`.`content_revision_key` = `r`.`content_revision_key` AND `z`.`entity_kind` = 'freepoint') AS `freepoint_count`,
  (SELECT COUNT(*) FROM `world_zen_entities` `z` WHERE `z`.`content_revision_key` = `r`.`content_revision_key` AND `z`.`entity_kind` = 'vob') AS `vob_count`,
  (SELECT COUNT(*) FROM `world_waypoint_edges` `edge` WHERE `edge`.`content_revision_key` = `r`.`content_revision_key`) AS `waypoint_edge_count`,
  (SELECT COUNT(*) FROM `daedalus_symbols` `s` WHERE `s`.`content_revision_key` = `r`.`content_revision_key`) AS `daedalus_symbol_count`,
  (SELECT COUNT(*) FROM `daedalus_npc_templates` `n` WHERE `n`.`content_revision_key` = `r`.`content_revision_key`) AS `npc_template_count`,
  (SELECT COUNT(*) FROM `daedalus_item_templates` `it` WHERE `it`.`content_revision_key` = `r`.`content_revision_key`) AS `item_template_count`,
  (SELECT COUNT(*) FROM `daedalus_routines` `rt` WHERE `rt`.`content_revision_key` = `r`.`content_revision_key`) AS `routine_count`,
  (SELECT COUNT(*) FROM `daedalus_perception_bindings` `p` WHERE `p`.`content_revision_key` = `r`.`content_revision_key`) AS `perception_binding_count`,
  (SELECT COUNT(*) FROM `dialog_outputs` `o` WHERE `o`.`content_revision_key` = `r`.`content_revision_key`) AS `dialog_output_count`,
  (SELECT COUNT(*) FROM `dialog_infos` `i` WHERE `i`.`content_revision_key` = `r`.`content_revision_key`) AS `dialog_info_count`,
  MAX(`bi`.`updated_at`) AS `last_build_update_at`
FROM `content_build_revisions` `r`
LEFT JOIN `content_build_imports` `bi`
  ON `bi`.`content_revision_key` = `r`.`content_revision_key`
GROUP BY
  `r`.`game_code`,
  `r`.`content_revision_key`,
  `r`.`build_status`,
  `r`.`manifest_hash`;

INSERT INTO `content_build_schema_versions` (`migration_key`,`checksum`,`description`)
VALUES (
  'server/sql/step221_content_build_item_templates_compat.sql',
  SHA2('server/sql/step221_content_build_item_templates_compat.sql',256),
  'Adds daedalus_item_templates compatibility table, view, and item_template_count health output.'
)
ON DUPLICATE KEY UPDATE
  `checksum` = VALUES(`checksum`),
  `description` = VALUES(`description`),
  `applied_at` = CURRENT_TIMESTAMP(6);
