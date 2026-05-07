/* Noah Himed
 *
 * Define a custom exception type to signal illegal or invalid chess moves.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_BAD_MOVE_H_
#define OMEGAZERO_SRC_BAD_MOVE_H_

#include <stdexcept>

namespace omegazero {

class BadMove : public std::exception {
 public:
  BadMove(const char* msg) { msg_ = msg; }
  const char* what() const noexcept override { return msg_; }

 private:
  const char* msg_;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_BAD_MOVE_H_
