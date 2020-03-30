#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PERIOD 1

void udp_server (char *host, uint16_t port) {
  struct sockaddr_in sin;
  fd_set fds, rfds;
  struct timeval tm;
  int fd, rc, nbyte, slen = sizeof(sin);
  char message[BUFSIZ];

  printf ("[%s] host: %s:%d (%x)\n", __FUNCTION__, host, port, ntohl(getaddrbyhost(host)));

  fd = udphostsock(host, port);
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

int main()
{
  udp_server ("127.0.0.1", 9009);
}
