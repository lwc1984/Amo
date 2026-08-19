# agent_ui 提供 view.h（显示模型）与 ui.h。依赖方向单向：
# main 产生视图 -> agent_ui 消费视图。反过来会形成组件循环。
idf_component_register(
    SRCS "main.c" "tiny_parse.c" "hosts.c" "net_wifi.c" "discovery.c"
         "pairing.c" "hosts_nvs.c" "buttons.c" "poller.c"
    INCLUDE_DIRS "."
    REQUIRES tds3_board agent_ui nvs_flash esp_wifi mdns esp_http_client driver esp_timer
)
