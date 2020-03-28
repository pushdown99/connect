CC	= gcc
INC 	= -I/usr/local/include/modbus -I./lib
LIBS 	=
CFLAGS 	= -g -w $(INC)

SRCS = main.c

OBJS = $(SRCS:.c=.o)
LIBS = -L./lib -lpopup -lpthread -L/usr/local/lib -lmodbus -Llibhttp/lib -lhttp

TARGET 	= connect
TESTS = modbus-server udp-client

all: $(TARGET) $(TESTS)

$(TARGET): $(OBJS)
	$(CC) -o $@ $< $(LIBS)

dep:
	gccmakedep $(INC) $(SRCS)

clean:
	rm -rf $(OBJS) $(TARGET) $(TESTS) core

modbus-server: modbus-server.c
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

udp-client: udp-client.c
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)
