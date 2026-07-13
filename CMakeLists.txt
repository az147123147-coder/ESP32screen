# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# 添加额外组件目录
set(EXTRA_COMPONENT_DIRS components/Middlewares)
add_compile_options(-fdiagnostics-color=always)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(synthesis)

if(CONFIG_TINYUSB_MSC_ENABLED)
    idf_component_get_property(esp_tinyusb_lib espressif__esp_tinyusb COMPONENT_LIB)
    idf_component_get_property(esp_tinyusb_dir espressif__esp_tinyusb COMPONENT_DIR)
    get_target_property(esp_tinyusb_sources ${esp_tinyusb_lib} SOURCES)
    set(esp_tinyusb_msc_source "${esp_tinyusb_dir}/tusb_msc_storage.c")
    list(FIND esp_tinyusb_sources "${esp_tinyusb_msc_source}" esp_tinyusb_msc_index)
    if(esp_tinyusb_msc_index EQUAL -1)
        message(FATAL_ERROR "esp_tinyusb default MSC source was not found")
    endif()
    list(REMOVE_ITEM esp_tinyusb_sources "${esp_tinyusb_msc_source}")
    set_property(TARGET ${esp_tinyusb_lib} PROPERTY SOURCES ${esp_tinyusb_sources})
endif()
