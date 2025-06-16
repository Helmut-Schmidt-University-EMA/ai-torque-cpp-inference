cmake_minimum_required(VERSION 3.15)
project(daqmodels CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set the path to your local libtorch installation
set(Torch_DIR "${CMAKE_SOURCE_DIR}/libs/libtorch/share/cmake/Torch")
find_package(Torch REQUIRED)

# Build the daqmodels library
add_library(daqmodels src/daqmodels.cpp)
target_link_libraries(daqmodels PRIVATE "${TORCH_LIBRARIES}")
target_include_directories(daqmodels PUBLIC include "${TORCH_INCLUDE_DIRS}")

set_target_properties(daqmodels PROPERTIES PUBLIC_HEADER "include/daqmodels.h")
install(TARGETS daqmodels)

# Build the executable for running/testing
add_executable(daqmodels_exec src/daqmodels.cpp)
target_link_libraries(daqmodels_exec PRIVATE "${TORCH_LIBRARIES}")
target_include_directories(daqmodels_exec PUBLIC include "${TORCH_INCLUDE_DIRS}")

# Optional: install the executable
install(TARGETS daqmodels_exec)
