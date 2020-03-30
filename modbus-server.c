#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <modbus.h>
#include <pthread.h>
#include <errno.h>

#include "sock.h"

typedef struct {
  pthread_t th;
  int       id;
  char      path[64];
  char      name[64];
  char      host[64];
  char      type[64];
  char      topic[64];
  long      queue;
  uint16_t  port;
} connect_t;

typedef struct {
  uint16_t  transaction_id;
  uint16_t  protocol_id;
  uint16_t  length;
  uint8_t   unit_identifier;
  uint8_t   payload[0];
} modbus_header_t;

typedef struct {
  uint8_t   code;     // Function code
  uint16_t  address;  // Reference number
  uint16_t  count;    // Word count
} modbus_read_registers_t;

typedef struct {
  uint8_t   code;     // Function code
  uint8_t   count;    // Word count
  uint8_t   payload[0];
} modbus_resp_registers_t;

extern int errno;

const uint16_t UT_BITS_ADDRESS            = 0x0;
const uint16_t UT_BITS_NB                 = 0x10;
const uint8_t  UT_BITS_TAB[]              = { 0, };

const uint16_t UT_INPUT_BITS_ADDRESS      = 0x0;
const uint16_t UT_INPUT_BITS_NB           = 0x10;
const uint8_t  UT_INPUT_BITS_TAB[]        = { 0, };

const uint16_t UT_REGISTERS_ADDRESS       = 0x0;
const uint16_t UT_REGISTERS_NB            = 0x10;
const uint16_t UT_REGISTERS_TAB[]         = { 1, };

const uint16_t UT_INPUT_REGISTERS_ADDRESS = 0x0;
const uint16_t UT_INPUT_REGISTERS_NB      = 0x10;
const uint16_t UT_INPUT_REGISTERS_TAB[]   = { 0, };

void modbus_server (uint16_t port) {
  fd_set fds, rfds;
  struct timeval tm;
  modbus_t *mb;
  modbus_mapping_t *mb_mapping;
  int fd, lfd, maxfd;
  int i, rc;

  if ((lfd = mb_listen (port, &mb)) < 0) {
     fprintf(stderr, "Connection and listen failed: %s\n", modbus_strerror(errno));
     return;
  }
  int header_length = modbus_get_header_length(mb);
  maxfd = lfd;
  printf("Listen allocated socket fd is %d \n", lfd);

  char* messages = malloc(MODBUS_TCP_MAX_ADU_LENGTH);
  modbus_set_debug(mb, FALSE);

  mb_mapping = modbus_mapping_new_start_address(
    UT_BITS_ADDRESS, 
    UT_BITS_NB, 
    UT_INPUT_BITS_ADDRESS, 
    UT_INPUT_BITS_NB,
    UT_REGISTERS_ADDRESS, 
    UT_REGISTERS_NB, 
    UT_INPUT_REGISTERS_ADDRESS, 
    UT_INPUT_REGISTERS_NB
  );
  if (mb_mapping == NULL) {
    fprintf(stderr, "Failed to allocate the mapping: %s\n", modbus_strerror(errno));
    modbus_free(mb);
    return -1;
  }

  modbus_set_bits_from_bytes(mb_mapping->tab_bits, 0, UT_BITS_NB, UT_BITS_TAB);
  modbus_set_bits_from_bytes(mb_mapping->tab_input_bits, 0, UT_INPUT_BITS_NB, UT_INPUT_BITS_TAB);
  for (i=0; i < UT_REGISTERS_NB; i++) mb_mapping->tab_registers[i] = UT_REGISTERS_TAB[i];;
  for (i=0; i < UT_INPUT_REGISTERS_NB; i++) mb_mapping->tab_input_registers[i] = UT_INPUT_REGISTERS_TAB[i];;

  FD_ZERO(&rfds);
  FD_SET(lfd, &rfds);

  while (1) {
    fds = rfds;
    settimeout(&tm,1,0);
    if((rc = select(maxfd + 1, &fds, NULL, NULL, &tm)) < 0) return rc;
    if(FD_ISSET(lfd,&fds)) {
      int fd = mb_accept (lfd, mb);
      if(fd < 0) {
          fprintf(stderr, "Connection and listen failed: %s\n", modbus_strerror(errno));
          continue;
      }

      printf("Accept allocated socket fd is %d \n", fd);
      maxfd = (maxfd > fd)? maxfd : fd;
      FD_SET(fd,&rfds);
      continue;
    }
    for(int sockfd = lfd + 1; sockfd <= maxfd; sockfd++) {
      if(FD_ISSET(sockfd, &fds)) {
        if((rc=modbus_receive(mb, messages)) < 0) {
          FD_CLR(sockfd, &rfds);
          close(sockfd);
          printf("Client disconnected. socket fd is %d \n", sockfd);
          continue;
        }
        modbus_header_t *mh = (modbus_header_t*)messages;
        switch(*(uint8_t*)mh->payload) {
        case MODBUS_FC_READ_COILS                :
        case MODBUS_FC_READ_DISCRETE_INPUTS      :
        case MODBUS_FC_READ_HOLDING_REGISTERS    :
        case MODBUS_FC_READ_INPUT_REGISTERS      :
        case MODBUS_FC_WRITE_SINGLE_COIL         :
        case MODBUS_FC_WRITE_SINGLE_REGISTER     :
        case MODBUS_FC_READ_EXCEPTION_STATUS     :
        case MODBUS_FC_WRITE_MULTIPLE_COILS      :
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS  :
        case MODBUS_FC_REPORT_SLAVE_ID           :
        case MODBUS_FC_MASK_WRITE_REGISTER       :
        case MODBUS_FC_WRITE_AND_READ_REGISTERS  :
        default:
             break;
        }
        modbus_reply (mb, messages, rc, mb_mapping);
      }
    }
  }
}

int
main (int argc, char *argv[])
{
  modbus_server (502);
}
