cmake_minimum_required(VERSION 3.15)
project(daqmodels CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set the path to your local libtorch installation
set(Torch_DIR "${CMAKE_SOURCE_DIR}/libs/libtorch/share/cmake/Torch")
find_package(Torch REQUIRED)

# Find and require OpenSSL
find_package(OpenSSL REQUIRED)

# Build the daqmodels library
add_library(daqmodels src/daqmodels.cpp)
target_link_libraries(daqmodels PRIVATE "${TORCH_LIBRARIES}" OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(daqmodels PUBLIC include "${TORCH_INCLUDE_DIRS}")

set_target_properties(daqmodels PROPERTIES PUBLIC_HEADER "include/daqmodels.h")
install(TARGETS daqmodels)

# Build the executable for running/testing
add_executable(daqmodels_exec src/daqmodels.cpp)
target_link_libraries(daqmodels_exec PRIVATE "${TORCH_LIBRARIES}" OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(daqmodels_exec PUBLIC include "${TORCH_INCLUDE_DIRS}")

if(WIN32)
file(GLOB TORCH_DLLS "${TORCH_DIR}/../../../bin/*.dll")
foreach(DLL ${TORCH_DLLS})
add_custom_command(TARGET daqmodels_exec POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
  ${DLL}
  $<TARGET_FILE_DIR:daqmodels_exec>)
endforeach()
endif()

# Optional: install the executable
install(TARGETS daqmodels_exec)
