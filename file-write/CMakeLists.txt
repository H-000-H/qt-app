set(FILE_WRITE_COMPONENT_NAME file_component)

set(FILE_WRITE_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/csv/src/csv-export.cc
)

set(FILE_WRITE_INC
  ${CMAKE_CURRENT_SOURCE_DIR}/csv/inc/
)

set(FILE_WRITE_HEADS
  ${FILE_WRITE_INC}/csv-export.hh
)

# 创建目标；AUTOMOC 由顶层继承
add_library(${FILE_WRITE_COMPONENT_NAME} STATIC
  ${FILE_WRITE_SRC}
  ${FILE_WRITE_HEADS}
)

set_target_properties(${FILE_WRITE_COMPONENT_NAME} PROPERTIES
  AUTOMOC ON
  POSITION_INDEPENDENT_CODE ON
)

# PUBLIC: 链接该库的目标也能找到 inc/ 下的头文件
target_include_directories(${FILE_WRITE_COMPONENT_NAME} PUBLIC
  ${FILE_WRITE_INC}
)

# Qt6::Core 已由顶层 find_package 提供，此处直接链接
target_link_libraries(${FILE_WRITE_COMPONENT_NAME} PRIVATE
  Qt6::Core
)
