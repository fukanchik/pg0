#!/usr/bin/env bash

#set -x
set -e

. ./defs.sh

clean_up

fresh

title "Server is up. Initialize"

init_table t1 2000000

init_backups

title "Do backup"
out=$(doselect FIRST 5432 "SELECT 'FIRST' as marker, COUNT(*), sum(num) FROM t1 where num > 500;")

rm -rf /tmp/backup.log
pg_probackup backup --backup-path /home/fuxx/src/pg0/bb --instance dba1 --backup-mode full -d postgres --stream 2>&1 | tee /tmp/backup.log

BACKUP_ID=$(grep 'INFO: Backup .* completed' /tmp/backup.log | sed 's/INFO: Backup \(.*\) completed/\1/')

rm -rf /tmp/aaa
N=50
title "pgbench PG0 first. Port=12345"
./pg0 -s xxx $BACKUP_ID
pg_ctl -D /home/fuxx/src/pg0/xxx -o "-F -p 12345" -l logfile-backup1 start


pgbench -i -s $N -p 12345 -d postgres

title "pgbench real server. port=5432"
pgbench -i -s $N -p 5432 -d postgres

title "Stop"
pg_ctl -D /home/fuxx/src/pg0/xxx -o "-F -p 12345" -l logfile-backup1 stop
pg_ctl -D /home/fuxx/DBA1 stop > /dev/null 2>&1 || /bin/true

umount xxx

