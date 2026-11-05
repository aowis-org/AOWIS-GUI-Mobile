#pragma once

#include <string>
#include <vector>

namespace AowisMobileProbe
{
    // Debug-text-only scene. This tests Qt Quick -> bgfx input propagation,
    // not shader geometry throughput or the eventual hydraulic map renderer.
    std::vector<std::string> createScene(int columns, int rows,
                                         double zoom, double panPixelsX,
                                         double panPixelsY);
}
