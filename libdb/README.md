# libdb

SQLCipher statement and transaction ownership, independent of SovKit models.
The public header is `include/libdb/libdb.h`; errors use native SQLite result codes.
`Statement` owns a prepared statement; `Transaction` rolls back an uncommitted transaction.
Callers own the connection, schema, thread confinement, encryption keys and deadline policy.
Link `sovrankit::libdb`. The existing SovKit repositories and schema now live in
`projects/sdk/src/storage_repository.*`; their persistent representation is unchanged.
