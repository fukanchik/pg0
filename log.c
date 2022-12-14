#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static FILE *LOG=NULL;

void
elog_init(const char *path)
{
  fprintf(stderr, "Opening log at %s\n", path);
  LOG = fopen(path, "a");
  if(!LOG) {
    fprintf(stderr, "Can't open logfile %s for append", path);
    abort();
  }
}

void
elog(char *fmt, ...)
{
  char buf[8192];
  va_list va;
  
  va_start(va, fmt);
  vsnprintf(buf, 8192, fmt, va);
  va_end(va);
  if(LOG==NULL) LOG=stderr;
  fprintf(LOG, "%s\n", buf);
  fflush(LOG);
}

void
efail(char *fmt, ...)
{
  va_list va;
  
  va_start(va, fmt);
  elog(fmt, va);
  va_end(va);

  abort();
}
