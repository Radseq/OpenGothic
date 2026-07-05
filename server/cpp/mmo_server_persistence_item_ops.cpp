#include "mmo_server_persistence.h"
#include "mmo_server_persistence_sql.h"

namespace Mmo::Server {

void transferCharacterItem(const MySqlTarget& target, const TransferCharacterItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @target_character_id=NULL; SET @amount_transferred=NULL;";
  sql += "CALL mmo_transfer_character_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += "UUID_TO_BIN(" + sqlLiteral(record.itemUuid) + ",1)," + sqlLiteral(record.targetCharacterKey) + ",";
  sql += std::to_string(record.amount) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey);
  sql += ",@event_id,@target_character_id,@amount_transferred);";
  (void)runMysql(target, sql);
}

void lootWorldInventoryItem(const MySqlTarget& target, const LootWorldInventoryRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @source_amount_remaining=NULL; SET @amount_looted=NULL;";
  sql += "CALL mmo_loot_npc_inventory(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.sourceEntityKey) + ",UUID_TO_BIN(" + sqlLiteral(record.itemUuid) + ",1),";
  sql += std::to_string(record.amount) + "," + std::to_string(record.bagIndex) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@source_amount_remaining,@amount_looted);";
  (void)runMysql(target, sql);
}

void grantCharacterItemBySymbol(const MySqlTarget& target, const GrantCharacterItemBySymbolRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @item_id=NULL; SET @amount_granted=NULL;";
  sql += "CALL mmo_grant_character_item_by_symbol(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += std::to_string(record.itemSymbol) + "," + std::to_string(record.amount) + ",";
  sql += std::to_string(record.bagIndex) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey);
  sql += ",@event_id,@item_id,@amount_granted);";
  (void)runMysql(target, sql);
}

void pickupWorldItem(const MySqlTarget& target, const PickupWorldItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @item_id=NULL; SET @amount_picked=NULL;";
  sql += "CALL mmo_pickup_world_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.entityKey) + "," + std::to_string(record.amount) + ",";
  sql += std::to_string(record.bagIndex) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey);
  sql += ",@event_id,@item_id,@amount_picked);";
  (void)runMysql(target, sql);
}

void removeWorldItem(const MySqlTarget& target, const RemoveWorldItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @item_id=NULL;";
  sql += "CALL mmo_remove_world_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.entityKey) + "," + sqlLiteral(record.reason) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@item_id);";
  (void)runMysql(target, sql);
}

void equipCharacterItem(const MySqlTarget& target, const EquipCharacterItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL;";
  sql += "CALL mmo_equip_character_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += "UUID_TO_BIN(" + sqlLiteral(record.itemUuid) + ",1)," + sqlLiteral(record.equipmentSlot) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id);";
  (void)runMysql(target, sql);
}

void unequipCharacterItem(const MySqlTarget& target, const UnequipCharacterItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @item_id=NULL;";
  sql += "CALL mmo_unequip_character_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.equipmentSlot) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@item_id);";
  (void)runMysql(target, sql);
}

void dropCharacterItem(const MySqlTarget& target, const DropCharacterItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @amount_remaining=NULL; SET @amount_dropped=NULL;";
  sql += "CALL mmo_drop_character_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += "UUID_TO_BIN(" + sqlLiteral(record.itemUuid) + ",1),";
  sql += std::to_string(record.amount) + "," + sqlLiteral(record.entityKey) + ",";
  sql += PersistenceSql::nullableDouble(record.posX) + "," + PersistenceSql::nullableDouble(record.posY) + "," +
         PersistenceSql::nullableDouble(record.posZ) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@amount_remaining,@amount_dropped);";
  (void)runMysql(target, sql);
}

} // namespace Mmo::Server
