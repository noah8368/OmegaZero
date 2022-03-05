/* Noah Himed
 *
 * Define a custom exception type to signal that the time limit has been reached
 * during iterative deepening.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_OUT_OF_TIME_H
#define OMEGAZERO_SRC_OUT_OF_TIME_H

#include <stdexcept>

namespace omegazero {

class OutOfTime : public std::exception {};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_OUT_OF_TIME_H