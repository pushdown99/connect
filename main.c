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
#include <libhttp.h>
#include <curl/curl.h>

#include "MQTTAsync.h"
#include "MQTTClientPersistence.h"

#include "__ipc.h"
#include "sock.h"

#define MAX_THREAD 100
#define PERIOD     60

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

#define CONNECTmessage(ch, t) \
  char MYTASK[] = t;           \
do {                           \
  printf ("%s| %s:%d (%x)\n", MYTASK, ch->host, ch->port, ntohl(getaddrbyhost(ch->host))); \
} while (0)

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

void modbus_client_task(connect_t *ch) {
  uint16_t tab_registers[UT_REGISTERS_NB];
  modbus_t *mb;
  int fd, rc;

  CONNECTmessage(ch, "MODBUS(C)");

  while (1) {
    if ((fd = mb_connect(getaddrbyhost(ch->host), ch->port, &mb)) < 0) {
      //fprintf(stderr, "(%s:%d) Connection failed: %s\n", ch->host, ch->port, modbus_strerror(errno));
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

void modbus_server_task(connect_t *ch) {
  fd_set fds, rfds;
  struct timeval tm;
  modbus_t *mb;
  modbus_mapping_t *mb_mapping;
  int fd, lfd, maxfd;
  int i, rc;

  CONNECTmessage(ch, "MODBUS(S)");

  if ((lfd = mb_listen (ch->port, &mb)) < 0) {
     fprintf(stderr, "Connection and listen failed: %s\n", modbus_strerror(errno));
     return;
  }
  int header_length = modbus_get_header_length(mb);
  maxfd = lfd;
  printf("%s| -- listen allocated socket (fd: %d) \n", MYTASK, lfd);

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
      printf("%s| -- accept socket (fd: %d) \n", MYTASK, fd);
      if(fd < 0) continue;
      maxfd = (maxfd > fd)? maxfd : fd;
      FD_SET(fd,&rfds);
      continue;
    }
    for(int sockfd = lfd + 1; sockfd <= maxfd; sockfd++) {
      if(FD_ISSET(sockfd, &fds)) {
        if((rc=modbus_receive(mb, messages)) < 0) {
          FD_CLR(sockfd, &rfds);
          close(sockfd);
          printf("%s| -- client disconnected. (fd: %d) \n", MYTASK, sockfd);
          continue;
        }
        printf ("%s| -- modbus-tcp data received. (fd:%d, %d bytes) \n", MYTASK, sockfd, rc);

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

void tcp_server_task(connect_t *ch) {
  struct sockaddr_in sin;
  fd_set fds, rfds;
  struct timeval tm;
  int fd, lfd, maxfd, rc, nbyte, slen = sizeof(sin);
  char message[BUFSIZ];

  CONNECTmessage(ch, "TCP   (S)");

  lfd = tcp_listen (ch->port);
  maxfd = lfd;
  printf("%s| -- listen allocated socket (fd: %d) \n", MYTASK, lfd);

  FD_ZERO(&rfds);
  FD_SET(lfd, &rfds);

  while (1) {
    fds = rfds;
    settimeout(&tm,1,0);
    if((rc = select(maxfd + 1, &fds, NULL, NULL, &tm)) < 0) {
      continue;
    }
    if(FD_ISSET(lfd,&fds)) {
      int fd = accept (lfd, &sin, &slen);
      printf("%s| -- accept socket (fd: %d) \n", MYTASK, fd);
      maxfd = (maxfd > fd)? maxfd : fd;
      FD_SET(fd,&rfds);
      continue;
    }
    for(int sockfd = lfd + 1; sockfd <= maxfd; sockfd++) {
      if(FD_ISSET(sockfd, &fds)) {
        nbyte = recvfrom(sockfd, message, BUFSIZ, 0, (struct sockaddr*)&sin, &slen);
        if(nbyte <= 0) {
          FD_CLR (sockfd, &rfds);
          close (sockfd);
          printf("%s| -- client disconnected. (fd: %d) \n", MYTASK, sockfd);
        }
        printf ("%s| -- tcp data received. (fd:%d, %d bytes) \n", MYTASK, sockfd, nbyte);
      }
    }
  }
}

void tcp_client_task(connect_t *ch) {
  int fd, nbyte;
  char message[BUFSIZ];

  CONNECTmessage(ch, "TCP   (C)");
  while (1) {
    if((fd = tcp_connect(ch->host, ch->port)) < 0) {
      sleep (PERIOD);
      continue;
    }

    strcpy(message, "HELLO WORLD");
    if((nbyte=send(fd, message, strlen(message), 0)) < 0) {
      close (fd);
      continue;
    }
    printf ("%s| -- tcp data sent. (fd:%d, %d bytes) \n", MYTASK, fd, nbyte);

    sleep(PERIOD);
  }
}

void udp_server_task(connect_t *ch) {
  struct sockaddr_in sin;
  fd_set fds, rfds;
  struct timeval tm;
  int fd, rc, nbyte, slen = sizeof(sin);
  char message[BUFSIZ];

  CONNECTmessage(ch, "UDP   (S)");

  fd = udphostsock(ch->host, ch->port);
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
      printf ("%s| -- udp data received. (fd:%d, %d bytes) \n", MYTASK, fd, nbyte);
    }
    sleep(PERIOD);
  }
}

void udp_client_task(connect_t *ch) {
  struct sockaddr_in sin;
  char message[BUFSIZ];
  int fd, nbyte;

  CONNECTmessage(ch, "UDP   (C)");
  while (1) {
    fd = udpsock(0);
    bzero((char*)&sin,sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = getaddrbyhost(ch->host);
    sin.sin_port        = htons(ch->port);

    strcpy(message, "HELLO WORLD");
    if((nbyte=sendto(fd, message, strlen(message), 0, (struct sockaddr*)&sin, sizeof(sin))) <= 0) {
      continue;
    }
    printf ("%s| -- udp data sent. (%d bytes) \n", MYTASK,strlen(message));
    sleep (PERIOD);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// HTTP
//

static int begin_request_handler(struct httplib_context *ctx, struct httplib_connection *conn)
{
  char content[BUFSIZ];
  const struct lh_rqi_t *ri = httplib_get_request_info(conn);
  connect_t *ch = (connect_t *)ri->user_data;

  if(!strcmp(ri->request_method, "POST")) {
    int nbyte = httplib_read(ctx, conn, content, sizeof(content));
    dump(content, nbyte);
    sendq(ch->queue, (char*)content, nbyte);
  } 
  else {
    int content_length = snprintf(content, sizeof(content), "Hello from connect! Remote port: %d", ri->remote_port);

    httplib_printf(ctx, conn,
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: %d\r\n"
      "\r\n"
      "%s", content_length, content);
  }
  return 1;
}

struct string {
  char *ptr;
  size_t len;
};

void init_string(struct string *s) {
  s->len = 0;
  s->ptr = malloc(s->len+1);
  if (s->ptr == NULL) {
    fprintf(stderr, "malloc() failed\n");
    exit(EXIT_FAILURE);
  }
  s->ptr[0] = '\0';
}

size_t begin_response_handler (void *ptr, size_t size, size_t nmemb, struct string *s)
{
  size_t new_len = s->len + size*nmemb;
  s->ptr = realloc(s->ptr, new_len+1);
  if (s->ptr == NULL) {
    fprintf(stderr, "realloc() failed\n");
    exit(EXIT_FAILURE);
  }
  memcpy(s->ptr+s->len, ptr, size*nmemb);
  s->ptr[new_len] = '\0';
  s->len = new_len;

  return size*nmemb;
}

void http_server_task(connect_t *ch) {
  int fd, rc;
  char port[BUFSIZ];
  sprintf(port, "%d", ch->port);

  struct httplib_context *ctx;
  char *options[] = {"listening_ports", port, NULL};
  struct lh_clb_t callbacks;

  CONNECTmessage(ch, "HTTP  (S)");

  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.begin_request = begin_request_handler;
  ctx = httplib_start(&callbacks, ch, options);
  while (1) {
    sleep(PERIOD);
  }
  httplib_stop(ctx);
  return 0;
}

void http_client_task(connect_t *ch) {
  int fd, rc;
  char uri[BUFSIZ], body[BUFSIZ];
  CURL* curl;
  CURLcode res;

  CONNECTmessage(ch, "HTTP  (C)");
  while (1) {
    struct string body;
    init_string(&body);

    curl = curl_easy_init();
    sprintf(uri, "http://%s:%d/", ch->host, ch->port);

    curl_easy_setopt (curl, CURLOPT_URL, uri);
    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 0);
    curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, begin_response_handler);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    if((res = curl_easy_perform (curl)) != CURLE_OK) {
      fprintf(stderr, "Failed %s \n", curl_easy_strerror(res));
    }
    sendq(ch->queue, (char*)body.ptr, body.len);
    curl_easy_cleanup(curl);
    sleep(PERIOD);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MQTT
//

char *topic     = "kaa/12";
char *clientid  = "paho-c-sub";
int qos         = 0;
volatile int finished = 0;
int subscribed = 0;
int disconnected = 0;

typedef struct {
  MQTTAsync client;
  connect_t *ch;
} MQTTContext;

int messageArrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message) {
    connect_t* ch = (connect_t*)context;
    printf("[mqtt] received %d Bytes, Topic: %s, ", message->payloadlen, topicName);
    printf("Payload: %.*s\n", message->payloadlen, (char*)message->payload);

    printf("%.*s", message->payloadlen, (char*)message->payload);
    sendq(ch->queue, (char*)message->payload, message->payloadlen);

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

void onConnectLost (void *context, char *cause)
{
  MQTTAsync client = (MQTTAsync)context;
  MQTTAsync_connectOptions opts = MQTTAsync_connectOptions_initializer;
  int rc;
  printf("\nConnection lost\n     cause: %s\nReconnecting\n", cause);
  opts.keepAliveInterval = 20;
  opts.cleansession      = 1;

  if ((rc = MQTTAsync_connect(client, &opts)) != MQTTASYNC_SUCCESS) {
    fprintf(stderr, "Failed to start connect, return code %d\n", rc);
    finished = 1;
  }
}

void onDisconnect(void* context, MQTTAsync_successData* response) {
    disconnected = 1;
}

void onSubscribe(void* context, MQTTAsync_successData* response) {
    subscribed = 1;
}

void onSubscribeFailure(void* context, MQTTAsync_failureData* response) {
    fprintf(stderr, "Subscribe failed, rc %s\n", MQTTAsync_strerror(response->code));
    finished = 1;
}

void onConnect(void* context, MQTTAsync_successData* response) {
  MQTTContext *c                 = (MQTTContext*)context;
  MQTTAsync client               = (MQTTAsync)c->client;
  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
  int rc;

  if(!strcmp(c->ch->type, "MQTT-SUBSCRIBER")) {
    printf("Subscribing to topic %s with client %s at QoS %d\n", c->ch->topic, c->ch->name, qos);

    rc = MQTTAsync_setCallbacks(client, c, NULL, messageArrived, NULL);
    if (rc != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "Failed to set callbacks, return code: %s\n", MQTTAsync_strerror(rc));
        exit(EXIT_FAILURE);
    }

    opts.onSuccess = onSubscribe;
    opts.onFailure = onSubscribeFailure;
    opts.context = client;
    if ((rc = MQTTAsync_subscribe(client, c->ch->topic, qos, &opts)) != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "Failed to start subscribe, return code %s\n", MQTTAsync_strerror(rc));
        finished = 1;
    }
  }
}

void onConnectFailure(void* context, MQTTAsync_failureData* response) {
    fprintf(stderr, "Connect failed, rc %s\n", response ? MQTTAsync_strerror(response->code) : "none");
    //finished = 1;
}

MQTTContext* MQTTClient (connect_t *ch) {
  MQTTAsync   client;
  MQTTContext* context = malloc(sizeof(MQTTContext));
  MQTTAsync_connectOptions opts = MQTTAsync_connectOptions_initializer;
  char address [BUFSIZ], clientid[BUFSIZ];
  int rc;

  sprintf(address, "%s:%d", ch->host, ch->port);
  sprintf(clientid,"%s", ch->name);
  printf("%s %s \n", address, clientid);
  MQTTAsync_create (&client, address, clientid, MQTTCLIENT_PERSISTENCE_NONE, NULL);
  context->client = client;
  context->ch     = ch;

  opts.keepAliveInterval  = 20;
  opts.cleansession       = 1;
  opts.onSuccess          = onConnect;
  opts.onFailure          = onConnectFailure;
  opts.context            = context;
  opts.MQTTVersion        = MQTTVERSION_DEFAULT;
  opts.automaticReconnect = 1;

  if ((rc = MQTTAsync_connect(client, &opts)) != MQTTASYNC_SUCCESS) {
    printf("Failed to start connect (%s), return code %d\n", address, rc);
    return NULL;
  }
  return context;
}
  

void mqtt_sub_task(connect_t *ch) {
  int fd, rc;

  CONNECTmessage (ch, "MQTT  (S)");

  MQTTContext* context = MQTTClient(ch);

  while (!finished) usleep(10000L);
  MQTTAsync_destroy(&context->client);
  return;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MQTT-PUBLISHER
//


void mqtt_pub_task(connect_t *ch) {
  int fd, rc;

  CONNECTmessage (ch, "MQTT  (P)");
  MQTTContext* context = MQTTClient(ch);

  while (!finished) usleep(10000L);
  MQTTAsync_destroy(&context->client);
}



static void* task(void* args) {
  connect_t *ch = (connect_t *) args;
  if(!ch) return;

  char path[BUFSIZ];

  if(!strcmp (ch->type, "MODBUS-TCP-SERVER")) modbus_server_task (ch);
  if(!strcmp (ch->type, "MODBUS-TCP-CLIENT")) modbus_client_task (ch);
  if(!strcmp (ch->type, "TCP-SERVER"))        tcp_server_task (ch);
  if(!strcmp (ch->type, "TCP-CLIENT"))        tcp_client_task (ch);
  if(!strcmp (ch->type, "UDP-SERVER"))        udp_server_task (ch);
  if(!strcmp (ch->type, "UDP-CLIENT"))        udp_client_task (ch);
  if(!strcmp (ch->type, "HTTP-SERVER"))       http_server_task (ch);
  if(!strcmp (ch->type, "HTTP-CLIENT"))       http_client_task (ch);
  if(!strcmp (ch->type, "MQTT-SUBSCRIBER"))   mqtt_sub_task (ch);
  if(!strcmp (ch->type, "MQTT-PUBLISHER"))    mqtt_pub_task (ch);
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

        connect_t *ch = calloc(1, sizeof(connect_t));
        CH[max_thread] = ch;

        //strcpy(ch->path, path);
        getconf(path, "name",         ch->name);
        getconf(path, "host",         ch->host);
        getconf(path, "type",         ch->type);
        getconf(path, "topic",        ch->topic);
        getconf(path, "port", v);     ch->port = atoi(v);
        getconf(path, "messageq", v); ch->queue = strtol(v, NULL, 16);
        ch->id = max_thread;

        initq (ch->queue);
        pthread_create(&ch->th, NULL, task, ch);
        max_thread++;
      }
    }
    (void) closedir (dp);
  }
}

int
main (int argc, char *argv[])
{
  curl_global_init(CURL_GLOBAL_ALL);
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
    connect_t *ch = CH[i];
    pthread_join(ch->th, NULL);
  }
  pthread_exit (0);
  curl_global_cleaup();
}
