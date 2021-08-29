/* Noah Himed
*
* Use a Game object to manage moves in a Chess game.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

#include "game.h"

int main() {
  Game game;
  while (game.IsActive()) {
    game.Play();
  }
}
