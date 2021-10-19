/* Noah Himed
 *
 * Define a custom exception type to signal illegal or invalid chess moves.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <stdexcept>

namespace omegazero {

class BadMove : public std::exception {
 public:
  BadMove(const char* msg) { msg_ = msg; }
  const char* what() const throw() { return msg_; }

 private:
  const char* msg_;
};

}  // namespace omegazero
