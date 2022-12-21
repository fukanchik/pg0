#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "pg0.h"

static const char usage[] = "usage: pg0\n"
  "\n"
  "options:\n"
  "  --backup-path=<path>	path to pg_probackup backup\n"
  "  --instance=<name>	name of postgres instance in pg_probackup backup\n"
  "  --backup-id=<name>	name of postgres instance in pg_probackup backup\n"
  "  --help		print help message\n"
  "  --debug		enable debug logging\n";

typedef struct
{
  char *backup_path;
  char *instance;
  char *backup_id;
  int is_help;
} pg0_args;

#define KEY_HELP 0
#define KEY_BACKUP_PATH 1
#define KEY_INSTANCE 2
#define KEY_BACKUP_ID 3

static const struct fuse_opt command_line_options[] =
  {
   {"--backup-path=%s", offsetof(pg0_args, backup_path), KEY_BACKUP_PATH},
   {"--instance=%s", offsetof(pg0_args, instance), KEY_INSTANCE},
   {"--backup-id=%s", offsetof(pg0_args, backup_id), KEY_BACKUP_ID},
   FUSE_OPT_KEY("--help", KEY_HELP),
   FUSE_OPT_END
  };

static int
process_arg(void *data, const char *arg, int key, struct fuse_args *outargs)
{
  pg0_args *args = data;

  switch(key)
		{
    case KEY_HELP:
      args->is_help = 1;
      fprintf(stderr, "%s", usage);
      return 0;
    default:
      break;
		}
  return 1;
}

typedef struct fs_node
{
  char *name;
  size_t size;
  struct fs_node *parent;
  struct fs_node *children;
  int n_children;
} fs_node;

static void *
pg_init(struct fuse_conn_info *conn)
{
  return NULL;
}

static int
pg_mkdir(const char *path, mode_t mode)
{
  elog("pg_mkdir(%s, 0%o)", path, mode);

  control_entry *e = entry_for_path(path);
  if(e) {
    elog("...EEXIST");
    return -EEXIST;
  }
  control_entry *parent = parent_entry(path);
  if(!parent) {
    elog("...EACCES %s", path);
    return -EACCES;
  }

  int ret=CE_add_child_dir(parent, path, mode);
  elog("...=>%d", ret);
  return ret;
}

static int
pg_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
           off_t offset, struct fuse_file_info *fi)
{
  elog("pg_readdir(%s)", path);
  control_entry *e = entry_for_path(path);
  if(!e) {
    elog("...ENOENT");
    return -ENOENT;
  }

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  for(int i=0;i<e->n_children;++i) {
    filler(buf, e->children[i]->name, NULL, 0);
    elog("...%d: <%s>", i+1, e->children[i]->name);
  }

  return 0;
}

static int
pg_getattr(const char *path, struct stat *st)
{
  elog("pg_getattr(%s)", path);
  control_entry *e = entry_for_path(path);
  if(!e) {
    elog("...ENOENT");
    return -ENOENT;
  }
  st->st_uid = getuid();
  st->st_gid = getgid();
  //memcpy(&st->st_mtim, &e->mtime, sizeof(struct timespec));
  st->st_mode = e->mode;
  if((e->mode & S_IFMT) == S_IFDIR)
    st->st_nlink=2+e->n_children;
  else
    st->st_nlink = 1;
  if(e->is_datafile && e->size) {
    st->st_size = e->n_headers * BLKSZ;
  } else {
    st->st_size = e->size;
  }

  elog("...mode=0%o, size=%ld", st->st_mode, st->st_size);

  return 0;
}

static int
pg_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
  elog("pg_create(path = %s, mode=%o, FLAGS=%X", path, mode, fi->flags);

  control_entry *e = entry_for_path(path);
  if(e) {
    elog("...EEXIST");
    return -EEXIST;
  }
  control_entry *parent = parent_entry(path);
  if(!parent) {
    elog("...EACCES");
    return -EACCES;
  }

  int ret=CE_add_child(parent, path, mode);
  elog("...=>%d", ret);
  return ret;
}

static int
pg_open(const char *path, struct fuse_file_info *fi)
{
  elog("pg_open(path=%s, flags=%X)", path, fi->flags);

  control_entry *e = entry_for_path(path);
  if(!e) {
    elog("...ENOENT");
    return -ENOENT;
  }
  if(!e->open) {
    elog("No open function for %s %p %s %s %s", path, e, e->absolute_path, e->rel_path, e->name);
    abort();
  }
  return e->open(e, fi->flags);
}

static void
elog_bytes(unsigned char *buf, size_t size)
{
  char log[8192];
  char *p=log;
  const char *sep="";
  for(int i=0;i<MIN(size, 16);++i) {
    snprintf(p, 200, "%s%02X", sep, buf[i]);
    sep=" ";
    p+=strlen(p);
  }
  *p++='\n';
  for(int i=0;i<MIN(size, 16);++i) {
    char ch = isprint(buf[i])?buf[i]:'.';
    snprintf(p, 20, "%s %c", sep, ch);
    sep=" ";
    p+=strlen(p);
  }
  elog("{%s}", log);
}

static int
pg_read(const char *path, char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
  control_entry *e = entry_for_path(path);
  elog("pg_read(path=%s, size=%ld, offset=%ld)", path, size, offset);
  if(!e) {
    elog("pg_read %s ENOENT", path);
    return -ENOENT;
  }
  if(e->n_headers > 0) {
    /* data file */
    char *data = read_data_file(e);//get_entry_header(e, BACKUP);
    if(!data) efail("No data for entry %s", e->name);
    size_t real_size = e->n_headers * BLKSZ;
    size_t ret = MIN(real_size-offset, size);
    memcpy(buf, data+offset, ret);
    elog("...%d (of %ld)", ret, real_size);
    if(ret>0) {
      elog_bytes(buf, ret);
    }
    //CACHED. Do not free `data'
    return ret;
  } else {
    /* non-data file */
    int ret=e->read(e, buf, size, offset);
    elog("...%d", ret);
    if(ret>0) {
      elog_bytes(buf, ret);
    }
    return ret;
  }
}

static int
pg_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
  elog("pg_write(%s, %ld, %ld)", path, size, offset);
  control_entry *e = entry_for_path(path);
  if(!e) return -ENOENT;
  elog("Found entry %s. Size=%ld", e->name, e->size);
  int ret= e->write(e, buf, size, offset);
  elog("...%d", ret);
  return ret;
}

static int
pg_unlink(const char *path)
{
  elog("pg_unlink(path=%s)", path);
  control_entry *e = entry_for_path(path);
  if(!e) return -ENOENT;
  return CE_unlink(e);
}

static int
pg_rename(const char *from, const char *to)
{
  elog("pg_rename(from=%s, to=%s)", from, to);
  control_entry *from_e = entry_for_path(from);
  if(!from_e) {
    elog("...ENOENT");
    return -ENOENT;
  }
  int ret=CE_rename(from_e, to);
  elog("...%d", ret);
  return ret;
}

static int
pg_link(const char *from, const char *to)
{
  elog("pg_link(from=%s, to=%s)", from, to);
  control_entry *from_e = entry_for_path(from);
  if(!from_e) return -ENOENT;
  return CE_link(from_e, to);
}

static int
pg_trunc(const char *path, off_t size)
{
  elog("pg_trunc(path=%s, size=%ld)", path, size);
  control_entry *e = entry_for_path(path);
  if(!e) return -ENOENT;
  e->size = size;
  if (e->write_buf) {
    e->write_buf = realloc(e->write_buf, size);
    e->wb_size = size;
  }
  if (e->my_data) {
    free(e->my_data);
    e->my_data = NULL;
  }
  return 0;
}

static int
pg_rmdir(const char *path)
{
  elog("pg_rmdir(path=%s)", path);
  control_entry *e = entry_for_path(path);
  if(!e) return -ENOENT;
  return CE_unlink(e);
}

static const struct fuse_operations
pg_oper = {
           .getattr = pg_getattr,
           .mkdir = pg_mkdir,
           .unlink = pg_unlink,
           .rmdir = pg_rmdir,
           .rename = pg_rename,
           .link = pg_link,
           .truncate = pg_trunc,
           .open = pg_open,
           .create = pg_create,
           .read = pg_read,
           .write = pg_write,
           .readdir = pg_readdir,
           .init = pg_init,
};
  
int
cstr(const void *a, const void *b)
{
  return strcmp(*(const char **)a, *(const char **)b);
}

char *
select_backup(pg0_args *args)
{
  if(!args->backup_path) {
    fprintf(stderr, "pg_probackup backups path is required\n"); 
    fprintf(stderr, "%s", usage);
    exit(2);
  }
  if(!args->instance) {
    fprintf(stderr, "database instance name is required\n"); 
    fprintf(stderr, "%s", usage);
    exit(3);
  }
  if(!args->backup_id) {
    fprintf(stderr, "backup id is required\n"); 
    fprintf(stderr, "%s", usage);
    exit(4);
  }

  char buf[PATH_MAX];
  snprintf(buf, PATH_MAX, "%s/backups/%s/%s", args->backup_path, args->instance,
           args->backup_id);
  char *backup = realpath(buf, NULL);
  if(!backup || access(backup, R_OK) < 0)
    efail("Can't access backup at %s", backup);

  
  fprintf(stdout, "Restoring from %s\n", backup);
  snprintf(buf, PATH_MAX, "/tmp/pg0-log-%s.txt", args->backup_id);
  elog_init(buf);

  return backup;
}

int
main(int argc, char *argv[])
{
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  pg0_args myargs = {};

  if(fuse_opt_parse(&args, &myargs, command_line_options, process_arg)) {
    return 1;
  }
  if(myargs.is_help){
    return 0;
  }


  char *backup = select_backup(&myargs);
  open_backup(backup);
  free(backup);

  int ret = fuse_main(args.argc, args.argv, &pg_oper, NULL);

  return ret;
}
