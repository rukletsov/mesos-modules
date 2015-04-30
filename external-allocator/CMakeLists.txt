
# We re-use the built-in Mesos allocator.
set(3rdparty_hdrs
  ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/constants.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mesos/allocator.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mesos/hierarchical.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/sorter/sorter.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/sorter/drf/sorter.hpp
)

set(3rdparty_srcs
  ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/constants.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/sorter/drf/sorter.cpp
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)

# Add an external allocator dynamic library.
add_library(mesos-external-allocator SHARED
  ${CMAKE_CURRENT_SOURCE_DIR}/module_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/external-allocator.json.in
  ${3rdparty_hdrs}
  ${3rdparty_srcs}
)

target_link_libraries(mesos-external-allocator
  ${Mesos_LIBRARIES}
)

# CONFIGURE_FILE would be a nicer option, but unfortunately it cannot
# generator expressions like $<TARGET_FILE_DIR:mesos-external-allocator>.
add_custom_command(TARGET mesos-external-allocator
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_CURRENT_SOURCE_DIR}/external-allocator.json.in"
    "$<TARGET_FILE_DIR:mesos-external-allocator>/external-allocator.json"
  COMMENT "Copying external-allocator.json data to the runtime directory"
)

add_custom_command(TARGET mesos-external-allocator
  POST_BUILD
  COMMAND sed -i ''
    's|HOOK_BINARY|'$<TARGET_FILE:mesos-external-allocator>'|g'
    "$<TARGET_FILE_DIR:mesos-external-allocator>/external-allocator.json"
  COMMENT "Replacing HOOK_MODULE with the actual hook path in external-allocator.json"
)
