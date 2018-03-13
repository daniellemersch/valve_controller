//
//  data_format.h
//  
//
//  Created by Danielle Mersch on 2/9/15.
//
//

#ifndef ____DATA_FORMAT__
#define ____DATA_FORMAT__

#include <stdint.h>

const uint8_t DATA_QUERY = 1;
const uint8_t PULSE_QUERY = 2;
const uint8_t FLOW_QUERY = 3;
const uint8_t MAX_LENGTH = 63;  ///< maximum length of strings for odor name, same as in configuration.h


struct data_packet{
  double timestamp; // timestamp of event in ns
  double duration;  // pulse duration in ms
  bool event_type; // start events are 1, stop events are 0
  char alias[28]; // length of longest alias +1 (terminator), i.e. Blend123_12V_ABCD-ABCD-ABCD
  char odor[MAX_LENGTH + 1]; // allow for MAX_LENGTH character strings + terminator
};


#endif /* defined(____DATA_FORMAT__) */
