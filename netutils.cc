//
//  netutils.cc
//  
//
//  Created by Danielle Mersch on 2/6/15.
//
//


#include "netutils.h"



// disables grouping of TCP packets, minimizes delays, send is immediate
// takes socket as parameter
void disable_nagle(const int s){
  uint32_t b = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*) &b, sizeof(b));
}


//receive function with time-out if no receive for some time
int block_recv(const int sk, const unsigned int timeout, void* out_data, unsigned int len, const bool oob){
  int i, j(0);
  char* data = (char*) out_data;
  
  while (len > 0){
    // if there is a timeout, enforces it
    if (timeout != 0) {
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(sk, &fds);
      timeval tv;
      tv.tv_sec = (timeout / 1000);
      tv.tv_usec = 1000 * (timeout % 1000);
      if (!oob) {
        i = select(sk + 1, &fds, NULL, NULL, &tv);
      } else {
        i = select(sk + 1, NULL, NULL, &fds, &tv);  // OOB data is considered an "exception" condition
      }
      // checks for timeout
      if (i == 0) {
        if (j == 0) {
          return -2;
        } else {
          return j;
        }
      }
      // some other error?
      if (i == -1) {
        return i;
      }
    }
    i = recv(sk, (char*) data, len, (oob ? MSG_OOB : 0));
    if (i == 0 || i == -1) {
      return i;
    }
    data += i;
    len -= i;
    j += i;
  }
  
  return j;
}