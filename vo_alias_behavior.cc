
#include "vo_alias_behavior.h"


using namespace std;



/// valves corresponding to the different vials (A...D) for each of the 3 odours
static const int vial_valves[3][2] = {
  {  V_ODOR1_VIALA, V_ODOR1_VIALB },  // odour 1
  {  V_ODOR2_VIALA, V_ODOR2_VIALB },  // odour 2
  {  V_ODOR3_VIALA, V_ODOR3_VIALB }   // odour 3
};

/// valves corresponding to the different vials (A...C) for each of the 3 controls
static const int control_vial_valves[3][1] = {
  {  V_CONTROL1_VIALA },  // control 1
  {  V_CONTROL2_VIALA },  // control 2
  {  V_CONTROL3_VIALA }   // control 3
};



// Validate vial IDs (possible ID's are A, B for behavior, A,B,C,D for physiology) e.g. Blend12_8V_ABCD-ABCD
bool valve_alias::validate_vials(const string names){
  char prev = 0;
  bool present[4];

  // initialisation
  for (int i(0); i < 4; i++) {
    present[i] = false;
  }

  for (unsigned int i(0); i < names.length(); i++) {
    if (names[i] < 'A' || names[i] > 'B') { // ephys diff
      return false;
    }
    // needs to be strictly in increasing order
    if (names[i] <= prev) {
      return false;
    }
    if (present[names[i] - 'A']) {
      return false;
    }
    prev = names[i];
    present[names[i] - 'A'] = true;
  }

  return true;
}

// Validate control vial IDs
bool valve_alias::validate_control_vials(const string names){
  char prev = 0;
  bool present[3];

  for (int i(0); i < 3; i++) {
    present[i] = false;
  }

  for (unsigned int i(0); i < names.length(); i++) {
    if (names[i] != 'A'){  // ephys diff
      return false;
    }
    if (names[i] <= prev) {
      return false;
    }
    if (present[names[i] - 'A']) {
      return false;
    }
    prev = names[i];
    present[names[i] - 'A'] = true;
  }

  return true;
}

/// Converts an alias of type "OdourN_xV_..." to a vector of open valves
vector<int> valve_alias::parse_odour(const string name){
  
  // checks the syntax
  if (name.length() < 11 || name[8] != 'V' || name[9] != '_') {
    cerr << "Invalid odour alias: " << name << endl;
    return vector<int>();
  }

  // odour number
  unsigned int odour = ctoi(name[5]);
  if (odour < 1 || odour > 3) {
    cerr << "Invalid odour in alias: " << name << endl;
    return vector<int>();
  }

  // validate if the number of vials corresponds to how many are listed
  unsigned int vials = ctoi(name[7]);
  string vial_list = name.substr(10);
  if (vials != vial_list.length() || !validate_vials(vial_list)) {
    cerr << "Invalid odour alias: " << name << endl;
    return vector<int>();
  }

  // base open valves for selected odour
  vector<int> result;

  // manifold waste valves used in NO position
  /*if (odour == 1) {
    result = make_vector<int>() << V_BLEND1 << V_WASTE1 << V_CARRIER << V_ODOR1_WASTE << V_BOOST; // ephys diff
  } else if (odour == 2) {
    result = make_vector<int>() << V_BLEND2 << V_WASTE2 << V_CARRIER << V_ODOR2_WASTE << V_BOOST; // ephys diff
  } else if (odour == 3) {
    result = make_vector<int>() << V_BLEND3 << V_WASTE3 << V_CARRIER << V_ODOR3_WASTE << V_BOOST; // ephys diff
  }*/

  // manifold waste valves used in NC position, but other waste is NO and carrier is NO
  if (odour == 1) {
    result = make_vector<int>() << V_BLEND1 << V_WASTE1 << V_ODOR2_WASTE << V_ODOR3_WASTE << V_CARRIER;
  } else if (odour == 2) {
    result = make_vector<int>() << V_BLEND2 << V_WASTE2 << V_ODOR1_WASTE << V_ODOR3_WASTE << V_CARRIER;
  } else if (odour == 3) {
    result = make_vector<int>() << V_BLEND3 << V_WASTE3 << V_ODOR1_WASTE << V_ODOR2_WASTE << V_CARRIER;
  }

  // add valves corresponding to vials for selected odour
  for (unsigned int i(0); i < vials; i++) {
    result.push_back(vial_valves[odour - 1][vial_list[i] - 'A']);
  }

  return result;
}


/// Converts an alias of type "ControlN_nV_X..." to a vector of open valves
vector<int> valve_alias::parse_control(const string name){
  
  if (name.length() < 12 || name[8] != '_' || name[11] != '_' || name[10] != 'V') {
    cerr << "Invalid control alias: " << name << endl;
    return vector<int>();
  }

  unsigned int control = ctoi(name[7]);
  unsigned int vials = ctoi(name[9]);

  if (control < 1 || control > 3 || vials != 1) {
    cerr << "Invalid control alias: " << name << endl;
    return vector<int>();
  }

  string vial_list = name.substr(12);
  if (vial_list.length() != vials || !validate_control_vials(vial_list)) {
    cerr << "Invalid control alias: " << name << endl;
    return vector<int>();
  }

  // base open valves for selected control
  vector<int> result;

// waste valves used in NO position
  /*if (control == 1) {
    result = make_vector<int>() << V_BLEND_C1 << V_WASTE1 << V_CARRIER << V_ODOR1_WASTE << V_BOOST; // ephys diff
  } else if (control == 2) {
    result = make_vector<int>() << V_BLEND_C2 << V_WASTE2 << V_CARRIER << V_ODOR2_WASTE << V_BOOST; // ephys diff
  } else if (control == 3) {
    result = make_vector<int>() << V_BLEND_C3 << V_WASTE3 << V_CARRIER << V_ODOR3_WASTE << V_BOOST; // ephys diff
  }*/

  // waste valves used in NC position but carrier in NO
  if (control == 1) {
    result = make_vector<int>() << V_BLEND_C1 << V_WASTE1 << V_ODOR2_WASTE << V_ODOR3_WASTE << V_CARRIER;
  } else if (control == 2) {
    result = make_vector<int>() << V_BLEND_C2 << V_WASTE2 << V_ODOR1_WASTE << V_ODOR3_WASTE << V_CARRIER;
  } else if (control == 3) {
    result = make_vector<int>() << V_BLEND_C3 << V_WASTE3 << V_ODOR1_WASTE << V_ODOR2_WASTE << V_CARRIER;
  }

  // add valves corresponding to vials for selected control
  // currently one but implemented so it is easy to extend to many: currently vials = 1
  for (unsigned int i(0); i < vials; i++) {
    result.push_back(control_vial_valves[control - 1][vial_list[i] - 'A']);
  }
  // add the valve blocks that always open together with the control odors.
  result.push_back(V_BLEND0);
  result.push_back(V_CONTROL);
  return result;
}

/// Converts an alias of type "BlendXY_xV_..." to a vector of open valves
/// (e.g. Blend12_3V_BC-A or Blend123_12V_ABCD-ABCD-ABCD)
vector<int> valve_alias::parse_blend(const string name){
  
  // minimal length of alias (e.g. Blend12_2V_A-B)
  if (name.length() < 14) {
    cerr << "Invalid blend alias: " << name << endl;
    return vector<int>();
  }

  // odour numbers
  int odour1 = ctoi(name[5]);
  int odour2 = ctoi(name[6]);
  int odour3 = ctoi(name[7]);

  if (odour3 == 0 && name[7] != '_') {
    cerr << "Invalid blend alias: " << name << endl;
    return vector<int>();
  }

  string str;

  if (odour3 == 0) {  // two odour blend
    if (odour1 < 1 || odour1 > 3 || odour2 < 1 || odour2 > 3 || name[7] != '_') {
      cerr << "Invalid odours in blend alias: " << name << endl;
      return vector<int>();
    }
    if (odour1 >= odour2) {
      cerr << "Invalid blend alias: " << name << endl;
      return vector<int>();
    }
    str = name.substr(8);
  } else {            // three odour blend (thus must be odours 1 2 3)
    if (odour1 !=1 || odour2 != 2 || odour3 != 3) {
      cerr << "Invalid odours in blend alias: " << name << endl;
      return vector<int>();
    }
    if (name[8] != '_') {
      cerr << "Invalid blend alias: " << name << endl;
      return vector<int>();
    }
    str = name.substr(9);
  }

  // extract vial count
  unsigned int vials = atoi(str.c_str());
  size_t pos = str.find('_');
  if (pos == string::npos) {
    cerr << "Invalid blend alias: " << name << endl;
    return vector<int>();
  }

  // complete vial list
  string vial_list = str.substr(pos + 1);

  // list must contain a '-' to have (at least) two sublists
  pos = vial_list.find('-');
  if (pos == string::npos) {
    cerr << "Invalid blend alias: " << name << endl;
    return vector<int>();
  }

  // split the vial list
  string vial_list_1 = vial_list.substr(0, pos);
  string vial_list_2 = vial_list.substr(pos + 1);

  // if we have three odors, vial_list_2 has to be split again
  string vial_list_3;
  if (odour3 != 0) {
    pos = vial_list_2.find('-');
    if (pos == string::npos) {
      cerr << "Invalid blend alias: " << name << endl;
      return vector<int>();
    }
    vial_list_3 = vial_list_2.substr(pos + 1);
    vial_list_2 = vial_list_2.substr(0, pos);
  }

  // check the vial lists and total vial count
  if (odour3 == 0) {   // blend with two odours
    if (vials != (vial_list_1.length() + vial_list_2.length()) || !validate_vials(vial_list_1) || !validate_vials(vial_list_2)) {
      cerr << "Invalid blend alias: " << name << endl;
      return vector<int>();
    }
  } else {             // blend with three odours
    if (vials != (vial_list_1.length() + vial_list_2.length() + vial_list_3.length()) || !validate_vials(vial_list_1) || !validate_vials(vial_list_2) || !validate_vials(vial_list_3)) {
      cerr << "Invalid blend alias: " << name << endl;
      return vector<int>();
    }
  }

  // base open valves for selected blend
  vector<int> result;
  // manifold waste valves used in NO position
  
  /*if (odour3 == 0) {   // blend with two odours
    if (odour1 == 1 && odour2 == 2) {
      result = make_vector<int>() << V_BLEND12 << V_ODOR1_WASTE << V_ODOR2_WASTE << V_WASTE1 << V_WASTE2 << V_CARRIER << V_BOOST; // ephys diff
    } else if (odour1 == 1 && odour2 == 3) {
      result = make_vector<int>() << V_BLEND13 << V_ODOR1_WASTE << V_ODOR3_WASTE << V_WASTE1 << V_WASTE3 << V_CARRIER << V_BOOST; // ephys diff
    } else if (odour1 == 2 && odour2 == 3) {
      result = make_vector<int>() << V_BLEND23 << V_ODOR2_WASTE << V_ODOR3_WASTE << V_WASTE2 << V_WASTE3 << V_CARRIER << V_BOOST; // ephys diff
    }
  } else {             // blend with three odours
    result = make_vector<int>() << V_BLEND123 << V_ODOR1_WASTE << V_ODOR2_WASTE << V_ODOR3_WASTE << V_WASTE1 << V_WASTE2 << V_WASTE3 << V_CARRIER << V_BOOST; // ephys diff
  }*/

  // manifold waste valves used in NC position
  if (odour3 == 0) {   // blend with two odours
    if (odour1 == 1 && odour2 == 2) {
      result = make_vector<int>() << V_BLEND12 << V_ODOR3_WASTE <<  V_WASTE1 << V_WASTE2 << V_CARRIER;
    } else if (odour1 == 1 && odour2 == 3) {
      result = make_vector<int>() << V_BLEND13 << V_ODOR2_WASTE <<  V_WASTE1 << V_WASTE3 << V_CARRIER;
    } else if (odour1 == 2 && odour2 == 3) {
      result = make_vector<int>() << V_BLEND23 << V_ODOR1_WASTE <<  V_WASTE2 << V_WASTE3 << V_CARRIER;
    }
  } else {             // blend with three odours
    result = make_vector<int>() << V_BLEND123 << V_WASTE1 <<  V_WASTE2 << V_WASTE3 << V_CARRIER;
  }

  // add valves corresponding to vials for 1st odour
  for (unsigned int i(0); i < vial_list_1.length(); i++) {
    result.push_back(vial_valves[odour1 - 1][vial_list_1[i] - 'A']);
  }

  // add valves corresponding to vials for 2nd odour
  for (unsigned int i(0); i < vial_list_2.length(); i++) {
    result.push_back(vial_valves[odour2 - 1][vial_list_2[i] - 'A']);
  }

  // add valves corresponding to the 3rd odour (if present)
  if (odour3 != 0) {
    for (unsigned int i(0); i < vial_list_3.length(); i++) {
      result.push_back(vial_valves[odour3 - 1][vial_list_3[i] - 'A']);
    }
  }

  return result;
}

/// Converts a valve alias (e.g. Odour2_2V_AD or Blend123_12V_ABCD-ABCD-ABCD)
/// to a vector with the list of valves that have to be opened
vector<int> valve_alias::parse_alias(const string name)
{
  // TESTING: for TEST ONLY
  if (name == "Test_food"){
    return make_vector<int>() << 0 << 11 << 13 << 22 << 23;
  }
  if (name == "Test_cVA"){
    return make_vector<int>() << 3 << 5 << 8 << 20 << 23;
  }
  if (name == "Test_carrier"){
    return make_vector<int>() << 0 << 8 << 21;
  }
  if (name == "Test_blend"){
    return make_vector<int>() << 3 << 5 << 11 << 13 << 19 << 23;
  }
  // END TEST BLOCK
  
  if (name == "Carrier") {
    /*return make_vector<int>(); */ // ephys diff  (NO manifold waste valves)
    return make_vector<int>() << V_ODOR1_WASTE << V_ODOR2_WASTE << V_ODOR3_WASTE << V_BOOST;  // NC manifold waste valves
  } else if (name.substr(0, 5) == "Odour") {
    return parse_odour(name);
  } else if (name.substr(0, 5) == "Blend" && (name.substr(7, 1) == "_" || name.substr(8, 1) == "_")) {
    return parse_blend(name);
  } else if (name.substr(0, 7) == "Control") {
    return parse_control(name);
  } else {
    cerr << "Unrecognised alias: " << name << "." << endl;
    return vector<int>();
  }
}
