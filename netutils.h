//
//  netutils.h
//  
//
//  Created by Danielle Mersch on 2/6/15.
//
//

#ifndef ____NETUTILS__
#define ____NETUTILS__

#include <iostream>

#include <sys/select.h> // includes for sockets
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>



// disables grouping of TCP packets, minimizes delays, send is immediate
// takes socket as parameter
void disable_nagle(const int s);


/// Receives a block of data of the specified size, if needed with a timeout
/// only returns if time out or if requeted amount of data is received, whichever happens first
/// \param timeout The timeout in milliseconds, or 0 if none required (wait forever)
/// \param oob If true, uses MSG_OOB to receive data (receives TCP urgent data only, which is by default separated by the rest of the data stream)
/// \return The number of bytes received, -2 in case of timeout and no data, -1 in case of other errors
int block_recv(const int sk, const unsigned int timeout, void* out_data, unsigned int len, const bool oob = false);


#endif /* defined(____NETUTILS__) */
