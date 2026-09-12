// stb_impl_shim.cpp — provides the stb_image_write implementation for the
// standalone gltf_bridge round-trip test only. In the real `ruby` build this
// symbol comes from batch_converter.cpp / imgui_draw.cpp; gltf_export.cpp only
// includes the header (declarations). This shim lets the test link without
// pulling in the whole app.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
