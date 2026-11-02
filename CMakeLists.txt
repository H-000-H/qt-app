cmake_minimum_required(VERSION 3.22)
set(PROJECT_NAME app)
project(${PROJECT_NAME} LANGUAGES CXX C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_AUTOMOC ON)
set(QT_ADDITIONAL_PACKAGES_PREFIX_PATH "$ENV{HOME}/.local/qtmqtt"
  CACHE STRING "Qt 附加模块安装前缀")
find_package(Qt6 REQUIRED COMPONENTS
  Core
  Widgets
  Charts
  Network
  Sql
  SerialPort
  Mqtt
)

add_subdirectory(console)
add_subdirectory(file-write)
add_subdirectory(net)

set(SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/main/main.cc
)

set(INC
  ${CMAKE_CURRENT_SOURCE_DIR}/main/
)

set(HEADS

)

add_executable(${PROJECT_NAME}
  ${SRC}
  ${HEADS}
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${INC}
)
target_link_libraries(${PROJECT_NAME} PRIVATE
  file_component
  net_component
  console_component
  Qt6::Core
  Qt6::Widgets
  Qt6::Charts
  Qt6::Network
  Qt6::Sql
  Qt6::SerialPort
  Qt6::Mqtt
)
