export PATH=/home/fuxx/PG/bin:$PATH

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

fail()
{
  echo -ne "::${RED}$*${NC}\n" 1>&2
  exit 1
}

msg()
{
  echo -ne "::$*\n" 1>&2
  #read x
}

title()
{
  msg "${GREEN}$*${NC}"
}

runsql()
{
  port="$1"
  sql1="$2"
  msg port=$port "${RED}$sql1${NC}"
  psql -d postgres -p $port -c "$sql1"
}

doselect()
{
  what="$1"
  port="$2"
  sql1="$3"
  msg port=$port sql=$sql1
  psql -d postgres -p $port -c "$sql1" > /tmp/sql.txt 2>&1

  cat /tmp/sql.txt 1>&2

  grep $what /tmp/sql.txt
}

clean_up()
{
    tmux clear-history

    title Cleanup everything
(
    rm -rf logfile /tmp/lll.txt logfile-backup
    killall postgres || /bin/true
    sleep 1
    killall -9 postgres || /bin/true

    [ -f xxx/PG_VERSION ] && umount xxx || /bin/true
    [ -f yyy/PG_VERSION ] && umount yyy || /bin/true

    rm -rf xxx
    rm -rf yyy

    mkdir xxx
    mkdir yyy
) > /dev/null 2>&1

(
    cd code
    make
    cp ./pg0 ..
) > /dev/null 2>&1

(
    pg_ctl -D /home/fuxx/DBA1 stop || /bin/true
    rm -rf /home/fuxx/DBA1
    rm -rf /home/fuxx/src/pg0/bb
) > /dev/null 2>&1

}

fresh()
{
    title Initialize fresh server
    initdb --data-checksums --pgdata /home/fuxx/DBA1 | grep 'Success.'
    pg_ctl -D /home/fuxx/DBA1 -l logfile start
    sleep 1
    cat logfile
}

init_table()
{
  name=$1
  count=$2

  runsql 5432 "CREATE TABLE ${name} (num numeric);"
  runsql 5432 "INSERT INTO t1 select random()*1000 from  generate_series(1, ${count})" 
}


init_backups()
{
  title "Initialize backups dir"
  pg_probackup init --backup-path /home/fuxx/src/pg0/bb
  pg_probackup add-instance --backup-path /home/fuxx/src/pg0/bb --instance dba1 --pgdata /home/fuxx/DBA1
}

do_backup()
{
  name=$1
  msg "Do backup $name"
  out=$(doselect FIRST 5432 "SELECT 'FIRST' as marker, COUNT(*), sum(num) FROM t1 where num > 500;")

  rm -rf /tmp/backup.log
  pg_probackup backup --backup-path /home/fuxx/src/pg0/bb --instance dba1 --backup-mode full -d postgres --stream 2>&1 | tee /tmp/backup.log

  BACKUP_ID=$(grep 'INFO: Backup .* completed' /tmp/backup.log | sed 's/INFO: Backup \(.*\) completed/\1/')

  echo "RESULT::$BACKUP_ID::$out"
}

