#define BLKSZ 8192

typedef signed int int32;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned long int uint64;
typedef uint64 XLogRecPtr;
typedef uint32 BlockNumber;

typedef struct {
  BlockNumber block;
  int32 compressed_size;
} BackupPageHeader;

typedef struct BackupPageHeader2
{
	XLogRecPtr  lsn;
	int32	    block;			 /* block number */
	int32       pos;             /* position in backup file */
	uint16      checksum;
} BackupPageHeader2;

void elog_init(const char *path);
void elog(char *fmt, ...);
void efail(char *fmt, ...);

static inline long MIN(long a,long b)
{
  if(a<b) return a;
  return b;
}

static inline long MAX(long a,long b)
{
  if(a>b) return a;
  return b;
}

typedef int bool;
static const int true = 1;
static const int false = 0;

typedef struct _ce {
  char *absolute_path;
  char *rel_path;
  char *name;
  size_t size;
  mode_t mode;
  bool is_datafile;

  int n_headers;
  int hdr_off;
  int hdr_size;


  struct _ce **children;
  int n_children;

  int handle;

  char *write_buf;
  size_t wb_size;

  int (*open)(struct _ce *e, int flags);
  int (*read)(struct _ce *e, char *buf, size_t size, off_t offset);
  int (*write)(struct _ce *e, const char *buf, size_t size, off_t offset);
  char *my_data;
} control_entry;

/* Read backup data */
void open_backup(const char *backup);

control_entry *entry_for_path(const char *path);
control_entry *parent_entry(const char *path);

/* Convert entry into read file name in the backup dir. */
char *get_real_path(control_entry *entry);

int CE_add_child(control_entry *e, const char *path, mode_t mode);
int CE_add_child_dir(control_entry *e, const char *path, mode_t mode);
int CE_rename(control_entry *e, const char *to);
int CE_unlink(control_entry *e);
int CE_link(control_entry *e, const char *to);

/* /a/b/c => c*/
char *get_file_name(const char *full_path);

BackupPageHeader2* get_entry_header(control_entry *e, const char *backup);
char *read_data_file(control_entry *e);
