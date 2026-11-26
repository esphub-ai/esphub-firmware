// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "sdkconfig.h"

extern "C" void paper_color_factory_main();

extern "C" void app_main()
{
    // The factory settings select the owner after the guide and on every boot.
    eh_config_t config = EH_CONFIG_DEFAULT();
#if !CONFIG_EH_NET_OPENETH
    config.manage_network = false;
#endif
    ESP_ERROR_CHECK(eh_agent_start_with_config(&config));
    paper_color_factory_main();
}
