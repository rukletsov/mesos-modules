
# Add a test hook dynamic library.
add_library(mesos-test-hook SHARED
  ${CMAKE_CURRENT_SOURCE_DIR}/module_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test-hook.json.in
)

target_link_libraries(mesos-test-hook
  ${Mesos_LIBRARIES}
)

# CONFIGURE_FILE would be a nicer option, but unfortunately it cannot
# generator expressions like $<TARGET_FILE_DIR:mesos-hook-test>.
add_custom_command(TARGET mesos-test-hook
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_CURRENT_SOURCE_DIR}/test-hook.json.in"
    "$<TARGET_FILE_DIR:mesos-test-hook>/test-hook.json"
  COMMENT "Copying test-hook.json data to the runtime directory"
)

add_custom_command(TARGET mesos-test-hook
  POST_BUILD
  COMMAND sed -i ''
    's|HOOK_BINARY|'$<TARGET_FILE:mesos-test-hook>'|g'
    "$<TARGET_FILE_DIR:mesos-test-hook>/test-hook.json"
  COMMENT "Replacing HOOK_MODULE with the actual hook path in module.json"
)
