set(LOG_COMPONENT_NAME log_component)

add_library(${LOG_COMPONENT_NAME} STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/log.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rotating-file.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/log.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/rotating-file.hh
)

target_include_directories(${LOG_COMPONENT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries(${LOG_COMPONENT_NAME} PRIVATE
    Qt6::Core
)
