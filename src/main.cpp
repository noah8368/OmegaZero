// Noah Himed
// 18 March 2021
//
// Use a Game object to manage moves in a Chess game.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#include "constants.h"
#include "game.h"

int main() {
  Game game;
  Player player = kWhite;

  while (game.isActive()) {
    game.play(player);
    // Switch to the other player
    player = static_cast<Player>(player ^ 1);
  }
  game.outputGameResolution();
}
