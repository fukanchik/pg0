#!/usr/bin/env bash

set -x
set -e
set -u

. ./defs.sh


clean_up

fresh

title "Server is up. Press enter to initialize"
init_table t1 1000000
init_table t2 1000000

init_backups

title "Backup #1: initial series"
firstout=$(doselect FIRST 5432 "SELECT 'FIRST' as marker, COUNT(*), sum(num) FROM t1 where num > 500;")

rm -rf /tmp/backup.log
pg_probackup backup --backup-path /home/fuxx/src/pg0/bb --instance dba1 --backup-mode full -d postgres --stream 2>&1 | tee /tmp/backup.log

BACKUP_ID1=$(grep 'INFO: Backup .* completed' /tmp/backup.log | sed 's/INFO: Backup \(.*\) completed/\1/')
echo "BACKUP_ID1=$BACKUP_ID1 select=$firstout"

title "Backup #2: delete >400 and < 600"
runsql 5432 "DELETE FROM t1 WHERE num > 400 AND num < 600"
secondout=$(doselect SECOND 5432 "SELECT 'SECOND' as marker, COUNT(*), sum(num) FROM t1 where num > 500;")
rm -rf /tmp/backup.log
pg_probackup backup --backup-path /home/fuxx/src/pg0/bb --instance dba1 --backup-mode full -d postgres --stream 2>&1 | tee /tmp/backup.log

BACKUP_ID2=$(grep 'INFO: Backup .* completed' /tmp/backup.log | sed 's/INFO: Backup \(.*\) completed/\1/')
echo "BACKUP_ID1=$BACKUP_ID1 select=$firstout"
echo "BACKUP_ID2=$BACKUP_ID2 select=$secondout"

title "Backup #3: insert another million"
runsql 5432 "INSERT INTO t1 select random()*1000 from  generate_series(1000001, 2000000)"
thirdout=$(doselect THIRD 5432 "SELECT 'THIRD' as marker, COUNT(*), sum(num) FROM t1 where num > 500;")
rm -rf /tmp/backup.log
pg_probackup backup --backup-path /home/fuxx/src/pg0/bb --instance dba1 --backup-mode full -d postgres --stream 2>&1 | tee /tmp/backup.log

BACKUP_ID3=$(grep 'INFO: Backup .* completed' /tmp/backup.log | sed 's/INFO: Backup \(.*\) completed/\1/')

title "We now have three backups. Let's play."

E=

while [ -z "$E" ]; do
    echo "BACKUP_ID1=$BACKUP_ID1 select=$firstout"
    echo "BACKUP_ID2=$BACKUP_ID2 select=$secondout"
    echo "BACKUP_ID3=$BACKUP_ID3 select=$thirdout"

    title "Running pg0"
    rm -rf logfile-backup
    ./pg0 -s xxx || E=stop

    if [ -z "$E" ]; then
    pg_ctl -D /home/fuxx/src/pg0/xxx -o "-F -p 12345" -l logfile-backup start
    cat logfile-backup

    echo "BACKUP_ID1=$BACKUP_ID1 select=$firstout"
    echo "BACKUP_ID2=$BACKUP_ID2 select=$secondout"
    echo "BACKUP_ID3=$BACKUP_ID3 select=$thirdout"
    title "Press enter to stop server, umount backup"
    read x
    fi
    pg_ctl -D /home/fuxx/src/pg0/xxx -o "-F -p 12345" stop > /dev/null 2>&1 || /bin/true
    [ -f xxx/PG_VERSION ] && umount xxx || /bin/true
done

