# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
# set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The managed `lvgl__lvgl` component includes "lv_conf.h" (via
# LV_CONF_INCLUDE_SIMPLE) but does not list `main` in its REQUIRES, so on a clean
# build it fails to locate main/lv_conf.h. Add main/ to the global include path
# so the configuration header is discoverable by every component. Quoted
# includes still resolve a component's own headers first, so this does not
# shadow other components' headers.
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_CURRENT_LIST_DIR}/main" APPEND)

project(esp-miner)

