#pragma once

#include <vector>
#include <string>


#ifdef _WIN32
  #define DAQMODELS_EXPORT __declspec(dllexport)
#else
  #define DAQMODELS_EXPORT
#endif

DAQMODELS_EXPORT void daqmodels();
DAQMODELS_EXPORT void daqmodels_print_vector(const std::vector<std::string> &strings);
