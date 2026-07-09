# 001 - Step243 Send Boundary: No DB Change

applied_to_db: no
sql_created: no
server_sql_touched: no

Step243 adds a C++ no-send boundary for a claimed dialog-intent action. It is
not applied to DB and does not require schema, procedure, view or index changes.

Future storage, still not present in MySQL: dispatch receipts, client ACK/NACK
receipts and admin views for the eventual live-send path.
