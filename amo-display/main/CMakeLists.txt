# 只声明此刻真实存在的东西。agent_ui 到接 UI 时才进 REQUIRES。
idf_component_register(
    SRCS "main.c" "tiny_parse.c" "hosts.c"
    INCLUDE_DIRS "."
    REQUIRES tds3_board
)
