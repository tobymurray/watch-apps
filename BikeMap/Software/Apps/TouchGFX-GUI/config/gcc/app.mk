# Out of the SDK tree there is no fixed number of ".." back to it. An in-tree
# app commits ../../../../../../ThirdParty/touchgfx, which counts levels from
# Examples/Apps/<App>/Software/Apps/TouchGFX-GUI and means nothing from here.
# Same treatment as MapManager's, for the same reason.
touchgfx_path := $(UNA_SDK)/ThirdParty/touchgfx
