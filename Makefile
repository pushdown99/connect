CC	= gcc
INC 	= -I/usr/local/include/modbus -I./lib -I./include -I./libhttp/include
LIBS 	=
CFLAGS 	= -g -w $(INC)

SRCS = main.c

OBJS = $(SRCS:.c=.o)
LIBS = -L./lib -lpopup -lpthread -L/usr/local/lib -lmodbus -Llibhttp/lib -lhttp -lpaho-mqtt3a 

TARGET 	= connect
TESTS = modbus-server modbus-client udp-server udp-client tcp-server tcp-client tcp-test ipc-test

all: $(TARGET) $(TESTS)

$(TARGET): $(OBJS)
	$(CC) -o $@ $< $(LIBS)

dep:
	gccmakedep $(INC) $(SRCS)

clean:
	rm -rf $(OBJS) $(TARGET) $(TESTS) core

modbus-server: modbus-server.c
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

modbus-client: modbus-client.c
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

udp-client: udp-client.c
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

udp-server: udp-server.c
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

tcp-client: tcp-client.c
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

tcp-server: tcp-server.c
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

tcp-test: tcp-test.c
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

ipc-test: ipc-test.c
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)
