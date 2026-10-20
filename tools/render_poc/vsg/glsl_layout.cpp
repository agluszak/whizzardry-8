// Writes the GLSL definitions of the shading-block layout for offline compilation.

#include <fstream>

#include "shading_block.h"

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    std::ofstream(argv[1]) << render_poc::glslLayoutDefinitions();
    return 0;
}
