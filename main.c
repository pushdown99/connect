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

#define MAX_THREAD 100
#define PERIOD     1

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

int max_thread = 0;
connect_t *CH[MAX_THREAD] = { NULL, };

extern int errno;

///////////////////////////////////////////////////////////////////////////////

const uint16_t UT_BITS_ADDRESS            = 0x0;
const uint16_t UT_BITS_NB                 = 0x10;
const uint8_t  UT_BITS_TAB[]              = { 0, };

const uint16_t UT_INPUT_BITS_ADDRESS      = 0x0;
const uint16_t UT_INPUT_BITS_NB           = 0x10;
const uint8_t  UT_INPUT_BITS_TAB[]        = { 0, };

const uint16_t UT_REGISTERS_ADDRESS       = 0x0;
const uint16_t UT_REGISTERS_NB            = 0x10;
const uint16_t UT_REGISTERS_TAB[]         = { 0, };

const uint16_t UT_INPUT_REGISTERS_ADDRESS = 0x0;
const uint16_t UT_INPUT_REGISTERS_NB      = 0x10;
const uint16_t UT_INPUT_REGISTERS_TAB[]   = { 0, };

void modbus_client_task(connect_t *cp) {
  uint16_t tab_registers[UT_REGISTERS_NB];
  modbus_t *mb;
  int fd, rc;

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, cp->host, cp->port, getaddrbyhost(cp->host));

  while (1) {
    if ((fd = mb_connect(getaddrbyhost(cp->host), cp->port, &mb)) < 0) {
      fprintf(stderr, "(%s:%d) Connection failed: %s\n", cp->host, cp->port, modbus_strerror(errno));
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

void modbus_server_task(connect_t *cp) {
  fd_set fds, rfds;
  struct timeval tm;
  modbus_t *mb;
  modbus_mapping_t *mb_mapping;
  int fd, lfd, maxfd;
  int i, rc;

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, cp->host, cp->port, getaddrbyhost(cp->host));

  if ((lfd = mb_listen (cp->port, &mb)) < 0) {
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
    if((rc = select(maxfd + 1, &fds, NULL, NULL, &tm)) < 0) {
      continue;
    }
    if(FD_ISSET(lfd,&fds)) {
      int fd = mb_accept (lfd, mb);
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

void tcp_server_task(connect_t *cp) {
  int fd, rc;

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, cp->host, cp->port, getaddrbyhost(cp->host));

  while (1) {
    sleep(PERIOD);
  }
}

void tcp_client_task(connect_t *cp) {
  int fd, rc;

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, cp->host, cp->port, getaddrbyhost(cp->host));

  while (1) {
    sleep(PERIOD);
  }
}

void udp_server_task(connect_t *cp) {
  struct sockaddr_in sin;
  fd_set fds, rfds;
  struct timeval tm;
  int fd, rc, nbyte, slen = sizeof(sin);
  char message[BUFSIZ];

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, cp->host, cp->port, getaddrbyhost(cp->host));

  fd = udphostsock(cp->host, cp->port);
  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);

  while (1) {
    fds = rfds;
    settimeout(&tm, 1, 0);
    if((rc = select(fd + 1, &fds, NULL, NULL, &tm)) < 0) {
      continue;
    }
    if(FD_ISSET(fd,&fds)) {
      nbyte = recvfrom(fd, message, BUFSIZ, 0, (struct sockaddr*)&sin, &slen);
      printf ("[DATA] Receiving...... %d bytes \n", nbyte);
    }
    sleep(PERIOD);
  }
}

void udp_client_task(connect_t *cp) {
  int fd, rc;

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, cp->host, cp->port, getaddrbyhost(cp->host));

  while (1) {
    sleep(PERIOD);
  }
}

void http_server_task(connect_t *cp) {
  int fd, rc;

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, cp->host, cp->port, getaddrbyhost(cp->host));

  while (1) {
    sleep(PERIOD);
  }
}

void http_client_task(connect_t *cp) {
  int fd, rc;

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, cp->host, cp->port, getaddrbyhost(cp->host));

  while (1) {
    sleep(PERIOD);
  }
}

void mqtt_sub_task(connect_t *cp) {
  int fd, rc;

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, cp->host, cp->port, getaddrbyhost(cp->host));

  while (1) {
    sleep(PERIOD);
  }
}

void mqtt_pub_task(connect_t *cp) {
  int fd, rc;

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, cp->host, cp->port, getaddrbyhost(cp->host));

  while (1) {
    sleep(PERIOD);
  }
}



static void* task(void* args) {
  connect_t *cp = (connect_t*)args;
  if(!cp) return;

  char path[BUFSIZ];

  if(!strcmp(cp->type, "MODBUS-TCP-SERVER")) modbus_server_task(cp);
  if(!strcmp(cp->type, "MODBUS-TCP-CLIENT")) modbus_client_task(cp);
  if(!strcmp(cp->type, "TCP-SERVER"))        tcp_server_task(cp);
  if(!strcmp(cp->type, "TCP-CLIENT"))        tcp_client_task(cp);
  if(!strcmp(cp->type, "UDP-SERVER"))        udp_server_task(cp);
  if(!strcmp(cp->type, "UDP-CLIENT"))        udp_client_task(cp);
  if(!strcmp(cp->type, "HTTP-SERVER"))       http_server_task(cp);
  if(!strcmp(cp->type, "HTTP-CLIENT"))       http_client_task(cp);
  if(!strcmp(cp->type, "MQTT-SUBSCRIBER"))   mqtt_sub_task(cp);
  if(!strcmp(cp->type, "MQTT-PUBLISHER"))    mqtt_pub_task(cp);
}

///////////////////////////////////////////////////////////////////////////////

void getFiles (char* d) {
  DIR *dp;
  char _d[BUFSIZ], v[BUFSIZ];
  char path[BUFSIZ];
  struct dirent *ep;

  if((dp = opendir (d)) != NULL) {
    while (ep = readdir (dp)) {
      if(!strcmp(ep->d_name,".") )   continue;
      if(!strcmp(ep->d_name,".."))   continue;

      if(ep->d_type == DT_DIR) {
        sprintf(_d,"%s/%s", d, ep->d_name);
        getFiles(_d);
      }
      else if(ep->d_type == DT_REG) {
        char name[BUFSIZ]="", host[BUFSIZ]="", type[BUFSIZ]="", topic[BUFSIZ]="";
        long queue;
        uint16_t port;

        sprintf(path, "%s/%s", d, ep->d_name);

        connect_t *cp = calloc(1, sizeof(connect_t));
        CH[max_thread] = cp;

        strcpy(cp->path, path);
        getconf(path, "name",      cp->name);
        getconf(path, "host",      cp->host);
        getconf(path, "type",      cp->type);
        getconf(path, "topic",     cp->topic);
        getconf(path, "port", v);  cp->port = atoi(v);
        getconf(path, "queue", v); cp->queue = atol(v);
        cp->id = max_thread;

        pthread_create(&cp->th, NULL, task, cp);
        max_thread++;
      }
    }
    (void) closedir (dp);
  }
}

int
main (int argc, char *argv[])
{
  char name[BUFSIZ];
  int c;
  while( (c = getopt(argc, argv, "n:h")) != -1) {
    switch(c) {
    case 'n':
      memcpy(name, optarg, strlen(optarg));
      break;
    }
  }
  getFiles ("/opt/radix/gateway.d");

  for (int i = 0; CH[i] != NULL; i++) {
    connect_t *cp = CH[i];
    pthread_join(cp->th, NULL);
  }
  pthread_exit (0);
}
