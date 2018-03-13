//
//  MFC_data.h
//  
//
//  Created by Danielle Mersch on 2/9/15.
//
//

#ifndef ____MFC_DATA__
#define ____MFC_DATA__

#include <stdint.h>
#include <cstring>
#include <cstdlib>


static const int MAX_MFC = 26; // maximum nb of distinct MFCs

struct MFC_flows{
  double timestamp;
  double values[MAX_MFC]; ///< values of MFC
  char names[MAX_MFC]; ///< names of MFCs
  char flow_type[MAX_MFC]; ///< type of airflow that is controlled by MFC: 1=odor1, 2=odor2, 3=odor3, C=carrier, B=Boost
  bool validity[MAX_MFC];
  MFC_flows(){
    timestamp = 0.0;
    memset(&values,0,sizeof(values));
    memset(&names,0,sizeof(names));
    memset(&names,0,sizeof(flow_type));
    memset(&validity,0,sizeof(validity));
  }
  // TO IMPLEMENT: == operator ignoring timestamp info
};


#endif /* defined(____MFC_DATA__) */
