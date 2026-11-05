#include "probe_scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace
{
    struct GridPoint
    {
        int x;
        int y;
    };

    struct WorldPoint
    {
        double x;
        double y;
    };

    void plot(std::vector<std::string> &scene, int x, int y, char glyph)
    {
        if (y >= 0 && y < static_cast<int>(scene.size()) &&
            x >= 0 && x < static_cast<int>(scene[static_cast<size_t>(y)].size()))
        {
            scene[static_cast<size_t>(y)][static_cast<size_t>(x)] = glyph;
        }
    }

    void line(std::vector<std::string> &scene, GridPoint first, GridPoint last)
    {
        const int deltaX = std::abs(last.x - first.x);
        const int deltaY = -std::abs(last.y - first.y);
        const int stepX = first.x < last.x ? 1 : -1;
        const int stepY = first.y < last.y ? 1 : -1;
        int error = deltaX + deltaY;

        // Never let a large offscreen pan create an unbounded debug workload.
        for (int step = 0; step < 4096; ++step)
        {
            plot(scene, first.x, first.y, '*');
            if (first.x == last.x && first.y == last.y)
                break;
            const int doubledError = 2 * error;
            if (doubledError >= deltaY)
            {
                error += deltaY;
                first.x += stepX;
            }
            if (doubledError <= deltaX)
            {
                error += deltaX;
                first.y += stepY;
            }
        }
    }
}

std::vector<std::string> AowisMobileProbe::createScene(
    int columns, int rows, double zoom, double panPixelsX, double panPixelsY)
{
    columns = std::clamp(columns, 12, 120);
    rows = std::clamp(rows, 6, 48);
    zoom = std::clamp(zoom, 0.3, 3.0);
    panPixelsX = std::clamp(panPixelsX, -4096.0, 4096.0);
    panPixelsY = std::clamp(panPixelsY, -4096.0, 4096.0);

    std::vector<std::string> scene(static_cast<size_t>(rows),
                                   std::string(static_cast<size_t>(columns), ' '));
    const double centerX = (columns - 1) / 2.0;
    const double centerY = (rows - 1) / 2.0;
    const double panColumns = panPixelsX / 8.0;
    const double panRows = panPixelsY / 20.0;

    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < columns; ++col)
        {
            const double x = (col - centerX - panColumns) / zoom;
            const double y = (row - centerY - panRows) / zoom;
            const bool vertical = std::abs(std::remainder(x, 10.0)) < 0.36 / zoom;
            const bool horizontal = std::abs(std::remainder(y, 5.0)) < 0.36 / zoom;
            if (vertical || horizontal)
                plot(scene, col, row, vertical && horizontal ? '+' : vertical ? '|' : '-');
        }
    }

    const std::array<WorldPoint, 5> nodes = {{
        {-18.0, 3.0}, {-8.0, -3.0}, {2.0, -2.0}, {10.0, 3.0}, {18.0, -2.0}
    }};
    std::array<GridPoint, 5> screenNodes{};
    for (size_t index = 0; index < nodes.size(); ++index)
    {
        screenNodes[index] = {
            static_cast<int>(std::lround(centerX + nodes[index].x * zoom + panColumns)),
            static_cast<int>(std::lround(centerY + nodes[index].y * zoom + panRows))
        };
    }
    for (size_t index = 1; index < screenNodes.size(); ++index)
        line(scene, screenNodes[index - 1], screenNodes[index]);
    for (size_t index = 0; index < screenNodes.size(); ++index)
        plot(scene, screenNodes[index].x, screenNodes[index].y, index == 0 ? '@' : 'O');

    return scene;
}
