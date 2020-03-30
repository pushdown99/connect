#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>
#include <modbus.h>
#include <pthread.h>
#include <errno.h>

#include "sock.h"

#define PERIOD     1

void modbus_client (char *host, uint16_t port) {
  uint16_t tab_registers[20];
  modbus_t *mb;
  int fd, rc;

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, host, port, htonl(getaddrbyhost(host)));

  while (1) {
    if ((fd = mb_connect(getaddrbyhost(host), port, &mb)) < 0) {
      fprintf(stderr, "(%s:%d) Connection failed: %s\n", host, port, modbus_strerror(errno));
      modbus_close(mb);
      modbus_free(mb);
      sleep(PERIOD);
      continue;
    }
    if ((rc = modbus_read_registers(mb, 0, 5, tab_registers)) < 0) {
      fprintf(stderr, "%s\n", modbus_strerror(errno));
    }
    else {
        for (int i=0; i < rc; i++) {
            printf("reg[%d]=%d (0x%X)\n", i, tab_registers[i], tab_registers[i]);
        }
    }
    modbus_close(mb);
    modbus_free(mb);
    sleep(PERIOD);
  }
}

int
main (int argc, char *argv[])
{
  modbus_client ("localhost", 502);
}

