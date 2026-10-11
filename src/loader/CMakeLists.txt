add_executable(hogrun main.cpp)
target_link_libraries(hogrun PRIVATE hogcommon)
set_target_properties(hogrun PROPERTIES OUTPUT_NAME "hogrun")
