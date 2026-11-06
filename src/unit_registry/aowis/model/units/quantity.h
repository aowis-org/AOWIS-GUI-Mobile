#ifndef AOWIS_MODEL_UNITS_QUANTITY_H
#define AOWIS_MODEL_UNITS_QUANTITY_H

#include "canonical_registry.h"

#include <cstddef>
#include <string_view>

// Use Quantity for a source-code literal, and std::string_view/QString for
// runtime keys read from profiles, external formats, or user input.
// There is no runtime lookup for Quantity: invalid literals fail to compile.
namespace aowis::units
{
class Quantity
{
public:
    template <std::size_t N>
    consteval Quantity(const char (&key)[N]) : key_(key)
    {
        // The registry is constexpr, so a misspelling makes this immediate
        // constructor ill-formed rather than silently returning no unit.
        if (N < 2 || key[N - 1] != '\0'
            || canonicalUnit(std::string_view(key, N - 1)).empty())
            throw "Unknown AOWIS quantity key";
    }

    [[nodiscard]] consteval const char *c_str() const noexcept { return this->key_; }
    [[nodiscard]] constexpr std::string_view view() const noexcept { return this->key_; }

private:
    const char *key_;
};
} // namespace aowis::units

// Adapts validated literal quantities to legacy const-char*/string_view APIs.
// Dynamic keys should keep using the runtime APIs; never wrap an external key.
#define AOWIS_QUANTITY(literal) (::aowis::units::Quantity{literal}.c_str())

#endif // AOWIS_MODEL_UNITS_QUANTITY_H
