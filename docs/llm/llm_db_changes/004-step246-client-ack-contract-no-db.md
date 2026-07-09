# 004 - Step246 Client ACK Contract: No DB Change

applied_to_db: no
sql_created: no
server_sql_touched: no

Step246 builds a client ACK/NACK correlation contract only. It is not applied to
DB and does not send, receive, schedule timers or mark actions applied.

Future storage, still not present in MySQL: client ACK/NACK receipts, dispatch
receipts, timeout/dead-letter state and terminal apply procedures.
