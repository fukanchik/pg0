#
# Build pg0
#
APP=pg0

CFLAGS=-ggdb -O0 -D_FILE_OFFSET_BITS=64
OBJS=fuse.o control.o log.o headers.o

all: $(APP)

$(APP): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -lfuse -lz -o $(APP)

clean:
	rm $(OBJS) $(APP)
