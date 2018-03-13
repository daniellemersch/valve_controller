#ifndef __pthread_event_h
#define __pthread_event_h

#include <pthread.h>
#include "utils.h"

class pthread_event {

public:
  pthread_event();
  ~pthread_event();
  
  /// waits for the event to be signaled (no timeout)
  void wait();
  
  /// \brief waits for the event to be signaled, or for a timeout
  /// \param timeout The maximum amount of time to wait for the event (in microseconds)
  /// \return true if the event was received, false if timed out while waiting
  bool timed_wait(const unsigned int timeout);
  
  /// signals the event
  void signal();

private:
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  volatile bool signaled;

};

#endif
