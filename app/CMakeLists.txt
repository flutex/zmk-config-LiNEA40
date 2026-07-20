# linea_jis.h (os_layer ⇔ layer_status_gatt の共有API) を全ソースから解決できるようにする。
# USBブロック内の include 追加と重複しても CMake が重複除去するので無害。
target_include_directories(app PRIVATE include)

target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_OS_LAYER app PRIVATE src/behavior_os_layer.c)

if(CONFIG_CUSTOM_BATTERY_TYPIST)
  zephyr_library_named(custom_battery_typist)
  zephyr_library_sources(src/behavior_battery_type.c)
  zephyr_library_include_directories(${ZMK_DIR}/include)
endif()
target_sources_ifdef(CONFIG_ZMK_LAYER_STATUS_GATT app PRIVATE src/layer_status_gatt.c)

# usb_host_os.c owns the SINGLE linker --wrap on usb_dc_ep_read. It is compiled
# whenever the OS detector OR the (debug) fingerprint capture is enabled, so the
# wrap symbol is defined exactly once even when both features are on.
if(CONFIG_ZMK_USB_HOST_OS_DETECTION OR CONFIG_ZMK_USB_FP_CAPTURE)
  target_sources(app PRIVATE src/usb_host_os.c)
  target_include_directories(app PRIVATE include)
  zephyr_link_libraries("-Wl,--wrap=usb_dc_ep_read")
endif()

# OS-detection: in-module custom event + the arbitration/GATT consumers already
# above pick up the guarded code paths. The event impl links its raise_/as_.
if(CONFIG_ZMK_USB_HOST_OS_DETECTION)
  target_sources(app PRIVATE src/events/usb_host_os_changed.c)
endif()

# Debug/measurement only: fingerprint ring + &usb_fp_type readout behavior.
# The wrap is NOT added here (usb_host_os.c above owns it). The include dir is
# also NOT re-added here: FP_CAPTURE implies the (HOST_OS OR FP_CAPTURE) block
# above already ran and added `include`, so repeating it would be redundant.
if(CONFIG_ZMK_USB_FP_CAPTURE)
  target_sources(app PRIVATE src/usb_fp_capture.c src/behavior_usb_fp_type.c)
endif()
