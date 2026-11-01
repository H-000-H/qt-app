set(APP_NAME app_component)

set(APP_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

set(APP_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

set(APP_HEADS
  ${APP_SRC}/
)

# 创建目标；AUTOMOC 由顶层继承
add_library(${APP_NAME} STATIC
  ${APP_SRC}
  ${APP_HEADS}
)

set_target_properties(${APP_NAME} PROPERTIES
  AUTOMOC ON
  POSITION_INDEPENDENT_CODE ON
)

# PUBLIC: 链接该库的目标也能找到 inc/ 下的头文件
target_include_directories(${APP_NAME} PUBLIC
  ${APP_SRC}
)

# Qt6::Core 已由顶层 find_package 提供，此处直接链接
target_link_libraries(${APP_NAME} PRIVATE
  Qt6::Core
)
