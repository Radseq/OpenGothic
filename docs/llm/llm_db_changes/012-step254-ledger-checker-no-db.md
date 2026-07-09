# 012 - Step254 Ledger Checker: No DB Change

applied_to_db: no
sql_created: no
server_sql_touched: no

Step254 adds read-only ledger validation. It is not applied to DB and does not
connect to MySQL, generate SQL or inspect live schema.

Future storage, still not present in MySQL: dispatch receipts, client ACK/NACK
receipts, timeout/dead-letter retry state, terminal procedures and
validation/check tools.
