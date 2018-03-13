/*
 *  @file utils.h
 *  \brief Various fonctions that are useful for many programs:
 1. unix timestamp <-> string conversions
 2. string manipulation

 *
 *
 */


#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <unistd.h>
#include <cstdlib>
#include <sys/types.h>
#include <sys/param.h>
#include <grp.h>
#include <cstdio>
#include <iostream>
#include "maccompat.h"
#include "utils.h"

using namespace std;

const char eofline = '\n';


// Mac and Linux compatible function for monotonic time
double time_monotonic(){
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  double result = ts.tv_nsec;
  result = (result / 1.0e9) + ts.tv_sec;
  return result;
}


// Mac and Linux compatible function for monotonic time
double time_real(){
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  double result = ts.tv_nsec;
  result = (result / 1.0e9) + ts.tv_sec;
  return result;
}


// =============================================================================
// chops up line of space-separated words into table of strings, returns number of strings found
int chop_line(string s, vector <string>& word_table){
	stringstream ss;
	ss.str(s);
	int ctr(0);
	while (!ss.eof()){
		string tmp;
		ss>>tmp;
		if (tmp != to_string(eofline)){ 
			word_table.push_back(tmp);
			ctr++;
		}
	}
	return ctr;
}


// =============================================================================
int ctoi(char c){
  if (c >= '0' && c <= '9') {
    return (c - '0');
  } else {
    return 0;
  }
}

// =============================================================================
// converts unixtime into a string of format YYYY/MM/DD HH:MM:SS.msmsms
string UNIX_to_datetime(double timestamp){
  
  double dummy(0);
  int fraction = (int)round(fabs(modf(timestamp, &dummy)) * 1e3);
    
  const time_t h = (time_t) timestamp;
  struct tm* tmp;
	tmp = localtime(&h);
	
	char buf[32];
	strftime(buf, sizeof(buf), "%Y/%m/%d %H:%M:%S", tmp);
  string datetime  = string(buf) + "." + to_stringHP(fraction,3,3,'0');
	return datetime;
}


// =============================================================================
// converts unixtime into a string of format specified, conventions frin strftime function : http://www.cplusplus.com/reference/ctime/strftime/
string UNIX_to_datetime(double timestamp, string format){
  
  const time_t h = (time_t) timestamp;
  struct tm* tmp;
	tmp = localtime(&h);
	
	char buf[128];
	strftime(buf, sizeof(buf), format.c_str(), tmp);
  string datetime = string(buf);
	return datetime;
}

// =============================================================================
/// helper function for timed wait, computes the absolute time after the given timeout
// delay is microseconds
void get_absolute_timeout(timespec& ts, const unsigned int delay)
{
  // gets current time
  timeval tv;
  gettimeofday(&tv, NULL);
  // adds the needed microseconds
  unsigned int delay_s = (delay / 1000000);
  unsigned int delay_us = (delay % 1000000);
  tv.tv_sec += delay_s;
  tv.tv_usec += delay_us;
  // checks if we there is more than 1 second on the microseconds field, and correct accordingly
  if (tv.tv_usec >= 1000000) {
    tv.tv_sec += 1;
    tv.tv_usec -= 1000000;
  }
  // converts to timespec (us to ns)
  ts.tv_sec = tv.tv_sec;
  ts.tv_nsec = tv.tv_usec * 1000;
}

// =============================================================================

/// function to drop root privileges, returning to those of the calling user
/// (used when the program is invoked with sudo to do some operation that requires
/// root privileges, such as using realtime priority, but those privileges are
/// useless and dangerous to keep)
// function adapted from Secure Programming Cookbook
void drop_privileges() {
  gid_t newgid = getgid(), oldgid = getegid();
  uid_t newuid = getuid(), olduid = geteuid();
   
  /* If root privileges are to be dropped, be sure to pare down the ancillary
   * groups for the process before doing anything else because the setgroups(  )
   * system call requires root privileges.  Drop ancillary groups regardless of
   * whether privileges are being dropped temporarily or permanently.
   */
  if (!olduid) {
    setgroups(1, &newgid);
  }
   
  if (newgid != oldgid) {
    if (setregid(newgid, newgid) == -1) {
      perror("setregid");
      abort();
    }
  }
   
  if (newuid != olduid) {
    if (setreuid(newuid, newuid) == -1) {
      perror("setreuid");
      abort();
    }
  }
   
  /* verify that the changes were successful */
  if (newgid != oldgid && getegid() != newgid) {
    std::cerr << "Unable to drop privileges, aborting..." << std::endl;
    abort();
  }
  if (newuid != olduid && geteuid() != newuid) {
    std::cerr << "Unable to drop privileges, aborting..." << std::endl;
    abort();
  }
}

// =============================================================================

/// function to set realtime scheduling on the current process
bool set_realtime() {
  // switch to realtime priority
  sched_param sp;
  sp.sched_priority = 50;
  if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
    perror("WARNING, sched_setscheduler() failed");
    return false;
  } else {
    return true;
  }
}