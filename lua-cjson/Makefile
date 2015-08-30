##### build cjson #####
CC = gcc
CFLAGS = -I../lua -DNDEBUG -DDISABLE_INVALID_NUMBERS
OBJS = lua-cjson.o strbuf.o fpconv.o g_fmt.o dtoa.o
TARGET = liblua-cjson.a

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	ar -cr $(TARGET) $(OBJS)
	ranlib $(TARGET)

clean:
	rm -f $(TARGET) *.o *~
