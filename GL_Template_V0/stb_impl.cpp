// stb_impl.cpp
// Compile stb implementations exactly once in the whole program.

#define STB_IMAGE_IMPLEMENTATION
#include "../External_libs/stb/image/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../External_libs/stb/image/stb_image_write.h"
