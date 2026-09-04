idf_component_register(
    SRCS "main.c"
         "showcase_scenes.c"
         "usage_link.c"
         "usage_model.c"
         "font_digits_44.c"
         "ui_pixel_math.c"
         "ui_glass.c"
         "ui_glass_optics.c"
         "ui_glass_theme.c"
         "ui_glass_focus.c"
         "ui_glass_motion.c"
         "ui_glass_quality.c"
         "ui_glass_widgets.c"
         "ui_glass_runtime.c"
         "liquid_glass_motion.c"
         "liquid_glass_compositor_core.c"
         "ui_glass_compositor.c"
         "fap_screenshot.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_driver_usb_serial_jtag esp_hw_support esp_timer lvgl
             nvs_flash
    EMBED_FILES "../assets/images/liquid_glass_wallpaper.rgb565"
)
