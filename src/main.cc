/* Noah Himed
*
* Use a Game object to manage moves in a Chess game.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

#include "constants.h"
#include "game.h"

// Debug library
#include <iostream>

int main() {
  Game game;
  int player = kWhite;
  while (game.IsActive()) {
    game.Play(player);
    player = (player == kWhite) ? kBlack : kWhite;
  }
  game.OutputGameResolution();
}
