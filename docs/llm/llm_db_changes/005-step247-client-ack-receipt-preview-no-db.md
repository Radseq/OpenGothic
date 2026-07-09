# 005 - Step247 ACK Receipt Preview: No DB Change

applied_to_db: no
sql_created: no
server_sql_touched: no

Step247 validates synthetic ACK, NACK and malformed receipt examples only. It is
not applied to DB and does not receive live client packets.

Future storage, still not present in MySQL: client ACK/NACK receipts, dispatch
receipts, timeout/dead-letter state and validation/check tools.
