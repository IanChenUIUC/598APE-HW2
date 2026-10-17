#pragma once

#include "program.h"
#include <cstdint>

namespace genetic {
namespace chez {

/**
 * @brief Evaluate programs by compiling each one with Chez Scheme
 *
 * @param progs   Pointer to programs
 * @param data    Pointer to input dataset (in col-major format)
 * @param y_pred  Pointer to output of program evaluation
 * @param n_rows  Number of rows in the input dataset
 * @param n_progs Number of programs being evaluated
 */
void execute(const program_t progs, const float *data, float *y_pred,
             const uint64_t n_rows, const uint64_t n_progs);

} // namespace chez
} // namespace genetic
