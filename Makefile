# build lua+mongoose+unqlite

CC=gcc
CFLAGS=-c -Ilua
LDFLAGS=-Llua -llua -lm -s
OBJS=lua-mongoose.o lua-unqlite.o lmu.o mongoose/mongoose.o unqlite/unqlite.o
LUALIB=lua/liblua.a
TARGET=lmu

.PHONY: all clean

$(TARGET):$(OBJS) $(LUALIB)
	$(CC) -o $(TARGET) $(OBJS) $(LDFLAGS)

$(LUALIB):
	cd lua; make posix; cd ..
	
clean:
	cd lua; make clean; cd ..; rm -f $(TARGET) $(OBJS)
