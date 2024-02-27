#!/usr/bin/env bash
set -u
set -e
set -x

export PGPATH=/tmp/postgres
export DB=$PGPATH/test-db
export BACKUPS=$PGPATH/backups
export FUSE=$PGPATH/fuse-db

mkdir -p $PGPATH

initdb $DB --data-checksums
pg_ctl -D $DB -l $PGPATH/pg-logfile start
psql -d postgres -c 'create table test as select i as id from generate_series(0,100) i;'

pg_probackup init --backup-path $BACKUPS
pg_probackup add-instance --backup-path $BACKUPS --instance dba1 --pgdata $DB
pg_probackup backup --backup-path $BACKUPS --instance=dba1 --stream --backup-mode full -d postgres
export BACKUP_ID=$(ls $BACKUPS/backups/dba1 | grep -v pg_probackup.conf)

cd $PGPATH
mkdir -p $FUSE
/pg0/pg0 --backup-path=$BACKUPS --instance=dba1 --backup-id=$BACKUP_ID -s $FUSE
pg_ctl -D $FUSE -o "-F -p 12345" -l $PGPATH/fuse-logfile start

psql -d postgres -p 5432 -c "select sum(id) from test;"
psql -d postgres -p 12345 -c "select sum(id) from test;"

pg_ctl -D $FUSE -o "-F -p 12345" stop
pg_ctl -D $DB stop

