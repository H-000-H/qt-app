set(CHART_COMPONENT_NAME chart_component)

set(CHART_COMPONENT_INC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/
)

set(CHART_COMPONENT_HEADS
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/data-view.hh
)

# 纯模板头文件，没有 .cc，所以是 INTERFACE 库：不产出 .a，只向使用者传递头文件路径和 Qt 依赖
add_library(${CHART_COMPONENT_NAME} INTERFACE)

target_include_directories(${CHART_COMPONENT_NAME} INTERFACE
    ${CHART_COMPONENT_INC}
)

target_sources(${CHART_COMPONENT_NAME} INTERFACE
    ${CHART_COMPONENT_HEADS}
)

target_link_libraries(${CHART_COMPONENT_NAME} INTERFACE
    Qt6::Core
    Qt6::Widgets
)
