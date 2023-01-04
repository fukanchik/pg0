# PG0 - run a postgresql server directly from pg\_probackup without restore
* Early alpha
* PG0 emulates PGDATA directory using FUSE
* root is not needed for mount. You can separate db admin and unix admin roles
* PG _never_ writes to backup

## Benefits
* No additional space is required to peek into your data

## Current limitations
* Full backups only. Delta backups are not supported yet
* Compression is not supported
* Single threaded FUSE

