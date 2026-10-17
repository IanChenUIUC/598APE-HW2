#pragma once

#include <cstdint>
#include <program.h>

namespace genetic {
namespace jit {

/**
 * Evaluate programs by compiling each one to machine code with copy-and-patch.
 * Programs that cannot be compiled are evaluated with the interpreter.
 *
 * @param progs    Pointer to programs
 * @param data     Pointer to input dataset (in col-major format)
 * @param y_pred   Pointer to output of program evaluation
 * @param n_rows   Number of rows in the input dataset
 * @param n_progs  Total number of programs being evaluated
 */
void execute(const program_t progs, const float *data, float *y_pred,
             const uint64_t n_rows, const uint64_t n_progs);

} // namespace jit
} // namespace genetic
