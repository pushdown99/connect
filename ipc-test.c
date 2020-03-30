#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int main() 
{
  while(1) {
    char messages[BUFSIZ];
    int nbyte = recvq (0x5000L, messages);
    if(nbyte > 0) {
      printf("%.*s \n", nbyte, messages);
    }
    else 
      sleep(1);
  }
}
