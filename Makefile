# build lua+unqlite+mongoose

CC=gcc
CFLAGS=-c -Ilua
LDFLAGS=-Llua -llua -lm -s
OBJS=lua-mongoose.o lua-unqlite.o lum.o mongoose/mongoose.o unqlite/unqlite.o
LUALIB=lua/liblua.a
TARGET=lum

.PHONY: all clean

$(TARGET):$(OBJS) $(LUALIB)
	$(CC) -o $(TARGET) $(OBJS) $(LDFLAGS)

$(LUALIB):
	cd lua; make posix; cd ..
	
clean:
	cd lua; make clean; cd ..; rm -f $(TARGET) $(OBJS)
