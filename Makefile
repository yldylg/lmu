# build lua+unqlite+mongoose

CC=gcc
CFLAGS=-c -Ilua
LDFLAGS=-Llua -llua -lm -s
OBJS=lua-mongoose.o lua-unqlite.o lum.o mongoose/mongoose.o unqlite/unqlite.o
TARGET=lum

.PHONY: all clean

$(TARGET):$(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)
