#!/usr/bin/env bash

#set -x
set -e

. ./defs.sh

clean_up
fresh

title "Server is up. Initialize"

init_table t1 2000000
init_table t2 2000000

init_backups

title "Do backup"
firstout=$(doselect FIRST 5432 "SELECT 'FIRST' as marker, COUNT(*), sum(num) FROM t1 where num > 500;")

rm -rf /tmp/backup.log
pg_probackup backup --backup-path /home/fuxx/src/pg0/bb --instance dba1 --backup-mode full -d postgres --stream 2>&1 | tee /tmp/backup.log

BACKUP_ID1=$(grep 'INFO: Backup .* completed' /tmp/backup.log | sed 's/INFO: Backup \(.*\) completed/\1/')
echo "BACKUP_ID1=$BACKUP_ID1 select=$firstout"

pg_ctl -D /home/fuxx/DBA1 stop > /dev/null 2>&1 || /bin/true
rm -rf /tmp/aaa

title pg_probackup restore time
time (
    pg_probackup restore  --backup-path /home/fuxx/src/pg0/bb --instance dba1 --backup-id $BACKUP_ID1 --pgdata=/tmp/aaa
    pg_ctl -D /tmp/aaa -l logfile start
    doselect FIRST 5432 "SELECT 'FIRST' as marker, COUNT(*), sum(num) FROM t1 where num > 500;"
    pg_ctl -D /tmp/aaa stop > /dev/null 2>&1 || /bin/true
    rm -rf /tmp/aaa
)

title "PG0 time"
time (
    ./pg0 -s xxx $BACKUP_ID1
    pg_ctl -D /home/fuxx/src/pg0/xxx -o "-F -p 12345" -l logfile-backup start
    doselect FIRST 12345 "SELECT 'FIRST' as marker, COUNT(*), sum(num) FROM t1 where num > 500;"
    pg_ctl -D /home/fuxx/src/pg0/xxx -o "-F -p 12345" stop > /dev/null 2>&1 || /bin/true
    [ -f xxx/PG_VERSION ] && umount xxx || /bin/true
)

pg_ctl -D /home/fuxx/DBA1 -l logfile start
title "Original server cold time"
time (
    doselect FIRST 5432 "SELECT 'FIRST' as marker, COUNT(*), sum(num) FROM t1 where num > 500;"
)
title "Stop"
pg_ctl -D /home/fuxx/DBA1 stop

