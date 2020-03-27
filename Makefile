CC	= gcc
INC 	= -I/usr/local/include/modbus -I./lib
LIBS 	=
CFLAGS 	= -g -w $(INC)

SRCS = main.c

OBJS = $(SRCS:.c=.o)
LIBS = -L./lib -lpopup -lpthread -L/usr/local/lib -lmodbus

TARGET 	= connect

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $< $(LIBS)

dep:
	gccmakedep $(INC) $(SRCS)

clean:
	rm -rf $(OBJS) $(TARGET) core