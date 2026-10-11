add_library(hogcommon STATIC profile.cpp inject.cpp)
target_include_directories(hogcommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
