/*
 * Xournal++
 *
 * Enum available stabilizer
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

namespace StrokeStabilizer {

// Append new values to preserve the integer representation stored in settings.
enum class AveragingMethod { NONE, ARITHMETIC, VELOCITY_GAUSSIAN, ONE_EURO, GOOGLE_INK };
static_assert(static_cast<int>(AveragingMethod::ONE_EURO) == 3, "Averaging methods are persisted by ordinal");
static_assert(static_cast<int>(AveragingMethod::GOOGLE_INK) == 4, "Averaging methods are persisted by ordinal");

constexpr bool isValid(AveragingMethod am) {
    return am == AveragingMethod::NONE || am == AveragingMethod::ARITHMETIC ||
           am == AveragingMethod::VELOCITY_GAUSSIAN || am == AveragingMethod::ONE_EURO ||
           am == AveragingMethod::GOOGLE_INK;
}

enum class Preprocessor { NONE, DEADZONE, INERTIA };

constexpr bool isValid(Preprocessor pp) {
    return pp == Preprocessor::NONE || pp == Preprocessor::DEADZONE || pp == Preprocessor::INERTIA;
}
}  // namespace StrokeStabilizer
