/*
***************************************************************************
*
* Author: Teunis van Beelen
*
* Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015 Teunis van Beelen
*
* Email: teuniz@gmail.com
*
***************************************************************************
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation version 2 of the License.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*
***************************************************************************
*
* This version of GPL is at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*
***************************************************************************
*/

/* Last revision: January 10, 2015 */

/* For more info and how to use this library, visit: http://www.teuniz.net/RS-232/ */

/* February 2015, Danielle Mersch:
 Modified version, no windows compatibility anymore! Arbitrary port name support, but port name needs to be specified. e.g. /dev/ttyS0 
  added RS232_PollComport_wTimeout*/


#include "rs232.h"
#include "utils.h"
#include <string>
#include <iostream>



int error;
struct termios new_port_settings;


int RS232_OpenComport(const char* comport_name, int baudrate, const char* mode){
  int baudr,
      status, comport_handle;


  switch(baudrate){
    case      50 : baudr = B50;
                   break;
    case      75 : baudr = B75;
                   break;
    case     110 : baudr = B110;
                   break;
    case     134 : baudr = B134;
                   break;
    case     150 : baudr = B150;
                   break;
    case     200 : baudr = B200;
                   break;
    case     300 : baudr = B300;
                   break;
    case     600 : baudr = B600;
                   break;
    case    1200 : baudr = B1200;
                   break;
    case    1800 : baudr = B1800;
                   break;
    case    2400 : baudr = B2400;
                   break;
    case    4800 : baudr = B4800;
                   break;
    case    9600 : baudr = B9600;
                   break;
    case   19200 : baudr = B19200;
                   break;
    case   38400 : baudr = B38400;
                   break;
    case   57600 : baudr = B57600;
                   break;
    case  115200 : baudr = B115200;
                   break;
    case  230400 : baudr = B230400;
                   break;
    /*case  460800 : baudr = B460800;
                   break;
    case  500000 : baudr = B500000;
                   break;
    case  576000 : baudr = B576000;
                   break;
    case  921600 : baudr = B921600;
                   break;
    case 1000000 : baudr = B1000000;
                   break;
    case 1152000 : baudr = B1152000;
                   break;
    case 1500000 : baudr = B1500000;
                   break;
    case 2000000 : baudr = B2000000;
                   break;
    case 2500000 : baudr = B2500000;
                   break;
    case 3000000 : baudr = B3000000;
                   break;
    case 3500000 : baudr = B3500000;
                   break;
    case 4000000 : baudr = B4000000;
                   break;
    */
    default      : printf("invalid baudrate\n");
                   return(1);
                   break;
  }

  int cbits=CS8,
      cpar=0,
      ipar=IGNPAR,
      bstop=0;

  if(strlen(mode) != 3)
  {
    printf("invalid mode \"%s\"\n", mode);
    return(1);
  }

  switch(mode[0])
  {
    case '8': cbits = CS8;
              break;
    case '7': cbits = CS7;
              break;
    case '6': cbits = CS6;
              break;
    case '5': cbits = CS5;
              break;
    default : printf("invalid number of data-bits '%c'\n", mode[0]);
              return(1);
              break;
  }

  switch(mode[1]){
    case 'N':
    case 'n': cpar = 0;
              ipar = IGNPAR;
              break;
    case 'E':
    case 'e': cpar = PARENB;
              ipar = INPCK;
              break;
    case 'O':
    case 'o': cpar = (PARENB | PARODD);
              ipar = INPCK;
              break;
    default : printf("invalid parity '%c'\n", mode[1]);
              return(1);
              break;
  }

  switch(mode[2]){
    case '1': bstop = 0;
              break;
    case '2': bstop = CSTOPB;
              break;
    default : printf("invalid number of stop bits '%c'\n", mode[2]);
              return(1);
              break;
  }

/*
http://pubs.opengroup.org/onlinepubs/7908799/xsh/termios.h.html

http://man7.org/linux/man-pages/man3/termios.3.html
*/
  /* need to open serial port as non-blocking, because of some
   USB-serial converter drivers on Mac */
  //comport_handle = open(comport_name, O_RDWR | O_NOCTTY /*| O_NDELAY*/); // Ubuntu
  // TEST
  comport_handle = open(comport_name, O_RDWR | O_NOCTTY | O_NDELAY); // Mac version, maybe Ubuntu
  if(comport_handle==-1){
    perror("unable to open comport ");
    return(-1);
  }
  /* now the port is open, set it to blocking mode */
  fcntl(comport_handle, F_SETFL, fcntl(comport_handle, F_GETFL) & ~O_NONBLOCK);

  memset(&new_port_settings, 0, sizeof(new_port_settings));  /* clear the new struct */
  new_port_settings.c_cflag = cbits | cpar | bstop | CLOCAL | CREAD;
  //new_port_settings.c_iflag = ipar;
  new_port_settings.c_oflag = 0;
  new_port_settings.c_lflag = 0;
  new_port_settings.c_cc[VMIN] = 1;      /* block until n bytes are received */
  new_port_settings.c_cc[VTIME] = 0;     /* block until a timer expires (n * 100 mSec.) */

  cfsetispeed(&new_port_settings, baudr);
  cfsetospeed(&new_port_settings, baudr);

  error = tcsetattr(comport_handle, TCSANOW, &new_port_settings);
  if(error==-1){
    close(comport_handle);
    perror("unable to adjust portsettings ");
    return(-1);
  }

  if(ioctl(comport_handle, TIOCMGET, &status) == -1){
    perror("unable to get portstatus");
    close(comport_handle);
    return(-1);
  }

  status |= TIOCM_DTR;    /* turn on DTR */
  status |= TIOCM_RTS;    /* turn on RTS */
/*
  if(ioctl(comport_handle, TIOCMSET, &status) == -1){
    perror("unable to set portstatus");
    close(comport_handle);
    return(-1);
  }
*/
  return(comport_handle);
}


int RS232_PollComport(int comport_handle, unsigned char* buf, int size){
  int n;
  n = read(comport_handle, buf, size);
  return(n);
}

int RS232_PollComport_wTimeout(int comport_handle, unsigned char* buf, int size, timeval timeout){
  
  fd_set set;
  FD_ZERO(&set);//clear the set
  FD_SET(comport_handle, &set);//add the comport to the set
  int n;
  double time_before = time_monotonic();
  n = select(comport_handle+1, &set, NULL, NULL, &timeout);
  double time_after = time_monotonic();
  if (n==-1){
    perror("Error select failed.");
  }else if(n==0){
    std::cout<<"Timeout occured."<<std::endl;
    std::string timeoutmsg = "Waited: " + to_stringHP(time_after - time_before, 9) + "\n";
/*
    printf("Trying to read from serial port... ");
    fflush(stdout);
    n = read(comport_handle, buf, size);
    printf("Result = %d\n", n);
*/
    std::cout<<timeoutmsg<<std::endl;
  }else{
    n = read(comport_handle, buf, size);
  }
  return(n);
}


int RS232_SendByte(int comport_handle, unsigned char byte){
  int n;
  n = write(comport_handle, &byte, 1);
  if(n<0){
    return(1);
  }
  return(0);
}


int RS232_SendBuf(int comport_handle, unsigned char* buf, int size){
  return(write(comport_handle, buf, size));
}


void RS232_CloseComport(int comport_handle){
  int status;
  if(ioctl(comport_handle, TIOCMGET, &status) == -1){
    perror("unable to get portstatus");
  }
  status &= ~TIOCM_DTR;    /* turn off DTR */
  status &= ~TIOCM_RTS;    /* turn off RTS */
  if(ioctl(comport_handle, TIOCMSET, &status) == -1){
    perror("unable to set portstatus");
  }
  close(comport_handle);
}

/*
Constant  Description
TIOCM_LE        DSR (data set ready/line enable)
TIOCM_DTR       DTR (data terminal ready)
TIOCM_RTS       RTS (request to send)
TIOCM_ST        Secondary TXD (transmit)
TIOCM_SR        Secondary RXD (receive)
TIOCM_CTS       CTS (clear to send)
TIOCM_CAR       DCD (data carrier detect)
TIOCM_CD        see TIOCM_CAR
TIOCM_RNG       RNG (ring)
TIOCM_RI        see TIOCM_RNG
TIOCM_DSR       DSR (data set ready)

http://man7.org/linux/man-pages/man4/tty_ioctl.4.html
*/

int RS232_IsDCDEnabled(int comport_handle){
  int status;
  ioctl(comport_handle, TIOCMGET, &status);
  if(status&TIOCM_CAR){
   return(1); 
  }else{
    return(0);
  }
}


int RS232_IsCTSEnabled(int comport_handle){
  int status;
  ioctl(comport_handle, TIOCMGET, &status);
  if(status&TIOCM_CTS){
    return(1);
  }else{
    return(0);
  }
}


int RS232_IsDSREnabled(int comport_handle){
  int status;
  ioctl(comport_handle, TIOCMGET, &status);
  if(status&TIOCM_DSR) {
    return(1);
  }else{
    return(0);
  }
}


void RS232_enableDTR(int comport_handle){
  int status;
  if(ioctl(comport_handle, TIOCMGET, &status) == -1){
    perror("unable to get portstatus");
  }
  status |= TIOCM_DTR;    /* turn on DTR */
  if(ioctl(comport_handle, TIOCMSET, &status) == -1){
    perror("unable to set portstatus");
  }
}


void RS232_disableDTR(int comport_handle){
  int status;
  if(ioctl(comport_handle, TIOCMGET, &status) == -1){
    perror("unable to get portstatus");
  }
  status &= ~TIOCM_DTR;    /* turn off DTR */
  if(ioctl(comport_handle, TIOCMSET, &status) == -1){
    perror("unable to set portstatus");
  }
}


void RS232_enableRTS(int comport_handle){
  int status;
  if(ioctl(comport_handle, TIOCMGET, &status) == -1){
    perror("unable to get portstatus");
  }
  status |= TIOCM_RTS;    /* turn on RTS */
  if(ioctl(comport_handle, TIOCMSET, &status) == -1){
    perror("unable to set portstatus");
  }
}


void RS232_disableRTS(int comport_handle){
  int status;
  if(ioctl(comport_handle, TIOCMGET, &status) == -1){
    perror("unable to get portstatus");
  }
  status &= ~TIOCM_RTS;    /* turn off RTS */
  if(ioctl(comport_handle, TIOCMSET, &status) == -1){
    perror("unable to set portstatus");
  }
}



void RS232_cputs(int comport_handle, const char* text){  /* sends a string to serial port */
  while(*text != 0){
    RS232_SendByte(comport_handle, *(text++));
  }
  syncfs(comport_handle);
}

void RS232_ClearSerialBuffer(int comport_handle){
  tcflush(comport_handle, TCIOFLUSH);
}


