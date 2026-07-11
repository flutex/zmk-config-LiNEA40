target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_OS_LAYER app PRIVATE src/behavior_os_layer.c)

if(CONFIG_CUSTOM_BATTERY_TYPIST)
  zephyr_library_named(custom_battery_typist)
  zephyr_library_sources(src/behavior_battery_type.c)
  zephyr_library_include_directories(${ZMK_DIR}/include)
endif()
target_sources_ifdef(CONFIG_ZMK_LAYER_STATUS_GATT app PRIVATE src/layer_status_gatt.c)

if(CONFIG_ZMK_USB_FP_CAPTURE)
  target_sources(app PRIVATE src/usb_fp_capture.c src/behavior_usb_fp_type.c)
  target_include_directories(app PRIVATE include)
  zephyr_link_libraries("-Wl,--wrap=usb_dc_ep_read")
endif()
