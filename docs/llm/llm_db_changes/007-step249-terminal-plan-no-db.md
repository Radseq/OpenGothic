# 007 - Step249 Terminal Plan: No DB Change

applied_to_db: no
sql_created: no
server_sql_touched: no

Step249 plans ACK apply, NACK reject/retry and timeout/dead-letter outcomes
only. It is not applied to DB and does not schedule timers or write terminal
state.

Future storage, still not present in MySQL: dispatch receipts, client ACK/NACK
receipts, timeout/dead-letter retry state and terminal apply procedures.
