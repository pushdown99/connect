#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PERIOD 1

int tcp_client (uint16_t port)
{
  struct sockaddr_in sin;
  char message[BUFSIZ];
  int fd, nbyte;

  while (1) {
    if((fd = tcp_connect("127.0.0.1", port)) < 0) {
      sleep (PERIOD);
      continue;
    }

    bzero((char*)&sin,sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_port        = htons(9007);

    strcpy(message, "HELLO WORLD");
    if((nbyte=sendto(fd, message, strlen(message), 0, (struct sockaddr*)&sin, sizeof(sin))) <= 0) {
      continue;
    }
    printf ("[DATA] Sending...... %d bytes \n", strlen(message));
    sleep (PERIOD);
  }
}

int main()
{
  tcp_client (9007);
}

