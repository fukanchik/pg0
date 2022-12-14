#!/usr/bin/env bash

#set -x
set -e

. ./defs.sh

psql -d postgres -p 12345 -c "select 42 as answer" > /dev/null 2>&1 || fail "Start one of the basics first"

if [[ ! $(psql -d postgres -p 5432 -c "select * from pg_catalog.pg_extension where extname='postgres_fdw'" | grep postgres_fdw) ]]; then
  title "Initialize extension"
  runsql 5432 'CREATE EXTENSION postgres_fdw'
  runsql 5432 "CREATE SERVER backup1 FOREIGN DATA WRAPPER postgres_fdw OPTIONS (host 'localhost', dbname 'postgres', port '12345')"
  runsql 5432 "CREATE USER MAPPING FOR fuxx SERVER backup1 OPTIONS (user 'fuxx');"
  runsql 5432 "CREATE FOREIGN TABLE backup_t1 (num NUMERIC) SERVER backup1 OPTIONS (table_name 't1')"
fi

title "count(*) entire foreign table"
runsql 5432 "SELECT count(*), sum(num) FROM backup_t1";
title "Compare local and foreign table"
runsql 5432 "select count(*), sum(num) from backup_t1 where num > 500"
runsql 5432 "select count(*), sum(num) from t1 where num > 500"
title "Join on itself and foreign"
runsql 5432 'select count(*), sum(t1.num) from t1, t1 as t2 where t1.num=t2.num'
runsql 5432 "select count(*), sum(t1.num) from t1, backup_t1 where t1.num=backup_t1.num"

title "Explains"
runsql 5432 "explain select count(*), sum(num) from t1 where num > 500"
runsql 12345 "explain select count(*), sum(num) from t1 where num > 500"

