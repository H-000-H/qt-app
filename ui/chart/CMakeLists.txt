set(CHART_COMPONENT_NAME chart_component)

set(CHART_COMPONENT_INC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/
)

set(CHART_COMPONENT_HEADS
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/data-view.hh
)

set(CHART_COMPONENT_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/data-view.cc
)

# 模板实现全在头文件里；src/data-view.cc 只负责把本目录编译条目送进 compile_commands.json
add_library(${CHART_COMPONENT_NAME} STATIC
    ${CHART_COMPONENT_SRC}
    ${CHART_COMPONENT_HEADS}
)

set_target_properties(${CHART_COMPONENT_NAME} PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(${CHART_COMPONENT_NAME} PUBLIC
    ${CHART_COMPONENT_INC}
)

target_link_libraries(${CHART_COMPONENT_NAME} PUBLIC
    Qt6::Core
    Qt6::Widgets
)
