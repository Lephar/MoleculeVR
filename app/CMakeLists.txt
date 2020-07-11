cmake_minimum_required(VERSION 3.10.2)
set(CMAKE_CXX_STANDARD 17)

add_library(main SHARED src/main/cpp/main.cpp)
target_include_directories(main PRIVATE src/main/include)

add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")
target_link_libraries(main android native_app_glue log vulkan)