cmake_minimum_required(VERSION 3.8)
project(FireMLT
  VERSION 3.5.0
  LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(SDL2 REQUIRED)
find_package(ArrayFire REQUIRED)

find_package(FFmpeg COMPONENTS AVCODEC AVFORMAT AVUTIL AVDEVICE AVFILTER SWSCALE SWRESAMPLE REQUIRED)

add_executable(player main.cpp)
list(APPEND libs FFmpeg::FFmpeg swscale SDL2::SDL2)

if(ArrayFire_CUDA_FOUND)
  list(APPEND libs ArrayFire::afcuda)
elseif(ArrayFire_OpenCL_FOUND)
  list(APPEND libs ArrayFire::afopencl)
elseif(ArrayFire_CPU_FOUND)
  list(APPEND libs ArrayFire::afcpu)
endif()

target_link_libraries(player PUBLIC ${libs})
