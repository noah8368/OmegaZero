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
  int player = kWhite;

  while (game.IsActive()) {
    game.Play(player);
    // Switch to the other player
    player ^= 1;
  }
  game.OutputGameResolution();
}
