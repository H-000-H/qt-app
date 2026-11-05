/**
 * @brief 占位编译单元：让 ui/chart/ 出现在 compile_commands.json 里。
 *        chart_component 只有头文件，没有这个 TU 时 clangd 只能从别的目录的
 *        编译条目推断参数，拿不到 Qt6::Widgets 的头文件路径。
 */
#include "data-view.hh"
