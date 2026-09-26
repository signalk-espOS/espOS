# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# espos_power: a deep-sleep duty cycle (include/espos_power.h). Optional:
# espos_start() starts it when the project builds it.
#
# The decision of whether to sleep is pure C (power_policy.c, host-tested in
# test/host/espos_power_test); the platform is behind power_port.h, with
# port_sim.c on the linux target, where nothing sleeps.
idf_build_get_property(target IDF_TARGET)
set(srcs src/power_policy.c src/espos_power.c)
set(priv_reqs espos_config espos_httpd espos_net espressif__cjson)
if(target STREQUAL "linux")
    list(APPEND srcs src/port_sim.c)
else()
    list(APPEND srcs src/port_idf.c)
    # esp_hw_support: esp_sleep.h and the RTC memory attribute; esp_system:
    # esp_reset_reason(); app_update: whether the running image is confirmed.
    list(APPEND priv_reqs esp_hw_support esp_system app_update esp_timer)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    PRIV_REQUIRES ${priv_reqs})

# With SignalK in the build the cycle waits for the stream and flushes it
# before sleeping; without it, the network being up is what a wake waits for.
# Namespace-tolerant component checks (espOS #138). Its own copy, because a
# component installed from the registry cannot reach a sibling's directory,
# and this component does not require espos_core. The file guards against
# being included twice.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/espos_components.cmake")

espos_optional_requires(PRIVATE espos_sk espos_ota)
espos_has_component(_have_sk espos_sk)
if(_have_sk)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOS_POWER_HAVE_SK=1)
endif()
# With espos_ota in the build, an update in progress keeps the device awake:
# a wake lasts seconds, and a download cut off by every sleep never finishes.
espos_has_component(_have_ota espos_ota)
if(_have_ota)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOS_POWER_HAVE_OTA=1)
endif()

espos_config_add_descriptor(config/power.json)
