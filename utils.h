/*
 *  @file utils.h
 *  \brief Various fonctions that are useful for many programs:
 1. monotonic time
 2. string manipulation
 3. template for creating a vector and filling in elements directly without a loop
 *
 *
 */

#ifndef __UTILS_H
#define __UTILS_H

#include <string>
#include <sstream>
#include <vector>
#include <sys/time.h> 
#include <errno.h>      // for ETIMEDOUT


// Mac and Linux compatible function for monotonic time
double time_monotonic();
// Mac and Linux compatible function for real time
double time_real();


/// \brief Converts a character (e.g. '1') to an integer (e.g. 1)
/// \return the integer value of the character, or 0 if it is not a digit
int ctoi(char c);

// template to convert any type to a string
template <class T>
inline std::string to_string (const T& t){
  std::stringstream ss;
	ss << t;
	return ss.str();
}

// template to convert any type to a string with high precision (HP applies only for doubles, floats and ints
template <class T>
inline std::string to_stringHP (const T& t, int precision){
  std::stringstream ss;
  ss.precision(precision);
  ss << std::fixed << t;
  return ss.str();
}

// template to convert any type to a string with high precision (HP applies only for doubles, floats and ints
template <class T>
inline std::string to_stringHP (const T& t, int precision, int width, char fill){
  std::stringstream ss;
  ss.fill(fill);
  ss.width(width);
  ss.precision(precision);
  ss << std::fixed << t;
  return ss.str();
}


/// template class to easily initialize a vector
/// usage: make_vector<type>() << elem1 << elem2 [<< elem3 [...]]
template <typename T> class make_vector {
  
public:
  typedef make_vector<T> my_type;
  
  my_type& operator<< (const T& val) {
    data_.push_back(val);
    return *this;
  }
  
  operator std::vector<T>() const {
    return data_;
  }
  
private:
  std::vector<T> data_;
};

// template to easliy find elements in a vector
template <typename T> static bool vector_contains (const std::vector <T>& v, const T el){
  for (unsigned int i(0); i < v.size(); i++){
   if (v[i] == el)
      return true;
  }
  return false;
}

std::string UNIX_to_datetime(double timestamp);

std::string UNIX_to_datetime(double timestamp, std::string format);

void get_absolute_timeout(timespec& ts, const unsigned int delay);

/// drop root privileges
void drop_privileges();

/// set realtime scheduling on the process (need root privileges!)
bool set_realtime();

// =============================================================================
// chops up line of space-separated words into table of strings, returns number of strings found
int chop_line(std::string s, std::vector <std::string>& word_table);


#endif // __UTILS_H__
