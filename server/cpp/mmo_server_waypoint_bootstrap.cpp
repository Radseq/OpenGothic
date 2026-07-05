#include "mmo_server_waypoint_bootstrap.h"

#include "mmo_server_snapshot_limits.h"

namespace Mmo::Server::Waypoint {

namespace {

[[nodiscard]] std::string activeHeroBootstrapSubquery(std::string_view sessionSql) {
  std::string out;
  out += "(SELECT ss.character_id,ss.realm_id,ss.world_instance_id,COALESCE(cp.pos_x,cca.pos_x,0) AS hx,";
  out += "COALESCE(cp.pos_y,cca.pos_y,0) AS hy,COALESCE(cp.pos_z,cca.pos_z,0) AS hz ";
  out += "FROM server_sessions ss ";
  out += "LEFT JOIN character_positions cp ON cp.character_id=ss.character_id ";
  out += "LEFT JOIN character_checkpoint_audit cca ON cca.checkpoint_id=(SELECT ca.checkpoint_id FROM character_checkpoint_audit ca WHERE ca.session_id=ss.session_id ORDER BY ca.created_at DESC LIMIT 1) ";
  out += "WHERE ss.session_id=UUID_TO_BIN(";
  out += sessionSql;
  out += ",1) LIMIT 1) h";
  return out;
}

[[nodiscard]] std::string sourceRowsToOrderedJsonArrayQuery(std::string_view sourceSql, std::size_t limit) {
  std::string query;
  query += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (SELECT row_json FROM (";
  query += sourceSql;
  query += ") source_rows ORDER BY dist_sq ASC,owner_key LIMIT " + std::to_string(limit);
  query += ") ordered_rows), JSON_ARRAY());";
  return query;
}

[[nodiscard]] std::string nearbyWaypointSource(std::string_view activeHeroSubquery,
                                               std::string_view worldSql,
                                               std::string_view radiusSql) {
  std::string out;
  out += "SELECT JSON_OBJECT('waypoint_key',wp.waypoint_key,'waypoint_name',wp.waypoint_name,'kind_key',wp.kind_key,'pos_x',wp.pos_x,'pos_y',wp.pos_y,'pos_z',wp.pos_z,";
  out += "'distance',SQRT(((wp.pos_x-h.hx)*(wp.pos_x-h.hx))+((wp.pos_y-h.hy)*(wp.pos_y-h.hy))+((wp.pos_z-h.hz)*(wp.pos_z-h.hz))),'updated_at',DATE_FORMAT(wp.materialized_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,";
  out += "((wp.pos_x-h.hx)*(wp.pos_x-h.hx))+((wp.pos_y-h.hy)*(wp.pos_y-h.hy))+((wp.pos_z-h.hz)*(wp.pos_z-h.hz)) AS dist_sq,wp.waypoint_key AS owner_key ";
  out += "FROM ";
  out += activeHeroSubquery;
  out += " JOIN realm_world_instances rwi ON rwi.world_instance_id=h.world_instance_id ";
  out += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id ";
  out += "JOIN mmo_server_waypoint_read_model wp ON wp.world_name=COALESCE(cwt.world_name,rwi.world_instance_key,";
  out += worldSql;
  out += ") ";
  out += "WHERE wp.pos_x IS NOT NULL AND wp.pos_y IS NOT NULL AND wp.pos_z IS NOT NULL ";
  out += "AND (((wp.pos_x-h.hx)*(wp.pos_x-h.hx))+((wp.pos_y-h.hy)*(wp.pos_y-h.hy))+((wp.pos_z-h.hz)*(wp.pos_z-h.hz))) <= (";
  out += radiusSql;
  out += "*";
  out += radiusSql;
  out += ")";
  return out;
}

[[nodiscard]] std::string nearbyWaypointDiagnosticQuery(std::string_view activeHeroSubquery,
                                                        std::string_view worldSql,
                                                        std::string_view radiusSql) {
  std::string out;
  out += "SELECT CONCAT('waypoint_near=',(SELECT COUNT(*) FROM mmo_server_waypoint_read_model wp WHERE wp.world_name=COALESCE(cwt.world_name,rwi.world_instance_key,";
  out += worldSql;
  out += ") AND wp.pos_x IS NOT NULL AND wp.pos_y IS NOT NULL AND wp.pos_z IS NOT NULL AND (((wp.pos_x-h.hx)*(wp.pos_x-h.hx))+((wp.pos_y-h.hy)*(wp.pos_y-h.hy))+((wp.pos_z-h.hz)*(wp.pos_z-h.hz))) <= (";
  out += radiusSql;
  out += "*";
  out += radiusSql;
  out += "))) FROM ";
  out += activeHeroSubquery;
  out += " JOIN realm_world_instances rwi ON rwi.world_instance_id=h.world_instance_id ";
  out += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id;";
  return out;
}

} // namespace

NearbyWaypointBootstrap readNearbyWaypointBootstrap(const MySqlTarget& target,
                                                    std::string_view sessionUuid,
                                                    std::string_view worldName) {
  const auto sessionSql = sqlLiteral(sessionUuid);
  const auto worldSql = sqlLiteral(worldName);
  const auto radiusSql = std::to_string(BootstrapNearbyWaypointRadius);
  const auto activeHero = activeHeroBootstrapSubquery(sessionSql);
  const auto source = nearbyWaypointSource(activeHero, worldSql, radiusSql);

  NearbyWaypointBootstrap out;
  out.json = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToOrderedJsonArrayQuery(source, MaxBootstrapNearbyWaypointRows),
      "[]",
      "bootstrap_nearby_waypoints");
  out.diagnostic = mysqlSingleFieldWithDiagnostic(
      target,
      nearbyWaypointDiagnosticQuery(activeHero, worldSql, radiusSql),
      "bootstrap_nearby_waypoints_debug");
  return out;
}

} // namespace Mmo::Server::Waypoint
