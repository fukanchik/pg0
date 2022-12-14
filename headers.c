#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>

#include <zlib.h>

#include "pg0.h"

BackupPageHeader2*
get_entry_header(control_entry *e, const char *backup)
{
  char path[8192];
  char zbuf[8192];

  snprintf(path, 8192, "%s/page_header_map", backup);

  if(e->n_headers <= 0) abort();//TODO
  int fd = open(path, O_RDONLY);
  if(fd==-1) {
    elog("Can't open %s", path);
    abort();
  }
  int rc = lseek(fd, e->hdr_off, SEEK_SET);
  if(rc==-1) abort();
  int read_len = (e->n_headers+1)*sizeof(BackupPageHeader2);
  rc=read(fd, zbuf, e->hdr_size);
  if(rc<0) abort();
  BackupPageHeader2 *headers = malloc(read_len);
  size_t len=read_len;
  if(!headers) abort();
  uncompress((void*)headers, &len, zbuf, e->hdr_size);
  for(int i=0;i<e->n_headers;++i) {
    elog("[%d] LSN=%lX BLOCK=%d POS=%d CHECKSUM=%08X", i, headers[i].lsn,
           headers[i].block, headers[i].pos, headers[i].checksum);
  }
  return headers;
}

char *
read_data_file(control_entry *e)
{
  if(!e->is_datafile) {
    efail("Not a datafile %s", e->name);
  }
  elog("read_data_file(%s, size=%ld)", e->absolute_path, e->size);
  if(e->my_data) return e->my_data;
  void *ret = malloc(e->size);
  if(!e->size) return ret;

  int fd = open(e->absolute_path, O_RDONLY);
  if(fd < 0) efail("Can't open %s", e->absolute_path);
  int rc = read(fd, ret, e->size);
  close(fd);
  if(rc != e->size) efail("Can't read %ld from %s: %d", e->size, e->absolute_path, rc);
  if(e->size%(sizeof(BackupPageHeader)+BLKSZ)) {
    size_t block_size = sizeof(BackupPageHeader)+BLKSZ;
    int remainder = e->size%(sizeof(BackupPageHeader)+BLKSZ);
    efail("Incomplete block: %d %d %d", e->size, block_size, remainder);
  }
  void *ret2 = malloc(BLKSZ*(e->n_headers+1));
  char *p = (char*)ret;
  char *r2 = (char*)ret2;
  for(int i = 0; i < e->n_headers; i++) {
    BackupPageHeader *h = (BackupPageHeader*)p;
    if(h->compressed_size != BLKSZ)
      efail("Block size %d in %s", h->block, h->compressed_size, e->absolute_path);
    memcpy(r2, p+sizeof(BackupPageHeader), BLKSZ);
    r2 += BLKSZ;
    p += sizeof(BackupPageHeader) + BLKSZ;
  }
  free(ret);
  e->my_data = ret2;
  return ret2;
}
