#include "../src/probe_scene.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    void require(bool condition, const char *message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    const std::vector<std::string> initial = AowisMobileProbe::createScene(55, 23, 1.0, 0.0, 0.0);
    const std::vector<std::string> panned = AowisMobileProbe::createScene(55, 23, 1.0, 80.0, -40.0);
    const std::vector<std::string> zoomed = AowisMobileProbe::createScene(55, 23, 1.5, 0.0, 0.0);
    require(initial.size() == 23, "scene row count");
    for (const std::string &row : initial)
        require(row.size() == 55, "scene column count");
    require(initial != panned, "pan changes rendered scene");
    require(initial != zoomed, "zoom changes rendered scene");
    require(initial != AowisMobileProbe::createScene(55, 23, 1.0, 16.0, 0.0), "small pan is visible");
    const std::vector<std::string> minimum = AowisMobileProbe::createScene(0, 0, 0.0, 0.0, 0.0);
    require(minimum.size() == 6, "minimum rows are clamped");
    require(minimum[0].size() == 12, "minimum columns are clamped");
    return 0;
}
