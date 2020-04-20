#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PERIOD 1

int temperature = 15;
void udp_server (char *host, uint16_t port) {
  struct sockaddr_in sin;
  fd_set fds, rfds;
  struct timeval tm;
  int fd, rc, nbyte, slen = sizeof(sin);
  char message[BUFSIZ];

  char MYTASK[] = "UDP   (S)";
  printf ("%s| %s:%d (%x)\n", MYTASK, host, port, ntohl(getaddrbyhost(host)));

  fd = udpsock(port);
  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);

  while (1) {
    fds = rfds;
    settimeout(&tm, 1, 0);
    if((rc = select(fd + 1, &fds, NULL, NULL, &tm)) < 0) {
      continue;
    }
    if(FD_ISSET(fd, &fds)) {
      nbyte = recvfrom(fd, message, BUFSIZ, 0, (struct sockaddr*)&sin, &slen);
      printf ("%s| -- udp data received (fd:%d, %d bytes\n", MYTASK, fd, nbyte);
      temperature += 1;
      int data = htonl(temperature);
      sendto(fd, (char*)&data, sizeof(temperature), 0, (struct sockaddr*)&sin, sizeof(sin));
      printf ("%s| -- tcp data sent (fd:%d, temperature:%d, %d bytes\n", MYTASK, fd, temperature, sizeof(temperature));

    }
    sleep(PERIOD);
  }
}

int main()
{
  udp_server ("34.84.40.30", 9009);
}
