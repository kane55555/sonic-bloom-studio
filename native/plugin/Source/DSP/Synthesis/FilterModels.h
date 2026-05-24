#pragma once
//==============================================================================
//  FilterModels.h — Selectable filter character models that wrap the
//  existing FilterBlock SVF. Each model is a small parameter preset
//  (resonance bias, drive, post-saturation, dual-mode HPF) plus extra
//  nonlinearity in the feedback path. This lets a single FilterBlock
//  switch between Clean / Analog / Vintage / Ladder / JP / Tape without
//  changing the audio graph.
//==============================================================================
#include "../FilterBlock.h"
#include <cmath>

namespace dida {

enum class FilterModel
{
    Clean,    // pristine, no drive, no post-sat
    Analog,   // mild pre-drive, mild post-sat — Juno-ish
    Vintage,  // heavier post-sat, smoother res — old SEM/Prophet
    Ladder,   // pre-drive + strong post-sat + 24 dB
    JP,       // dual HPF→LPF stage, bright resonance
    Tape      // soft, dark, gentle post-sat
};

inline void applyFilterModel(FilterBlock& f, FilterModel m, float baseDrive = 0.0f) noexcept
{
    switch (m)
    {
        case FilterModel::Clean:
            f.setDrive(0.0f); f.setOutputDrive(0.0f); f.setDualMode(false);
            break;
        case FilterModel::Analog:
            f.setDrive(0.20f + baseDrive * 0.5f); f.setOutputDrive(0.25f); f.setDualMode(false);
            break;
        case FilterModel::Vintage:
            f.setDrive(0.15f + baseDrive * 0.4f); f.setOutputDrive(0.45f); f.setDualMode(false);
            break;
        case FilterModel::Ladder:
            f.setType(FilterBlock::Type::LP24);
            f.setDrive(0.30f + baseDrive * 0.6f); f.setOutputDrive(0.55f); f.setDualMode(false);
            break;
        case FilterModel::JP:
            f.setType(FilterBlock::Type::LP24);
            f.setDrive(0.10f + baseDrive * 0.3f); f.setOutputDrive(0.30f); f.setDualMode(true);
            f.setHpCutoff(160.0f);
            break;
        case FilterModel::Tape:
            f.setDrive(0.05f); f.setOutputDrive(0.35f); f.setDualMode(false);
            break;
    }
}

inline FilterModel filterModelFromString(const char* s) noexcept
{
    if (!s) return FilterModel::Clean;
    // case-insensitive ASCII match
    auto eq = [&](const char* a, const char* b) {
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = char(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = char(cb - 'A' + 'a');
            if (ca != cb) return false;
            ++a; ++b;
        }
        return *a == *b;
    };
    if (eq(s, "analog"))  return FilterModel::Analog;
    if (eq(s, "vintage")) return FilterModel::Vintage;
    if (eq(s, "ladder"))  return FilterModel::Ladder;
    if (eq(s, "jp"))      return FilterModel::JP;
    if (eq(s, "tape"))    return FilterModel::Tape;
    return FilterModel::Clean;
}

} // namespace dida
