#include "mmo_server_world_clock.h"

namespace Mmo::Server {

std::string buildWorldClockSnapshotQuery(std::string_view sessionSql,
                                         std::string_view fallbackWorldSql) {
  std::string query;
  query += "SELECT COALESCE((SELECT JSON_OBJECT(";
  query += "'world_instance_uuid',BIN_TO_UUID(rwi.world_instance_id,1),'world_instance_key',rwi.world_instance_key,";
  query += "'world_name',COALESCE(cwt.world_name,";
  query += fallbackWorldSql;
  query += "),'current_tick',rwi.current_tick,";
  query += "'current_world_time_ms',rwi.current_world_time_ms,'updated_at',DATE_FORMAT(rwi.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) ";
  query += "FROM server_sessions ss JOIN realm_world_instances rwi ON rwi.world_instance_id=ss.world_instance_id ";
  query += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(";
  query += sessionSql;
  query += ",1) LIMIT 1), JSON_OBJECT());";
  return query;
}

} // namespace Mmo::Server
