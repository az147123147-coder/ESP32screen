idf_component_register(
    SRC_DIRS 
        "."
        "APP"
        "MANAGE"
        "IMAGE"
    EXCLUDE_SRCS
        "APP/lv_camera_ui.c"
        "IMAGE/usb.c"
    INCLUDE_DIRS 
        "."
        "APP"
        "MANAGE"
        "IMAGE"
    EMBED_FILES "canon.pcm")
