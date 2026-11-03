set(APP_COMPONENT_NAME app_component)

set(APP_COMPONENT_INC
    ${CMAKE_CURRENT_SOURCE_DIR}/home/
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/inc/
)

set(APP_COMPONENT_HEADS
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/inc/data-test.hh
)

# 目前只有头文件，入口 main.cc 由顶层的可执行目标直接编译
add_library(${APP_COMPONENT_NAME} INTERFACE)

target_include_directories(${APP_COMPONENT_NAME} INTERFACE
    ${APP_COMPONENT_INC}
)

target_sources(${APP_COMPONENT_NAME} INTERFACE
    ${APP_COMPONENT_HEADS}
)

target_link_libraries(${APP_COMPONENT_NAME} INTERFACE
    chart_component
    Qt6::Core
    Qt6::Widgets
)
