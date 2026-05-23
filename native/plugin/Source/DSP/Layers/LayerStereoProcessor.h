#pragma once
//==============================================================================
//  LayerStereoProcessor.h — M/S stereo widener for the shared layer bus.
//
//  Subtle widening to glue layers into one cohesive stereo image without the
//  "hard pan" feel of independent layers. Width = 1.0 is the original image.
//  Range 0..2: 0 = mono, 1 = neutral, >1 = wider sides.
//==============================================================================
#include <algorithm>

class LayerStereoProcessor
{
public:
    void setWidth(float w) noexcept { width = std::clamp(w, 0.0f, 2.0f); }

    inline void process(float& l, float& r) const noexcept
    {
        const float mid  = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * width;
        l = mid + side;
        r = mid - side;
    }

private:
    float width = 1.18f; // gentle default widening
};
