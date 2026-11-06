#include "compass_heading.h"

#include <cassert>
#include <cmath>
#include <string>

int main()
{
    assert(AowisCompass::normalize(0.0) == 0.0);
    assert(AowisCompass::normalize(360.0) == 0.0);
    assert(AowisCompass::normalize(725.0) == 5.0);
    assert(AowisCompass::normalize(-10.0) == 350.0);
    assert(AowisCompass::normalize(-720.0) == 0.0);
    assert(AowisCompass::normalize(345.0 + 90.0) == 75.0);
    assert(AowisCompass::normalize(NAN) == -1.0);
    assert(AowisCompass::normalize(INFINITY) == -1.0);
    assert(std::string(AowisCompass::direction(0.0)) == "N");
    assert(std::string(AowisCompass::direction(44.0)) == "NE");
    assert(std::string(AowisCompass::direction(90.0)) == "E");
    assert(std::string(AowisCompass::direction(135.0)) == "SE");
    assert(std::string(AowisCompass::direction(180.0)) == "S");
    assert(std::string(AowisCompass::direction(225.0)) == "SW");
    assert(std::string(AowisCompass::direction(270.0)) == "W");
    assert(std::string(AowisCompass::direction(315.0)) == "NW");
    assert(std::string(AowisCompass::direction(359.9)) == "N");
    assert(std::string(AowisCompass::direction(-1.0)) == "N");
    assert(std::string(AowisCompass::direction(NAN)) == "--");
    assert(AowisCompass::clampedCalibration(-5.0) == 0.0);
    assert(AowisCompass::clampedCalibration(0.5) == 0.5);
    assert(AowisCompass::clampedCalibration(2.0) == 1.0);
    assert(AowisCompass::clampedCalibration(INFINITY) == -1.0);
}
