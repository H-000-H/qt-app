set(CONSOLE_NAME console_component)
set(CONSOLE_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/serial-port.cc
)

set(CONSOLE_INC
  ${CMAKE_CURRENT_SOURCE_DIR}/inc/
)

set(CONSOLE_HEADS
  ${CONSOLE_INC}/serial-port.hh
)

add_library(${CONSOLE_NAME} STATIC
  ${CONSOLE_INC}
  ${CONSOLE_SRC}
)

set_target_properties(${CONSOLE_NAME} PROPERTIES
  AUTOMOC ON
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(${CONSOLE_NAME} PUBLIC
  ${CONSOLE_INC}
)

target_link_libraries(${CONSOLE_NAME} PRIVATE
  Qt6::Core
  Qt6::SerialPort
)
