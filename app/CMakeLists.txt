target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_OS_LAYER app PRIVATE src/behavior_os_layer.c)

if(CONFIG_CUSTOM_BATTERY_TYPIST)
  zephyr_library_named(custom_battery_typist)
  zephyr_library_sources(src/behavior_battery_type.c)
  zephyr_library_include_directories(${ZMK_DIR}/include)
endif()
