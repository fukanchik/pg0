#!/usr/bin/env bash

#set -x
set -e

. ./defs.sh

clean_up

fresh

title "Server is up. Initialize"

init_table t1 1000000

init_backups

title "Do backup 1"
firstout=$(doselect FIRST 5432 "SELECT 'FIRST' as marker, COUNT(*), sum(num) FROM t1 where num > 500;")

rm -rf /tmp/backup.log
pg_probackup backup --backup-path /home/fuxx/src/pg0/bb --instance dba1 --backup-mode full -d postgres --stream 2>&1 | tee /tmp/backup.log

BACKUP_ID1=$(grep 'INFO: Backup .* completed' /tmp/backup.log | sed 's/INFO: Backup \(.*\) completed/\1/')
echo "BACKUP_ID1=$BACKUP_ID1 select=$firstout"

runsql 5432 "DELETE from t1 where num > 400 and num < 600;"

title "Do backup 2"
secondout=$(doselect FIRST 5432 "SELECT 'FIRST' as marker, COUNT(*), sum(num) FROM t1 where num > 500;")

rm -rf /tmp/backup.log
pg_probackup backup --backup-path /home/fuxx/src/pg0/bb --instance dba1 --backup-mode full -d postgres --stream 2>&1 | tee /tmp/backup.log

BACKUP_ID2=$(grep 'INFO: Backup .* completed' /tmp/backup.log | sed 's/INFO: Backup \(.*\) completed/\1/')
echo "BACKUP_ID1=$BACKUP_ID1 select=$firstout"
echo "BACKUP_ID2=$BACKUP_ID2 select=$secondout"

pg_ctl -D /home/fuxx/DBA1 stop > /dev/null 2>&1 || /bin/true
rm -rf /tmp/aaa

title "PG0 first. Port=12345"
./pg0 -s xxx $BACKUP_ID1
pg_ctl -D /home/fuxx/src/pg0/xxx -o "-F -p 12345" -l logfile-backup1 start
doselect FIRST 12345 "SELECT 'FIRST' as marker, COUNT(*), sum(num) FROM t1 where num > 500;"

sleep 1

[ ! -d yyy ] && mkdir yyy

title "PG0 second. Port=54321"
./pg0 -s yyy $BACKUP_ID2
pg_ctl -D /home/fuxx/src/pg0/yyy -o "-F -p 54321" -l logfile-backup2 start
doselect FIRST 54321 "SELECT 'FIRST' as marker, COUNT(*), sum(num) FROM t1 where num > 500;"

title "Two servers are up. Press enter to shut down"
read x
title "Stop"
pg_ctl -D /home/fuxx/src/pg0/xxx -o "-F -p 12345" stop
pg_ctl -D /home/fuxx/src/pg0/yyy -o "-F -p 54321" stop

umount xxx
umount yyy

rm -r yyy

