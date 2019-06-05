cmake_minimum_required(VERSION 3.8)
project(FireMLT
  VERSION 3.5.0
  LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(SDL2 REQUIRED)
find_package(ArrayFire REQUIRED)

include_directories(${SDL2_INCLUDE_DIRS})
find_package(FFmpeg COMPONENTS AVCODEC AVFORMAT AVUTIL AVDEVICE AVFILTER SWSCALE SWRESAMPLE REQUIRED)

add_executable(player main.cpp)
list(APPEND libs FFmpeg::FFmpeg ${SDL2_LIBRARIES})

#if(ArrayFire_CUDA_FOUND)
#  list(APPEND libs ArrayFire::afcuda)
#elseif(ArrayFire_OpenCL_FOUND)
#  list(APPEND libs ArrayFire::afopencl)
#elseif(ArrayFire_CPU_FOUND)
  list(APPEND libs ArrayFire::afcpu)
#endif()

target_link_libraries(player PUBLIC ${libs})
