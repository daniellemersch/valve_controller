 //
//  configuration.cpp
//  
//
//  Created by Danielle Mersch on 2/10/15.
//
//
// ! the extract_instruction code assumes there are 5 flow types only: 1,2,3,B,C If more exist, need to adjust code!


#include "configuration.h"


using namespace std;

const unsigned int MAX_DELAY = 4294967; // maximum delay in s that is accepted by usleep
const unsigned int MAX_PULSE = 4294967; // maximum pulse length in ms
//MAX_LENGTH = 63; ///< maximum length of strings for odor name, defined in data_format.h
const unsigned int NBMFC = 1; ///< total number of flow controller that are needed
//const unsigned int PULSE_WAIT = 4 ; // nb of seconds to wait after pulse
const unsigned int MAX_FLIES = 15; // maximum nb of flies that can be exposed to the airflow at the same time

static std::vector <char> FLOW_TYPE = make_vector<char>() <<'1'<<'2'<<'3'<<'C'<<'B';

static void write_data(ofstream& g, flow_data& flow, const double timestamp){
	g<<flow.ID<<",";
	g.precision(11);
	g<<fixed<<timestamp<<",";
	g.precision(2);
	g<<flow.pressure<<","<<flow.temperature<<",";
	g.precision(3);
	g<<flow.volumetric_flow<<","<<flow.mass_flow<<","<<flow.setpoint<<","<<flow.gas<<endl<<flush;
}


// =============================================================================
Configuration::Configuration(){
  nb_pulses = 0 ;
  interval = 0;
  delay = MAX_DELAY + 1 ;
  pulses_delivered = 0;
  partner = "";
  logfile = "";
  trigger = "internal";
  config_filename = "";
  comport_name="";
  comport_handle=-1;
  mfclogfile="";
  nb_mfc = 0;
  nb_events = 0;
  totalflow = 0.0;
  pulsewait = MAX_DELAY;
  max_air_flow = 0.0;
  flies = 0;
  waitstop_event = false;
  pthread_mutex_init(&mutex_MFC_com, NULL);
  pthread_mutex_init(&MFC_data_mutex, NULL);

}

// =============================================================================
Configuration::~Configuration(){
  //delete events from event_table
  for (unsigned int i(0); i < instructions.size(); i++){
    if (instructions[i].etype=="MFCSET"){
      delete (flowchange*)instructions[i].einfo;
    }else if(instructions[i].etype=="PULSE"){
      delete (pulse*)instructions[i].einfo;
    }else if (instructions[i].etype== "WAIT"){
      delete (double*)instructions[i].einfo; 
    }
  }

  pthread_mutex_destroy(&mutex_MFC_com);
  pthread_mutex_destroy(&MFC_data_mutex);
  g.close();
}

// =============================================================================
string Configuration::get_partner(){
  return partner;
}

// =============================================================================
unsigned int Configuration::get_nb_pulses(){
  return nb_pulses;
}

//=============================================================================
unsigned int Configuration::get_nb_events(){
  return nb_events;
}

//=============================================================================
bool Configuration::get_event(unsigned int idx, string& e){
  if (idx < nb_events){
    e = event_table[idx].etype;
    return true;
  }
  return false;
}

// =============================================================================
unsigned int Configuration::get_delay(){
  return delay;
}

// =============================================================================
double Configuration:: get_interval(){
  return interval;
}

// =============================================================================
string Configuration::get_comport_name(){
  return comport_name;
}

// =============================================================================
void Configuration::log(string message){
  g<<message<<endl;
}


// =============================================================================
std::string Configuration::get_trigger(){
  return trigger;
}

// =============================================================================
double Configuration::get_pulsewait(){
    return pulsewait;
}



// =============================================================================
unsigned int Configuration::update_pulses_delievered(){
  pulses_delivered++;
  return pulses_delivered;
}

// =============================================================================
/*bool Configuration:: get_pulse(unsigned int idx, pulse& p){
  if (idx < nb_pulses){
    p = pulse_table[idx];
    return true;
  }
  return false;
}*/

// =============================================================================
bool Configuration::get_interval_pulse(pulse& p){
  if (interval>0){
    p = interval_pulse;
    return true;
  }
  return false;
}

// =============================================================================
string Configuration::get_mfclog(){
  return mfclogfile;
}

// =============================================================================
int Configuration::get_nb_instructions(){
  return instructions.size();
}

//=============================================================================
bool Configuration::get_instruction(unsigned int idx, instruct& command) {
  if (idx >= 0 && idx < instructions.size()){
    command = instructions[idx];
    return true;
  }
  return false;
}


// =============================================================================
bool Configuration::set_flow(char ID, double flow){
     
  if (FlowController::is_valid_ID(ID)){
    pthread_mutex_lock(&mutex_MFC_com);
    mfc_map[ID].set_flow(flow);
    pthread_mutex_unlock(&mutex_MFC_com);
    return true;
  }else{
    cerr<<"There is no flow controller with ID "<<ID<<endl;
    return false;
  }
  
}

// =============================================================================
bool Configuration::balance_carrier_boost(char ID_carrier, double carrier_flow, char ID_boost, double boost_flow){
  
  if (FlowController::is_valid_ID(ID_carrier) && FlowController::is_valid_ID(ID_boost)){
    pthread_mutex_lock(&mutex_MFC_com);
    // balance_flows
    FlowController::balance_carrier_boost(mfc_map[ID_carrier], carrier_flow, mfc_map[ID_boost], boost_flow);
    pthread_mutex_unlock(&mutex_MFC_com);
    return true;
  }else{
    cerr<<"There is a problem: no flow controller with ID "<<ID_carrier<<" or "<<ID_boost<<endl;
    return false;
  }
  
}

// =============================================================================
void Configuration::init_MFC_data(){
  int ctr (0);
  for (std::map <char, FlowController>::iterator iter = mfc_map.begin(); iter != mfc_map.end(); iter++){
    flow_data tmp;
    pthread_mutex_lock(&mutex_MFC_com);
    iter->second.get_flow_data(tmp);
    pthread_mutex_unlock(&mutex_MFC_com);
    double timestamp = time_monotonic();
    

    pthread_mutex_lock(&MFC_data_mutex);
    MFC_data.timestamp = timestamp;
    MFC_data.names[ctr] = iter->first;
    MFC_data.flow_type[ctr] = iter->second.get_flowtype();
    MFC_data.values[ctr] = tmp.mass_flow;
    pthread_mutex_unlock(&MFC_data_mutex);
    ctr++;
  }
}

// =============================================================================
void Configuration::log_flow_data(ofstream& g1){
  int ctr (0);
  for (std::map <char, FlowController>::iterator iter = mfc_map.begin(); iter != mfc_map.end(); iter++){
    flow_data tmp;
    pthread_mutex_lock(&mutex_MFC_com);
    iter->second.get_flow_data(tmp);
    pthread_mutex_unlock(&mutex_MFC_com);
    double timestamp = time_real();//time_monotonic();
    write_data(g1, tmp, timestamp);
    
    // fill in flow value for MFC only if the specific MFC contributes to flow experienced by fly, not if the flow goes to waste
    //need to decode MFC contribution from pulse
    double flow(0.0);
    // TODO
    //if ( ){
    //  tmp.mass_flow;
    //}

    pthread_mutex_lock(&MFC_data_mutex);
    MFC_data.timestamp = timestamp;
    MFC_data.names[ctr] = iter->first;
    MFC_data.values[ctr] = tmp.mass_flow;
    pthread_mutex_unlock(&MFC_data_mutex);
    ctr++;
  }
}

// =============================================================================
void Configuration::convert_pulse_to_flowtypes(const string& pulse_type, std::vector <char>& flow_types_valid){
  if (pulse_type == "Carrier"){
    flow_types_valid.push_back('C'); // carrier air
    flow_types_valid.push_back('B'); // boost air
  }else{
    flow_types_valid.push_back('C'); // carrier air
    // find out which other flow goes to flies -> alias are of type Odor1_ Odor_2 Odor3_ Control1_ Control2_ Control3_ Blend12
    // determine types based on properties of alias
    if (pulse_type[0]=='C'){ // Control 1 or Control 2 or Control 3
      flow_types_valid.push_back(pulse_type[7]);
    }else if(pulse_type[0]=='O'){ // Odor 1 or Odor 2 or Odor 3
      flow_types_valid.push_back(pulse_type[4]);
    }else if(pulse_type[0]=='B'){ // Blend12, Blend23, Blend 13 or Blend123
      flow_types_valid.push_back(pulse_type[5]);
      flow_types_valid.push_back(pulse_type[6]);
      if (pulse_type[7]=='3'){
        flow_types_valid.push_back(pulse_type[7]);
      }
    }else{
      cerr<<"There had been an error. Please quit program."<<endl;
    }
  }
}

// =============================================================================
void Configuration::update_flow_destination(const string& pulse_type){
 
  // set validity to false for every flow
  bool tmp_validity[MAX_MFC]; 
  memset(&tmp_validity,0,sizeof(tmp_validity));

  vector <char> flow_types_valid;
  flow_types_valid.resize(0);
  convert_pulse_to_flowtypes(pulse_type, flow_types_valid);
  //cout<<"size filled: "<<flow_types_valid.size()<<endl;

  // for each flow type in map, indicate that flow should be set to true 
  for (unsigned int i(0); i < flow_types_valid.size(); i++){
    for (unsigned int j(0); j < MAX_MFC; j++){
      if (flow_types_valid[i] == MFC_data.flow_type[j]){
        tmp_validity[j]=true;
      } 
    }
  }

  pthread_mutex_lock(&MFC_data_mutex);
  memcpy(&MFC_data.validity, &tmp_validity, sizeof(MFC_data.validity));
  pthread_mutex_unlock(&MFC_data_mutex);
  
}

// =============================================================================
MFC_flows Configuration::get_MFC_data(){
  MFC_flows tmp;
  pthread_mutex_lock(&MFC_data_mutex);
  tmp = MFC_data;
  pthread_mutex_unlock(&MFC_data_mutex);
  return tmp;
}


// =============================================================================
void Configuration::set_interval_pulse(double interval){
  interval_pulse.odor_alias ="Carrier";
  interval_pulse.duration = interval;
  interval_pulse.name = "Carrier_air";
  interval_pulse.valve_blocks = valve_alias::parse_alias(interval_pulse.odor_alias);
}


 // =============================================================================
void Configuration::set_pulsewait(double p){
  pulsewait = p;
}


// =============================================================================
bool Configuration::read_config_file(string filename){
  config_filename = filename;
  
  ifstream f;
  f.open(filename.c_str());
  if (!f.is_open()){
    cerr<<"Cannot open configuration file."<<endl;
    return false;
  }

  map <char,bool> mfc_table;
  
  map <char,bool> flow_types_declared; 

  vector <string> config_input;
  config_input.resize(0);
  
  while(!f.eof()){
    string s;
    stringstream ss;
    getline (f, s);
    if (!s.empty()){
      config_input.push_back(s); // add input to config_input vector so can later be retrieved and written to log file
      if (s[0]!= '#'){ // comments start with #
        vector <string> word_table;
        unsigned int nb_words = chop_line(s, word_table);
	/*cout<<"Number of words found: "<<nb_words<<endl;
	for (int i(0); i<nb_words; i++){
	  cout<<"word["<<i<<"]="<<word_table[i]<<endl;			
	}*/

        if (nb_words >= 2){

          if (word_table[0] == "LOGFILE"){
            logfile = word_table[1];
            std::string datetime = UNIX_to_datetime(time_real(), "%Y%m%d-%H%M");//format is YYYYMMDD-HHMM
            logfile = logfile + "-" + datetime + ".txt";
            // test if path is valid
            ifstream f;
            f.open(logfile.c_str());
            if (f.is_open()){
              cerr<<"Logfile exists already."<<endl;
              return false;
            }
            if (nb_words >2){
              cerr<<"Warning: in line "<<s<<endl<<" parameters after word "<< logfile<< " are ignored."<<endl;
            }

          }else if (word_table[0] == "MFCLOG"){
            mfclogfile = word_table[1];
            std::string datetime = UNIX_to_datetime(time_real(), "%Y%m%d-%H%M");//format is YYYYMMDD-HHMM
            mfclogfile = mfclogfile + "-" + datetime + ".txt";
            // test if path is valid
            ifstream f;
            f.open(mfclogfile.c_str());
            if (f.is_open()){
              cerr<<"MFC logfile exists already."<<endl;
			        f.close();
              return false;
            }
            if (nb_words >2){
              cerr<<"Warning: in line "<<s<<endl<<" parameters after word "<< mfclogfile<< " are ignored."<<endl;
            }
            
          }else if (word_table[0] =="COMPORT"){
            comport_name = word_table[1];
            // check if comport is valid
            // open serial connection with multiflow controllers
            comport_handle = RS232_OpenComport(comport_name.c_str(), FlowController::get_baudrate(), FlowController::get_mode());
            if(comport_handle == -1){
             cerr<<"Can not open serial port "<<comport_name<<endl;
              return false;
            }
          
          // MFC   
          }else if (word_table[0] =="MFC"){
            if (comport_name ==""){
              cerr<<"COMPORT unknown. Specify the COMPORT path before adding multiflow controllers."<<endl;
              return false;
            }
            // set flow rates of each multiflow controller
	          char ID = word_table[1][0];  // ID of flow controller
            FlowController tmp; 
            if (!FlowController::is_valid_ID(ID)){
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"The specified flow controller ID is invalid."<<endl;
              return false;
            }
            // check that this is first declaration of MFC
            std::map <char, FlowController>::iterator iter = mfc_map.find(ID);
            bool found = (iter != mfc_map.end());
            if (found){
              cerr<<"Error: MFC "<<ID<<" has already been declared."<<endl;
              cerr<<"Each MFC can only be declared once per file."<<endl;
              return false;
            }
            
            if (nb_words > 2) {
              unsigned int range = atoi(word_table[2].c_str());
              if (!FlowController::is_valid_range(range)){
                cerr<<"Error in configuration file in line: "<<s<<endl;
                cerr<<"The specified maximum range is invalid."<<endl;
                return false;
              }
              
              char flow_type ('0');
              if (nb_words > 3){
                 flow_type = word_table[3][0];
                if (!vector_contains(FLOW_TYPE, flow_type)){
                  cerr<<"Error in configuration file in line: "<<s<<endl;
                  cerr<<"Error: the flow type specified is invalid."<<endl;
                  cerr<<"Valid flow types are: 1, 2, 3, C, B."<<endl;
                  return false;
                }
                map <char,bool>::iterator it = flow_types_declared.find(flow_type);
                if (it != flow_types_declared.end()){
                  cerr<<"Error in configuration file in line: "<<s<<endl;
                  cerr<<"Error: there is already an MFC with a flow of type "<<flow_type<<". It can only be declared once."<<endl;
                  return false;
                }else{
                  flow_types_declared[flow_type]=true;
                }
                // add flow to max_air_flow
                if (flow_type == 'C' || flow_type == 'B'){
                  max_air_flow += range;
                }
              }
              
              flow_MFC_LUT[flow_type]=ID; // fill LUT with flow type and MFC ID
              
              //set_flow(ID,flow);
              tmp.init(comport_handle, ID, range, flow_type);
              mfc_map[ID] = tmp;
              nb_mfc++;
              
				
            }else{
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"The max of flow range is missing."<<endl;
              return false;
            }

          }else if (word_table[0] == "PARTNER"){
            partner = word_table[1];
            // check if valid partner
            if (partner != "Igor" && partner != "Flytracker"){
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"The partner is unknown."<<endl;
              return false;
            }
            // check whether delay also specified
            if (delay < MAX_DELAY+1){
              cerr<<"Conflict in configuration file. Partner and delay are both specified, but only one can be set."<<endl;
            }
            if (nb_words >2){
              cerr<<"Warning: in line "<<s<<endl<<" parameters after word "<< partner<< " are ignored."<<endl;
            }
            
          }else if (word_table[0]== "DELAY"){
            delay = atoi(word_table[1].c_str());
            if (delay > MAX_DELAY){
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"The specified start delay is too long or negative."<<endl;
              return false;
            }
            // check whether partner also specified
            if (partner != ""){
              cerr<<"Conflict in configuration file. Partner and delay are both specified, but only one can be set."<<endl;
            }
          
          }else if (word_table[0] == "INTERVAL"){  // duration in seconds
            if (interval > 0){
              cerr<<"The interval has already been specified. You cannot specify it twice."<<endl;
              cerr<<"Error in configuration file in line: "<<s<<endl;
              return false;
            }
            interval = atof(word_table[1].c_str());
            if (interval > MAX_DELAY){
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"The specified interval duration is invalid."<<endl;
              return false;
            }
            set_interval_pulse(interval);
          
          }else if (word_table[0] == "PULSEWAIT"){
            if (pulsewait != MAX_DELAY){
              cerr<<"The pulsewait duration has already been specified. You cannot specify it twice."<<endl;
              cerr<<"Error in configuration file in line: "<<s<<endl;
              return false; 
            }
            if (get_interval() == 0){
              cerr<<"Unknown interval duration. You need to specifiy the interval duration first."<<endl;
              return false;
            }
            pulsewait = atoi(word_table[1].c_str());
            if ((pulsewait < 0) || (pulsewait >= MAX_DELAY)){
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"The specified pulsewait duration is invalid. Pulsewait needs to be [0 429495] seconds."<<endl;
              cerr<<"If the valve controller is used jointly with Igor or the Flytracker, the pulsewait duration should be shorter than the triggering interval."<<endl;
              return false;
            }

            unsigned int corrected_interval = interval - pulsewait;
            set_interval_pulse(corrected_interval);
            set_pulsewait(pulsewait);
          
          }else if (word_table[0] == "FLIES"){
            if (flies != 0){
              cerr<<"Error: The number of flies has already been declared."<<endl;
              return false;
            }
            flies = atoi(word_table[1].c_str());
            if ((flies < 1) || (flies > MAX_FLIES)){
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"The specified number of flies is invalid."<<endl;
              cerr<<"If the valve controller is used jointly with Igor, then only the number of flies needs to be one."<<endl;
              return false;
            }         

          }else if (word_table[0] == "TRIGGER"){
            trigger = word_table[1];
            if (trigger != "external" && trigger != "internal"){
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"The specified trigger is unknown."<<endl;
              return false;
            }
            if (trigger == "external") {
              set_interval_pulse(1);
              interval = 1;
              cout << "External trigger configuration: interval pulse set to 1 s as minimum." << endl;
            }

          // FLYFLOW events
          }else if (word_table[0] == "FLYFLOW"){
            if (flies == 0){
              cerr<<"Error: the number of flies has not been specified. You need to specify it first before indicating the flowrate per fly."<<endl;
              return false; 
            }
			      if (waitstop_event){
				      cerr<<"WARNING: the event: "<<s<<" will not be executed because there is a preceeding waitstop event!"<<endl;						
			      }

            double tmp = atof(word_table[1].c_str());
            // check that flow is valid (postif and smaller/equal to sum of MFC boost and MFC carrier range
            if (max_air_flow == 0){
              cerr<<"Error: the flows of the carrier and boost have not been specified. They need to be specified first. "<<endl;
              return false;
            }
            
            double totaltmp = flies * tmp;  // calculate totalflow and keep that value

            if (totaltmp <= 0 && totaltmp<= max_air_flow) {
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"Error: fly flow rate is invalid (either too high, zero or negativ)."<<endl;
              return false;
            }
            if (totalflow == 0){
              totalflow =totaltmp;
            }
            event ev;
            ev.etype = word_table[0];
            ev.einfo = new double (totaltmp);
            event_table.push_back(ev);
            nb_events++; 
          
          // WAIT event
          }else if (word_table[0] == "WAIT"){
			      if (waitstop_event){
				      cerr<<"WARNING: the event: "<<s<<" will not be executed because there is a preceeding waitstop event!"<<endl;						
			      }

            if (totalflow == 0){
              cerr<<"Error: the totalflow is missing. It needs to be specified before WAIT commands."<<endl;
              return false;
            }

            unsigned int delay = atoi(word_table[1].c_str()); // time to wait in seconds, max
            if (delay > MAX_DELAY){
              cerr<<"Error in configuration file in line "<<s<<endl;
              cerr<<"The delay to wait exceeds the maximum possible."<<endl;
              return false;
            }
            double* d = new (double);
            *d = (double)delay;
            event ev;
            ev.etype = word_table[0];
            ev.einfo = d;
            event_table.push_back(ev);
            nb_events++;

					
          // PULSE event
          }else if(word_table[0] == "PULSE"){  /// requires at least 4 words plus optional descriptor
            if (waitstop_event){
				      cerr<<"WARNING: the event: "<<s<<" will not be executed because there is a preceeding waitstop event!"<<endl;						
			      }
            if (totalflow == 0){
              cerr<<"Error: the totalflow is missing. It needs to be specified before PULSE commands."<<endl;
              return false;
            }
            
            if (nb_words < 4){ // minimal PULSE command has 4 words plus 1 optional one
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"Incomplete pulse information."<<endl;
              return false;
            }
            
            string word = word_table[1];
            pulse tmp;
            vector <int> valve_blocks = valve_alias::parse_alias(word);
            tmp.odor_alias = word;
            if (valve_blocks.empty()){
              cerr<<"Error in configuration file in line: "<<s<<endl;
              return false;
            }
            // determine nb of flow types for pulse (requires that MFCs have been all declared)
            std::size_t found = word.find_first_of("123"); // find first digit in alias,: indicates flow type
            if (found==string::npos){
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"Error: could not identify flow types from alias "<<word<<"."<<endl;
              return false;
            }
            unsigned int nbflows(1); //expected nb of flowrates for this pulse
            if (isdigit(word[found+1])){
              nbflows++;
              if (isdigit(word[found+2])){
                nbflows++;
              }
            }               
            
            // get pulse duration (in ms)
            unsigned int dur = atoi(word_table[2].c_str());
            if (dur > MAX_PULSE || dur < 0){
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"The specified pulse duration is invalid."<<endl;
              return false;
            }
            tmp.duration = dur;
            tmp.valve_blocks = valve_blocks;
            
            // get flow rates
            if (nb_words < (3 + nbflows)){
              cerr<<"Error in configuration file in line: "<<s<<endl;
              cerr<<"Missing flow rates."<<endl;
              return false;
            }
            // for each flow type in pulse, there should be a flowrate, however unused flowtypes are not specified
            double summed_flow (0.0); 
            for (unsigned int i(0); i< nbflows; i++){
              // search MFC corresponding to flow type
              map <char, char>::iterator iter;
              iter = flow_MFC_LUT.find(word[found+i]);
              if (iter == flow_MFC_LUT.end()){
                cerr<<"Error: unable to find a MFC that controls a flow of type: "<<word[found+i]<<endl;
                return false;
              }
              double flux = atof(word_table[3+i].c_str()); // flow for single fly
              flux = flux*flies; // calculate flow for all flies
              if (flux < 0 || flux > totalflow){
                cerr<<"Error: the specifid flow is invalid (either because it is negative or because it is higher than the specified totalflux."<<endl;
                return false;
              }
              summed_flow+= flux;
              // add flow rate info into pulse map with ID of flow type 
              tmp.MFC_flow[iter->first]=flux;
            }
            tmp.MFC_flow['B']=summed_flow;// Boost flow corresponds to summed flow rate of pulses
            
            // get optional description
            if (nb_words > (3 + nbflows)){
              tmp.name = word_table[3 + nbflows];
              if (tmp.name.length()> MAX_LENGTH){
                char odor[MAX_LENGTH];
                strncpy(odor, tmp.name.c_str(), sizeof(odor));
                odor[sizeof(odor) - 1] = 0;
                cerr<<"Warning: "<<tmp.name<<" will be truncated to "<< (string)odor<<" when sent to a partner."<<endl;
              }
              // add name to map LUT (TO BE IMPLEMENTED!!!!)
                
              if (nb_words> (4 + nbflows)){
                cerr<<"Warning: All characters after "<<tmp.name<<" were ignored."<<endl;
              }
            }
            //add event
            event ev;
            ev.etype = word_table[0];
            pulse* pl = new pulse;
            *pl = tmp;
            ev.einfo = pl;
            event_table.push_back(ev);
            nb_events++;
            nb_pulses++;
            
          
          }else{
            cerr<<"Error in configuration file in line: "<<s<<endl;
            cerr<<"Keyword unknown."<<endl;
            return false;
          }
          
        }else{

				// WAITSTOP event
          if (word_table[0] == "WAITSTOP"){
						double* d = new (double);
            *d = 0.0;	// unused, just to make coding simpler, waitstop can be deleted like a wait event					
						event ev;
            ev.etype = word_table[0];
            ev.einfo = d;
            event_table.push_back(ev);
            nb_events++;
						waitstop_event = true; 
					}else{
          	cerr<<"Error in configuration file in line: "<<s<<endl;
          	cerr<<"Parameters are missing."<<endl;
          	return false;
					}
        }
      }
    }
  }

  // check that all mandatory parameters are present   
  if (comport_name == ""){
    cerr<<"Missing information in configuration file."<<endl;
    cerr<<"No serial communication port specified."<<endl;
    return false;
  }
  if (nb_mfc< NBMFC){
    cerr<<"Missing information in configuration file."<<endl;
    cerr<<"Number of multiflow controllers insufficient."<<endl;
     return false;
  }
  if (flies!=1 && partner =="Igor"){
     cerr<<"Error: the numberof flies indicated is incompatible with running the valve controller with Igor. Only one fly can be exposed to air flow."<<endl;
     return false;
  }
  if(nb_pulses>0 && pulsewait == MAX_DELAY){
    cerr<<"Missing information in configuration file."<<endl;
    cerr<<"Pulsewait duration not specified in file."<<endl;
     return false;
  }
  if (logfile==""){
    cerr<<"Missing information in configuration file."<<endl;
    cerr<<"No logfile specified."<<endl;
     return false;
  }
  if (mfclogfile==""){
    cerr<<"Missing information in configuration file."<<endl;
    cerr<<"No MFC logfile specified."<<endl;
     return false;
  }
  if (partner== "" && delay > MAX_DELAY){
    cerr<<"Missing information in configuration file."<<endl;
    cerr<<"No partner and no delay specified. One needs to be specified."<<endl;
    return false;
  }
  if (partner =="" && trigger =="external"){
    cerr<<"Missing information in configuration file."<<endl;
  	cerr<<"Trigger is set to external, but no partner is specified. The valve controller needs a partner if the trigger is external."<<endl;
     return false;
  }
    
  
  
  // file in correct format
  cout<<"Configuration file valid."<<endl;
  g.open(logfile.c_str());
  if (!g.is_open()){
    cerr<<"Unable to open logfile: "<<logfile<<endl;
    return false;
  }
  

  for (unsigned int i(0); i < config_input.size(); i++){
    g<<"CONFIG "<<config_input[i]<<endl;
  }

  // convert events to list of instructions for valve controller
  if (!extract_instructions()){
    return false;
  }
  

  cout<<"Data will be logged to "<<logfile<<endl;
  if (nb_pulses > 0){
    cout<<nb_pulses<<" pulses will be presented ";
    if(trigger!="internal"){
      cout<<"triggered by "<<trigger<<endl;
      //cout<<"The minimum interval duration is "<<interval<<"ms"<<endl;
    }else{
      cout<<"with intervals of "<<interval<<"ms; triggered internally."<<endl;
    }
  }
  if (delay == 0){
    cout<<"Valve controller will be started immidiatly."<<endl;
  }else if (partner != ""){
    cout<<"Valve controller will be started by "<<partner<<endl;
  }else{
    cout<<"Valve controller will be started in "<<delay<<" seconds."<<endl;
  }
  
  
  return true;
}


// =============================================================================
int Configuration::find_next_event(unsigned int i){
  bool found (false);
  int idx(-1);
  i++;
  while (i < event_table.size() && !found){
    if (event_table[i].etype  == "FLYFLOW" || event_table[i].etype == "PULSE"){
      found = true;
      idx = i;
    }
		if (event_table[i].etype == "WAITSTOP"){
			found = true; 
		} 
    i++;
  }
  return idx;
}


// =============================================================================
// adds MFCSET instructions for boost and carrier air & updates current_flow map
// returns true if successful, false if error
bool Configuration::update_boost_carrier_flow(double boostflow, double carrierflow, map <char, double>& current_flow, bool user){

  // add two MFCSET instructions to set carrier and boost
  instruct tmp;
  tmp.user = user; // event specified internally, following user request to change flows
  // copy event to instruction table
  tmp.etype = "MFCSET2";
  double_flowchange* flch = new double_flowchange;
  
  //determine which MFC regulates boost air
  std::map <char, char>::iterator iter = flow_MFC_LUT.find('B');
  if (iter == flow_MFC_LUT.end()){
    cerr<<"Error: could not find entry for boost flow. "<<endl;
    return false;
  }
  // boost flow
  flch->ID_boost = iter->second;  // ID of MFC
  flch->flow_boost = boostflow; // flowrate
  
  // find ID of MFC regulating carrier air
  iter = flow_MFC_LUT.find('C');
  if (iter == flow_MFC_LUT.end()){
    cerr<<"Error: could not find entry for carrier flow. "<<endl;
    return false;
  }
  // carrier flow
  flch->ID_carrier = iter->second; // ID of MFC
  flch->flow_carrier  = carrierflow; // flowrate
  
  tmp.einfo = flch;
  instructions.push_back(tmp);
  
  //update flows in current_flow map
  current_flow['B'] = boostflow;
  current_flow['C'] = carrierflow;
  
  return true;

}

// =============================================================================
// adds MFCSET instructions for pulse if needed & updates current_flow accordingly
// returns true if successful, false if error
bool Configuration::update_flow_rate_for_next_pulse(const event& ev, map <char, double>& current_flow){
  // if the current flows differ from those needed for the pulse, also add MFCSET commands for the flows for pulses
  // to do that scan flows of pulse, for every pulse flow that is not a boost or carrier type, compare flow rates to current ones
  // if they differ, add a MFCSET command for that flow
  // for each flow in pulse
  
  //cout<<"in pulse flow update"<<endl;
  for (map <char, double>::iterator iter2 = (((pulse*)ev.einfo)-> MFC_flow).begin(); iter2 != ((pulse*)ev.einfo)-> MFC_flow.end(); iter2++){
    //
    //cout<<"comparing "<<iter2->first<<endl;
    if (iter2->first != 'B' && iter2->first != 'C'){  // if the flow is different from carrier and boost
      // compare flow rate for pulse to current_flow rates for the given flow type
      map <char, double>::iterator iter3 = current_flow.find(iter2->first);
      if(iter3 == current_flow.end()){
        cerr<<"Error: Unable to find flow type specified in pulse, in the current flow map. This should not happen."<<endl;
        return false;
      }
      //cout<<"before pulses differ"<<endl;
      // if the pulse flow differs from the current flow, add MFCSET
      if(iter2->second != iter3->second){
        instruct istr;
        istr.user = 0;
        istr.etype = "MFCSET";
        flowchange* flch = new flowchange;
        // identify MFC ID associated with flow
        std::map <char, char>::iterator iter = flow_MFC_LUT.find(iter2->first);
        if(iter == flow_MFC_LUT.end()){
          cerr<<"Error: Unable to find flow type in the MFC LUT map. This should not happen."<<endl;
          return false;
        }
        flch->ID = iter->second; //ID of MFC
        flch->flow = iter2->second; // set flow rate to that needed for pulse
        istr.einfo = flch;
        instructions.push_back(istr);
      }
      current_flow[iter3->first] = iter2->second;// update flow also in current flow map
    }
  }
  return true;
}

// =============================================================================
// copy pulse flow data to current_flow
void Configuration::update_current_with_pulse_flows(map <char, double>& current_flow, map <char, double>& MFC_flow_pulse){
  for (map <char, double>::iterator it_p = MFC_flow_pulse.begin(); it_p != MFC_flow_pulse.end(); it_p++){
    //cout<<"updating current"<<endl;
    current_flow[it_p->first] = it_p->second; 
  }
}


// =============================================================================
void Configuration::display_instructions(ostream& output){
  for (unsigned int i (0); i< instructions.size(); i++){
     output<<"INSTRUCT ["<<i<<"] "<<instructions[i].etype<< " (" <<instructions[i].user<<") -> ";
     if (instructions[i].etype == "WAIT"){
        output<<" "<<*(double*)instructions[i].einfo<<" sec"<<endl;
     }else if (instructions[i].etype == "PULSE"){
        pulse* pls = (pulse*)instructions[i].einfo; 
        output<< " "<< pls->odor_alias<<" - "<<pls->duration<<"ms";
        for (map <char, double>::iterator it = pls->MFC_flow.begin(); it != pls->MFC_flow.end(); it++){
          output<<" "<<it->first<<": "<<it->second/(double)flies;
        }
        output<<endl;
     }else if (instructions[i].etype == "MFCSET"){
        flowchange* fl = (flowchange*)instructions[i].einfo;
        output<<" "<<fl->ID<<": "<<fl->flow/(double)flies<<endl;
     }else if (instructions[i].etype == "MFCSET2"){
       double_flowchange* fl = (double_flowchange*)instructions[i].einfo;
       output<<" "<<fl->ID_carrier<<": "<<fl->flow_carrier/(double)flies<<" and ";
       output<<" "<<fl->ID_boost<<": "<<fl->flow_boost/(double)flies<<endl;
     }else if (instructions[i].etype == "WAITSTOP"){
				output<<" waiting user to stop with CTRL+C"<<endl;
		}
  }
}

// =============================================================================
void Configuration::add_wait(double delay, bool user){
  instruct tmp;
  double* t = new double (delay);
  // copy event to instruction table
  tmp.user = user; // event specified by user
  tmp.etype = "WAIT";
  tmp.einfo = t;
  instructions.push_back(tmp);   
}

// =============================================================================
bool Configuration::extract_instructions(){
  
  // determine initial flowrates of all MFCs
  double totflow = totalflow;

  map <char, double> current_flow; // type of flow (1,2,3,B,C) and flowrate
  for (map <char, char>::iterator iter = flow_MFC_LUT.begin(); iter != flow_MFC_LUT.end(); iter++){
    // ID of current_flow is fly_type, associated element is flow rate, e.g. current_flow['1'] = 0.2
    // all flow type are listed in flow_MFC_LUT, the flow information is listed in mfc_map
    current_flow[iter->first]= 1;
  }
  bool first (true);
  //cout<<"There are "<< event_table.size()<< " events."<<endl;
  
  for (unsigned int i(0); i < event_table.size(); i++){
    
    //cout<<"event is: "<<event_table[i].etype<<endl;

    // event is a Flyflow change
    if (event_table[i].etype == "FLYFLOW"){
      // update new total flow
      totflow = *((double*)event_table[i].einfo); // get new flow rate (already calculated for all flies)
      
      // to define how to split flow rates between MFC carrier and MFC boost,
      // need to find next event of type TOTALFLOW or PULSE
      int idx = find_next_event(i);
      
      // if there is future event and this event is a PULSE, then the boost contribution needs to match the pulse contribution, carrier fills until totalflow is reached
      if (idx != -1 && event_table[idx].etype == "PULSE"){ 
        
        // determine flow rate of carrier during pulse: totflow - pulse_flow (or totflow - boost_flow)
        //we use boost flow for calculations, because requires checking only a single MFC
        // we search ID of boost MFC
        double boostflow = ((pulse*)event_table[idx].einfo)->MFC_flow['B'];
        double carrierflow = totflow - boostflow;
        // update event_table entry: add carrier flow to pulse
        //(((pulse*)event_table[i].einfo)-> MFC_flow).insert(pair < char,double> ('C', carrierflow));
        
        if (!update_boost_carrier_flow(boostflow, carrierflow, current_flow, true)){
          return false;
        }
        //cout<<"after boost"<<endl;
        // update flow rates for pulse if needed, will update current_flow automatically
        if (!update_flow_rate_for_next_pulse(event_table[idx], current_flow)){
          return false;
        }

        // set flow rate of flow controllers
        if (first){
          // for every declared MFC, identify flowrate at start based on its type
          for (map <char, FlowController>::iterator iter = mfc_map.begin(); iter != mfc_map.end(); iter++){
            char ft = iter->second.get_flowtype();  // determine flowtype of given MFC
            map <char, double>::iterator iter2 = current_flow.find(ft); // find flow type among current flows
            if(iter2 == current_flow.end()){
              cerr<<"Error: flowtype "<<ft<<" missing."<<endl;
              return false;
            }
            //(iter->second).set_flow(iter2->second); // set flow rate of MFC to that of current flow of same type
          }
          first = false;
        }
        
        // NEW: add a 1sec wait after MFC flow changes
        add_wait(1, false);
        
      // if there is no future event or the future event is TOTALFLOW event -> totflow can be split proportionally to range between MFC carrier and MFC boost
      }else{
        // insert two MFCSET instructions             
        // find MFC ID of carrier air
        std::map <char, char>::iterator iter = flow_MFC_LUT.find('C');
        if (iter == flow_MFC_LUT.end()){
          cerr<<"Error: could not find MFC regulating carrier flow. "<<endl;
          return false;
        }
        int range_carrier = mfc_map[iter->second].get_range();
        // calculate flowrates for MFCSET events
        double carrierflow = totflow * (range_carrier/(double)max_air_flow); //flow contribtion of each controller is proportional to total range
        
        // find MFC ID of boost air
        iter = flow_MFC_LUT.find('B');
        if (iter == flow_MFC_LUT.end()){
          cerr<<"Error: could not find MFC ID of boost air. "<<endl;
          return false;
        }
        bool MFC_change = false;
        int range_boost = mfc_map[iter->second].get_range();
        double boostflow = totflow * (range_boost/(double)max_air_flow); //flow contribtion of each controller is proportional to total range
                // update flow in current_flow map
        current_flow['C'] = carrierflow;
        current_flow['B'] = boostflow;
        if (!update_boost_carrier_flow(boostflow, carrierflow, current_flow, false)){
          return false;
        }else{
          add_wait(1, false);
          MFC_change = true;
        }
        
      }
    
    // event is a Wait  
    }else if(event_table[i].etype == "WAIT"){
      /*instruct tmp;
      double* t = new double (*(double*)event_table[i].einfo);
      // copy event to instruction table
      tmp.user = true; // event specified by user
      tmp.etype = event_table[i].etype;
      tmp.einfo = t;
      instructions.push_back(tmp);
      */
      add_wait((*(double*)event_table[i].einfo), true);
      
		// event is a Waitstop: means that system remains in current configuration and runs until stopped with CTRL+C  
    }else if(event_table[i].etype == "WAITSTOP"){
      instruct tmp;
      double* t = new double (0.0);
      // copy event to instruction table
      tmp.user = 1; // event specified by user
      tmp.etype = "WAITSTOP";
      tmp.einfo = t;
      instructions.push_back(tmp);

    // event is a Pulse  
    }else if(event_table[i].etype == "PULSE"){
      instruct tmp;
      // if not yet specified complement carrier flow of pulse to reach totflow
      pulse* pls = new pulse (*(pulse*)event_table[i].einfo);
      //map <char, double>::iterator iter = ((pulse*)event_table[i].einfo)->MFC_flow.find('C');
      // calculate carrier flow rate
      pls->MFC_flow['C'] = totflow - (((pulse*)event_table[i].einfo)->MFC_flow)['B'];
      current_flow['C'] = totflow - ((pulse*)event_table[i].einfo)->MFC_flow['B'];
      
      // copy pulse event to instruction table
      tmp.user = true; // event specified by user
      tmp.etype = event_table[i].etype;
      tmp.einfo = pls;
      instructions.push_back(tmp);
      
      add_wait(pulsewait, false);

      // need to update current_flow with values from pulse
      update_current_with_pulse_flows(current_flow, ((pulse*)event_table[i].einfo)->MFC_flow);

      // need to find next event of type TOTALFLOW or PULSE
      int idx = find_next_event(i);
      // NEW: variable to determine whether MFC flow changed
      bool MFC_change = false;
      // if there an event in the future and it is a pulse, adjust flow rates for the pulse if needed, if it is a different event or no event, do nothing
      if(idx != -1 && event_table[idx].etype == "PULSE"){
        // adjust flow rates
        if (!update_flow_rate_for_next_pulse(event_table[idx], current_flow)){
          return false;
        }else{
          MFC_change = true;
        }
        // find out whether we need to adjust the flow rates of the boost and carrier air as well
        if (((pulse*)event_table[idx].einfo)->MFC_flow['B'] != current_flow['B']){ // current boost and next pulse differ, adjust boost & carrier
          double boostflow = ((pulse*)event_table[idx].einfo)->MFC_flow['B'];
          double carrierflow = totflow - boostflow;
          if (!update_boost_carrier_flow(boostflow, carrierflow, current_flow, false)){
            return false;
          }else{
            MFC_change = true;
          }
        }
      }
      
      // NEW: add wait to completment pulse-wait duration so it reaches interval_duration
      // if external trigger, then wait only 1s (time needed for MFCs changes to converge)
      if (trigger == "internal"){
        double interval_complement = interval - pulsewait;
        if (interval_complement <= 0.0){
          cerr<<" The duration of the interval complement is invalid (<= 0). Revise."<<endl;
          return false;
        }
        add_wait(interval_complement, false);
      }else{
        if (MFC_change){
          // if external trigger, then there is a 1sec wait after MFC changes
          add_wait(1, false);
        }
      }
      
    }
  }

  //delete events from event_table
  for (unsigned int i(0); i < event_table.size(); i++){
    if (event_table[i].etype=="FLYFLOW"){
      delete (flowchange*)event_table[i].einfo;
    }else if(event_table[i].etype=="PULSE"){
      delete (pulse*)event_table[i].einfo;
    }else if (event_table[i].etype== "WAIT" ||event_table[i].etype== "WAITSTOP"){
      delete (double*)event_table[i].einfo; 
    }
    
  }

  //
  //display_instructions(cout);
  display_instructions(g);
  return true; 
}




