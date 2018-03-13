#include "pthread_event.h"
#include <sys/time.h>   // for gettimeofday()
#include <errno.h>      // for ETIMEDOUT
#include <iostream>
#include <cstdio>

pthread_event::pthread_event(){
  signaled = false;
  pthread_mutex_init(&mutex, NULL);
  pthread_cond_init(&condition, NULL);
}


pthread_event::~pthread_event(){
  pthread_mutex_destroy(&mutex);
  pthread_cond_destroy(&condition);
}


void pthread_event::wait(){
  // mutex needs to be locked for pthread_cond_wait to be called
  pthread_mutex_lock(&mutex);
  //int retries(0); // for debugging
  while (!signaled) {
    // pthread_cond_wait only returns if condition changes, however it can happen that pthread_cond_wait returns although condition has NOT changed -> therefore garded by while loop that checks signal arrival
    //std::cout << "waiters: " << condition.__data.__nwaiters << ", retry " << retries << std::endl;
    pthread_cond_wait(&condition, &mutex);
    //retries++;
  }
  signaled = false;
  pthread_mutex_unlock(&mutex);
}

bool pthread_event::timed_wait(const unsigned int timeout){
  // compute the absolute time when the timeout will expire
  timespec ts;
  get_absolute_timeout(ts, timeout);
  // mutex needs to be locked for pthread_cond_wait to be called
  pthread_mutex_lock(&mutex);
  int ret(0);  // return code of timed wait
  while (!signaled && ret != ETIMEDOUT) {
    // pthread_cond_timedwait only returns if condition changes, however it can happen that it returns although condition has NOT changed -> therefore garded by while loop that checks signal arrival
    ret = pthread_cond_timedwait(&condition, &mutex, &ts);
  }
  // if we timed out, do not set signaled back to false, as nothing says
  // it didn't switch to true just after the timeout expired
  if (ret != ETIMEDOUT) {
    signaled = false;
  }
  pthread_mutex_unlock(&mutex);
  if (ret == ETIMEDOUT) {
    return false;
  } else {
    return true;
  }
}

void pthread_event::signal(){
  pthread_mutex_lock(&mutex);
  signaled = true;
  if (pthread_cond_broadcast(&condition) != 0) {
    perror("pthread_cond_broadcast");
  }
  pthread_mutex_unlock(&mutex);
}

