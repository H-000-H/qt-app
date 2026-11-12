set(NET_COMPONENT_NAME net_component)

set(NET_COMPONENT_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/mqtt/src/mqtt_client.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/src/tcp_client.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/src/udp-client.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/http/src/http-client.cc
)

set(NET_COMPONENT_INC
    ${CMAKE_CURRENT_SOURCE_DIR}/
    ${CMAKE_CURRENT_SOURCE_DIR}/mqtt/inc/
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/inc/
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/inc/
    ${CMAKE_CURRENT_SOURCE_DIR}/http/inc/
)

set(NET_COMPONENT_HEADS
    ${CMAKE_CURRENT_SOURCE_DIR}/net-base.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/net-error.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/mqtt/inc/mqtt_client.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/inc/tcp_client.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/inc/udp-client.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/http/inc/http-client.hh
)

# 创建目标；AUTOMOC 由顶层继承
add_library(${NET_COMPONENT_NAME} STATIC
    ${NET_COMPONENT_SRC}
    ${NET_COMPONENT_HEADS}
)

set_target_properties(${NET_COMPONENT_NAME} PROPERTIES
  AUTOMOC ON
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(${NET_COMPONENT_NAME} PUBLIC
    ${NET_COMPONENT_INC}
)

target_link_libraries(${NET_COMPONENT_NAME} PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Mqtt
)
