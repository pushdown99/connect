#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PERIOD 1

int tcp_server (char* host, uint16_t port)
{
  struct sockaddr_in sin;
  fd_set fds, rfds;
  struct timeval tm;
  int fd, lfd, maxfd, rc, nbyte, slen = sizeof(sin);
  char message[BUFSIZ];

  char MYTASK[] = "TCP   (S)";
  printf ("%s| %s:%d (%x)\n", MYTASK, host, port, ntohl(getaddrbyhost(host)));

  if((lfd = tcp_listen (port)) < 0) {
    perror("listen");
    exit (0);
  }
  maxfd = lfd;
  printf("%s| -- listen allocated socket (fd: %d) \n", MYTASK, lfd);

  FD_ZERO(&rfds);
  FD_SET(lfd, &rfds);

  while (1) {
    fds = rfds;
    settimeout(&tm,1,0);
    if((rc = select(maxfd + 1, &fds, NULL, NULL, &tm)) < 0) {
      perror ("select");
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

int main()
{
  tcp_server ("127.0.0.1", 9007);
}

