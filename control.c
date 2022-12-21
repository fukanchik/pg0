#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>

#include "pg0.h"

static int null_opener(struct _ce *e, int flags);
static int log_file_reader(control_entry *e, char *buf, size_t size, off_t offset);
static int memory_writer(control_entry *e, const char *buf, size_t size, off_t offset);
static off_t log_file_sizer(control_entry *e);

typedef off_t (*get_size)(control_entry *e);

#define NSIZERS 1
static get_size SIZERS[] = {log_file_sizer};
#define LOG_SIZE_INDEX -1

off_t
get_entry_size(control_entry *e)
{
  elog("get_entry_size(rel_path=%s, is_datafile=%d, size=%d)",
       e->rel_path, e->is_datafile, e->size);

  off_t ret;
  if(e->is_datafile && e->size) {
    ret = e->n_headers * BLKSZ;
  } else if(e->size >= 0) {
    ret = e->size;
  } else if (e->size < 0) {
    int index = -(e->size+1);
    elog("...INDEX=%d, size=%d", index, e->size);
    if(index < 0 || index > NSIZERS) {
      elog("get_entry_size(): index=%d", index);
      return -ENOENT;
    }
    ret = SIZERS[index](e);
  } else {
    elog("XXX: EIO");
    ret = -EIO;
  }
  return ret;
}

static const char *BACKUP;

static void
elog_entry(const char *msg, control_entry *e)
{
  elog("%s: [%s, %s, %s, %ld, 0%o]", msg, e->absolute_path, e->rel_path, e->name,
       e->size, e->mode);
}

char *
get_real_path(control_entry *e)
{
  /* TODO fail if not a data entry. */
  char buf[8192];
  snprintf(buf, 8192, "%s/database/%s", BACKUP, e->rel_path);

  return strdup(buf);
}

static control_entry root = {.absolute_path = NULL,
                             .rel_path = "/",
                             .name = "",
                             .size=4192,
                             .mode=S_IFDIR|0700,
                             .is_datafile = 0,
                             .children = NULL,
                             .n_children=0,
                             .handle=-1,
                             .write_buf = NULL,
                             .wb_size=0,
                             .open=NULL,
                             .read=NULL,
                             .write=NULL,
                             .my_data=NULL,
};

static control_entry log_file = {.absolute_path = NULL,
                                 .rel_path = INTERNAL_LOG_PATH,
                                 .name = INTERNAL_LOG_PATH,
                                 .size = -1,
                                 .mode = S_IFREG|0400,
                                 .is_datafile = 0,
                                 .children = NULL,
                                 .n_children = 0,
                                 .handle = -1,
                                 .write_buf = NULL,
                                 .wb_size = 0,
                                 .open = null_opener,
                                 .read = log_file_reader,
                                 .write = NULL,
                                 .my_data = NULL,
};

static int
null_opener(struct _ce *e, int flags)
{
  elog("Null Opener flags=%x rel_path=%s name=%s", flags, e->rel_path, e->name);
  return 0;
}

/* Opens real files. */
static int
opener(struct _ce *e, int flags)
{
  elog("Opener flas=0x%x %p %s name=%s", flags, e, e->rel_path, e->name);
  if(e->size && (e->mode & S_IFMT) == S_IFREG) {
    int h = open(e->absolute_path, O_RDONLY);//fi->flags);
    if(h==-1) {
      elog("Can't open file %s", e->absolute_path);
      return -errno;
    }
    e->handle = h;
    elog("...=>%d", h);
    if((flags&O_RDWR) == O_RDWR || (flags&O_WRONLY) == O_WRONLY) {
      elog("...setting memory writer");
      e->write = memory_writer;
    }
    return 0;
  }
  elog("...EACCESS");
  return -EACCES;
}

static int
opener2(struct _ce *e, int flags)
{
  elog("opener2 %s %x", e->rel_path, flags);
  return 0;
}

static off_t
log_file_sizer(control_entry *e)
{
  elog("log_file_sizer(rel_path=%s)", e->rel_path);

  const char *log = elog_get_path();
  struct stat st;

  if(stat(log, &st) < 0) return -errno;

  return st.st_size;
}

static int
log_file_reader(control_entry *e, char *buf, size_t size, off_t offset)
{
  elog("log_file_reader(rel_path=%s, size=%ld, offset=%ld)", e->rel_path, size, offset);
  const char *log = elog_get_path();
  int fd = open(log, O_RDONLY);
  if(fd<0) return -errno;
  int rc = lseek(fd, offset, SEEK_SET);
  if (rc < 0) { close(fd); return -errno; }
  rc = read(fd, buf, size);
  close(fd);
  return rc;
}

static int
reader(control_entry *e, char *buf, size_t size, off_t offset)
{
  elog("reader(%s, %ld, %ld)", e->rel_path, size, offset);

  off_t rc = lseek(e->handle, offset, SEEK_SET);
  if(rc != offset) abort();
  int ret=read(e->handle, buf, size);
  elog("...%d", ret);
  return ret;
}

static int
reader2(control_entry *e, char *buf, size_t size, off_t offset)
{
  elog("reader2(rel_path=%s, size=%ld, offset=%ld, :::: wb_size=%ld)", e->rel_path, size, offset, e->wb_size);
  size_t sz = MIN(e->wb_size-offset, size);
  if(sz<0) sz=0;
  elog("...sz=%ld", sz);
  memcpy(buf, e->write_buf + offset, sz);

  return sz;
}

static int
memory_writer(control_entry *e, const char *buf, size_t size, off_t offset)
{
  elog("memory_writer(rel_path=%s, size=%ld, offset=%ld)", e->rel_path, size, offset);

  if (e->read != reader2) {
    if(e->is_datafile && e->size) {
      char *data = read_data_file(e);
      if(!data) efail("No data for entry %s", e->name);
      size_t real_size = e->n_headers * BLKSZ;
      e->write_buf = realloc(e->write_buf, real_size);
      e->wb_size = real_size;
      memcpy(e->write_buf, data, real_size);
      e->size = real_size;

      e->is_datafile = 0;
    } else {
      e->write_buf = realloc(e->write_buf, e->size);
      off_t rc = lseek(e->handle, 0, SEEK_SET);
      if(rc != 0) abort();
      int ret=read(e->handle, e->write_buf, e->size);
      if(ret != e->size) efail("...memory writer: %d != %d", ret, e->size);
      e->wb_size = e->size;
    }
    e->read = reader2;
  }

  e->wb_size = MAX(e->wb_size, offset + size);
  e->write_buf = realloc(e->write_buf, e->wb_size);
  elog("...wb_size=%ld", e->wb_size);
  memcpy(e->write_buf + offset, buf, size);

  e->size = e->wb_size;

  return size;
}

static void
dump(control_entry *e, int level)
{
  if(!e) return;
  for(int i=0;i<level;++i) printf("  ");
  printf("%s %ld %o %d [%d]\n", e->name, e->size, e->mode, e->is_datafile, e->n_children);
  for(int i=0;i<e->n_children;++i) {
    dump(e->children[i], level+1);
  }
}

static control_entry*
entry_for_path_r(const char *path, control_entry *parent)
{
  char buf[8192];
  char *p;
  char *rest;
  if((p=strchr(path, '/'))) {
    strncpy(buf, path, p-path);
    buf[p-path]=0;
    rest = p + 1;
  } else {
    strcpy(buf, path);
    rest = NULL;
  }
  for(int i=0;i<parent->n_children;++i) {
    control_entry *child = parent->children[i];
    if(!strcmp(child->name, buf)) {
      if(rest)
        return entry_for_path_r(rest, child);
      else
        return child;
    }
  }
  return NULL;
}

control_entry*
entry_for_path(const char *path)
{
  if(!strcmp(path, "/")) return &root;
  path = path + 1; // path is always absolute
  return entry_for_path_r(path, &root);
}

char *
get_file_name(const char *full_path)
{
  char *slash = strrchr(full_path, '/');
  if(!slash) return strdup(full_path);
  return strdup(slash+1);
}

static control_entry*
build_entry(const char *path, size_t size, mode_t mode, bool is_datafile, bool is_virt,
            int n_headers, int hdr_off, int hdr_size)
{
  if(path[0]=='/') {
    elog("build_entry(%s, %ld, 0%o", path, size, mode);
    abort();
  }
  elog("build_entry(path=%s, size=%ld, is_datafile=%d n_headers=%d", path, size, is_datafile, n_headers);
  control_entry *ret = malloc(sizeof(control_entry));
  if(!ret)abort();

  memset(ret, 0, sizeof(control_entry));

  ret->rel_path = strdup(path);
  ret->name = get_file_name(path);
  ret->size = size;
  ret->mode = mode;
  ret->is_datafile = is_datafile;
  ret->children = NULL;
  ret->n_children = 0;

  ret->n_headers = n_headers;
  ret->hdr_off = hdr_off;
  ret->hdr_size = hdr_size;

  if(is_virt)
    ret->absolute_path = NULL;
  else
    ret->absolute_path = get_real_path(ret);

  ret->write_buf = malloc(0);
  ret->wb_size = 0;
  ret->handle = -1;
  if(ret->size && (ret->mode & S_IFMT) == S_IFREG) {
    ret->open = opener;
    ret->read = reader;
  } else if(ret->size == 0 && (ret->mode & S_IFMT) == S_IFREG) {
    ret->open = opener2;
    ret->read = reader2;
    ret->write = memory_writer;
  } else {
    ret->open = NULL;
    ret->read = NULL;
  }
  ret->my_data= NULL;

  return ret;
}

void
grow_children(control_entry *e, control_entry *child)
{
  if(!child) {
    elog("NO CHILD");
    abort();
  }
  size_t next = e->n_children+1;
  e->children = realloc(e->children, sizeof(control_entry*)*next);
  e->children[e->n_children] = child;
  e->n_children = next;
}

int
remove_child(control_entry *e, control_entry *child)
{
  int pos = -1;
  for(int i=0;i<e->n_children;++i) {
    if(e->children[i] == child) {
      pos = i;
      break;
    }
  }
  if(pos==-1) return -1;
  size_t count = e->n_children-pos-1; 
  elog("pos=%d n_ch=%d count=%ld", pos, e->n_children, count);
  memmove(e->children+pos, e->children+pos+1, sizeof(control_entry*)*count);
  return 0;
}

int
CE_link(control_entry *e, const char *to)
{
  control_entry *to_e = entry_for_path(to);
  if(to_e) return -EACCES;
  control_entry *to_parent = parent_entry(to);
  control_entry *copy = build_entry(to+1, e->size, e->mode, e->is_datafile, true, e->n_headers, e->hdr_off, e->hdr_size);
  copy->open = opener2;
  copy->read = reader2;
  copy->write = memory_writer;
  
  grow_children(to_parent, copy);
  elog_entry("CE_link source", e);
  elog_entry("CE_link copy",copy);
  return 0;
}

int
CE_unlink(control_entry *e)
{
  char buf[8192];
  snprintf(buf, 8192, "/%s", e->rel_path);
  control_entry *parent = parent_entry(buf);
  if(!parent) {
    elog("No parent for %s", buf);
    abort();
  }
  if(remove_child(parent, e) == -1) return -ENOENT;
  return 0;
}

int
CE_rename(control_entry *e, const char *to)
{
  control_entry *to_e = entry_for_path(to);
  if(to_e) {
    elog_entry("CE_rename to", to_e);
    elog("CE_rename=>already exists %s", to);
    //return -EACCES;
    CE_unlink(to_e);
  }
  char buf[8192];
  snprintf(buf, 8192, "/%s", e->rel_path);
  control_entry *parent = parent_entry(buf);
  if(!parent) {
    elog("No parent for %s", buf);
    abort();
  }
  if(remove_child(parent, e) == -1) return -ENOENT;
  control_entry *to_parent = parent_entry(to);
  grow_children(to_parent, e);
  e->rel_path = strdup(to+1);
  e->name= get_file_name(to);
  return 0;
}

int
CE_add_child_dir(control_entry *e, const char *path, mode_t mode)
{
  char *fname = get_file_name(path);
  for(int i=0;i<e->n_children;++i) {
    if(!strcmp(e->name, fname)) return -EEXIST;
  }
  control_entry *child = build_entry(path+1, 0, mode|S_IFDIR, 0, true, -1, -1, -1);

  elog("...%s: add child %s (%s)", e->rel_path, path, fname);
  child->open = NULL;
  child->read = NULL;
  child->write = NULL;
  child->size = 8192;

  grow_children(e, child);

  return 0;
}

control_entry*
CE_add_child(control_entry *e, const char *path, mode_t mode)
{
  char *fname = get_file_name(path);

  /* Prevent adding the same entry twice. */
  for(int i = 0; i < e->n_children; ++i) {
    if(!strcmp(e->name, fname)) return NULL;
  }

  control_entry *child = build_entry(path+1, 0, mode, 0, true, -1, -1, -1);
  elog_entry("CE_add_child", child);

  child->open = opener2;
  child->read = reader2;
  child->write = memory_writer;

  grow_children(e, child);

  return child;
}

control_entry *parent_entry(const char *path)
{
  if(!strcmp(path, "/")) return NULL;
  char *slash = strrchr(path, '/');
  if(!slash) return NULL;
  char buf[8192];
  size_t len = MIN(8191, slash - path)+1;
  memcpy(buf, path, len);
  buf[len] = 0;
  if(len>1 && buf[strlen(buf)-1]=='/') buf[strlen(buf)-1]=0;
  return entry_for_path(buf);
}

char JSON_PATH[] = "{\"path\":\"";
char JSON_SIZE[] = ", \"size\":\"";
char JSON_MODE[] = ", \"mode\":\"";
char JSON_IS_DATAFILE[] = ", \"is_datafile\":\"";
char N_HEADERS[] = ",\"n_headers\":\"";
char HDR_OFF[] = ",\"hdr_off\":\"";
char HDR_SIZE[] = ",\"hdr_size\":\"";

static control_entry*
parse_line(char *line)
{
  char path[8192];
  char buf[8192];
  char *end;
  char *p = line;
  size_t size;
  mode_t mode;
  int is_datafile;

  if(strncmp(p, JSON_PATH, strlen(JSON_PATH)))
    abort();
  p += strlen(JSON_PATH);
  end = strchr(p, '"');
  if(!end) abort();
  memcpy(path, p, end-p);
  path[end-p]=0;

  p = strstr(p, JSON_SIZE);
  if(!p) abort();
  p += strlen(JSON_SIZE);
  end = strchr(p, '"');
  if(!end) abort();
  memcpy(buf, p, end-p);
  buf[end-p]=0;
  size = atol(buf);

  p = strstr(p, JSON_MODE);
  if(!p) abort();
  p += strlen(JSON_MODE);
  end = strchr(p, '"');
  if(!end) abort();
  memcpy(buf, p, end-p);
  buf[end-p]=0;
  mode = atol(buf);

  p = strstr(p, JSON_IS_DATAFILE);
  if(!p) abort();
  p += strlen(JSON_IS_DATAFILE);
  end = strchr(p, '"');
  if(!end) abort();
  memcpy(buf, p, end-p);
  buf[end-p] = 0;
  is_datafile = atoi(buf);

  int n_headers = -1;
  int hdr_off=-1;
  int hdr_size=-1;
  if(is_datafile && size) {
    p = strstr(p, N_HEADERS);
    if(!p) {
      elog("SHIT AT %s", line);
      abort();
    }
    p += strlen(N_HEADERS);
    end = strchr(p, '"');
    if(!end) abort();
    memcpy(buf, p, end-p);
    buf[end-p] = 0;
    n_headers = atoi(buf);

    p = strstr(p, HDR_OFF);
    if(!p) abort();
    p += strlen(HDR_OFF);
    end = strchr(p, '"');
    if(!end) abort();
    memcpy(buf, p, end-p);
    buf[end-p] = 0;
    hdr_off = atoi(buf);

    p = strstr(p, HDR_SIZE);
    if(!p) abort();
    p += strlen(HDR_SIZE);
    end = strchr(p, '"');
    if(!end) abort();
    memcpy(buf, p, end-p);
    buf[end-p] = 0;
    hdr_size = atoi(buf);
  }
  
  return build_entry(path, size, mode, is_datafile, size==0, n_headers, hdr_off, hdr_size);
}

#define MAX_ENTRIES 1000000
control_entry* entries[MAX_ENTRIES];
int n_entries;

static control_entry*
build_tree(const char *prefix, control_entry *parent)
{
  if(!prefix)abort();
  if(!parent)abort();
  int plen = strlen(prefix);

  control_entry *children[1000];
  int n_children = 0;

  for(int i = 0; i < n_entries; ++i) {
    control_entry *entry  = entries[i];
    if(entry == NULL) continue;
    if(!strncmp(prefix, entry->rel_path, plen) && !strchr(entry->rel_path+plen, '/')) {
      children[n_children++] = entry;
      if(n_children>=1000)abort();
      entries[i] = NULL;
    }
  }

  if(parent->children) abort();//we were here already
  parent->children = malloc(sizeof(control_entry*)*n_children);
  if(!parent->children) abort();
  parent->n_children = n_children;
  for(int i=0;i<n_children;++i) {
    control_entry *child = children[i];
    child->name = child->rel_path + plen;
    parent->children[i] = child;
    char buf[8192];
    if((child->mode & S_IFMT) == S_IFDIR) {
      snprintf(buf, 8192, "%s/", child->rel_path);
      build_tree(buf, child);
    }
  }
  return parent;
}

static control_entry*
read_contents(const char *backup)
{
  char buf[8192];

  snprintf(buf, 8192, "%s/backup_content.control", backup);
  FILE *fi = fopen(buf, "r");
  if(!fi) efail("Can't open %s", buf);

  n_entries = 0;
  while (fgets(buf, 8192, fi)) {
    control_entry *entry  = parse_line(buf);
    entries[n_entries++] = entry;
    if(n_entries>=MAX_ENTRIES) abort();
  }
  fclose(fi);

  control_entry *ret = build_tree("", &root);

  // check for missed `entries'
  for(int i=0;i<MAX_ENTRIES;i++){
    if(entries[i]) {
      efail("This entry is not part of the tree: %s\n", entries[i]->rel_path);
    }
  }
  return ret;
}

void
open_backup(const char *backup)
{
  BACKUP = strdup(backup);
  read_contents(backup);
  control_entry *log = CE_add_child(&root, INTERNAL_LOG_PATH, 0400);
  log->size = LOG_SIZE_INDEX;
  log->mode = S_IFREG | log->mode;
  log->read = log_file_reader;
  elog("ERR=%s, size=%d", log->rel_path, log->size);
}
