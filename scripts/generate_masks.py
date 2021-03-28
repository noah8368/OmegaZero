# Noah Himed
# 22 March 2021
#
# Generate all possible LERF formatted bitboards of attack sets. Squares are
# indexed MSB to LSB and sets include edge pieces.
#
# Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

from datetime import date


class MaskGenerator:

    def __init__(self):
        self.num_ranks = 8
        self.num_files = 8

    def get_bitboard(self, mask):
        bitboard_bn = ""
        for rank in range(self.num_ranks):
            for file in range(self.num_files):
                if mask[rank][file] == 1:
                    bitboard_bn = '1' + bitboard_bn
                else:
                    bitboard_bn = '0' + bitboard_bn
        return int(bitboard_bn, 2)

    # Get bitboards for forward pawn pushes, excluding captures
    def get_pawn_push_mask(self, rank, file, color):
        if color == "WHITE":
            if rank == 1:
                return self.get_non_slider_attack_mask(rank, file,
                                                       [(1, 0), (2, 0)])
            else:
                return self.get_non_slider_attack_mask(rank, file, [(1, 0)])
        elif color == "BLACK":
            if rank == 6:
                return self.get_non_slider_attack_mask(rank, file,
                                                       [(-1, 0), (-2, 0)])
            else:
                return self.get_non_slider_attack_mask(rank, file, [(-1, 0)])

    # Get piece masks for "sliding" pieces (Bishop and Rook)
    # The argument "move_dirs" is a list of two element tuples denoting single
    # move directions in the format (rank, file)
    def get_slider_piece_mask(self, rank, file, move_dirs,
                              endpoints_only=False):
        slider_board = [[0 for file in range(self.num_files)] for rank in \
                         range(self.num_ranks)]
        for dir in move_dirs:
            move_rank = rank + dir[0]
            move_file = file + dir[1]
            # Keep moving in one diagonal direction until piece is off board
            while (0 <= move_rank < self.num_ranks
                   and 0 <= move_file < self.num_files):
                if not endpoints_only:
                    slider_board[move_rank][move_file] = 1
                move_rank += dir[0]
                move_file += dir[1]

            # Get the last position before running off the board
            move_rank -= dir[0]
            move_file -= dir[1]
            # Check that the endpoint is not the original piece position
            if move_rank != rank or move_file != file:
                if endpoints_only:
                    slider_board[move_rank][move_file] = 1
                else:
                    slider_board[move_rank][move_file] = 0
        return self.get_bitboard(slider_board)

    def get_slider_attack_mask(self, moves, occupancy_mask, pos_rank,
                               pos_file):
        attack_mask = 0X0
        blocker_board = [[0 for file in range(self.num_files)] for rank in \
                            range(self.num_ranks)]
        attack_board = blocker_board
        for rank in range(self.num_ranks):
            for file in range(self.num_files):
                square =  self.num_files * rank + file
                square_bit_set = occupancy_mask & (1 << square)
                if square_bit_set:
                    blocker_board[rank][file] = 1

        for move in moves:
            # Move in one direction until a blocker piece is encountered
            move_rank = pos_rank + move[0]
            move_file = pos_file + move[1]
            while (0 <= move_rank < self.num_ranks
                   and 0 <= move_file < self.num_files):
                attack_board[move_rank][move_file] = 1
                # Break loop after the first blocking piece is encountered
                if blocker_board[move_rank][move_file] == 1:
                    break
                move_rank += move[0]
                move_file += move[1]

        return self.get_bitboard(attack_board)

    # Get attack masks for "non sliding" pieces (Pawn, Knight, King)
    # The argument moves is a list of two element tuples denoting moves
    # in the format (rank, file)
    def get_non_slider_attack_mask(self, rank, file, non_slider_moves):
        non_slider_board = [[0 for file in range(self.num_files)] for rank in \
                             range(self.num_ranks)]
        for move in non_slider_moves:
            move_rank = rank + move[0]
            move_file = file + move[1]
            # Detect if the move goes off the board
            if (0 <= move_rank < self.num_ranks
                and 0 <= move_file < self.num_files):
                non_slider_board[move_rank][move_file] = 1;
        return self.get_bitboard(non_slider_board)


# Format hex numbers with set number of digits and upper case letters
def format_hex(hex_num, num_digits):
    return "{0:#0{1}X}".format(hex_num, num_digits + 2)

def write_mask_set(f, mask_name, mask_generator, args):
    f.write("\n  // Define " + mask_name + ".\n  {")
    num_ranks = 8
    num_files = 8
    print_count = 0
    for rank in range(num_ranks):
        for file in range(num_files):
            mask = mask_generator(rank, file, *args)
            bitboard = format_hex(mask, 16)
            f.write(bitboard)
            print_count += 1
            if rank == num_ranks - 1 and file == num_files - 1:
                f.write('}')
            elif print_count == 3:
                f.write(",\n   ")
                print_count = 0
            else:
                f.write(", ")

if __name__ == "__main__":
    date = date.today().strftime("%d %B %Y")
    boiler_plate = ("// Noah Himed" + "\n// " + date + "\n//"
        + "\n// Define the bit masks used to denote the positions pieces can "
        + "move \n// to in the bitboard representation of a chess board.\n//"
        + "\n// Licensed under MIT License. Terms and conditions enclosed in "
        + "\"LICENSE.txt\".\n\n#include \"board.h\""
        + "\n#include \"constants.h\"\n\n")
    mask_gen = MaskGenerator()
    f = open("../src/masks.cpp", 'w')
    f.write(boiler_plate)

    f.write("const Bitboard "
            + "kNonSliderAttackMasks[kNumNonSliderMasks][kNumSquares] = {")
    write_mask_set(f, "white pawn push attack masks",
                   mask_gen.get_pawn_push_mask, ["WHITE"])
    f.write(",")
    white_pawn_captures = [(1, 1), (1, -1)]
    write_mask_set(f, "white pawn capture attack masks",
                   mask_gen.get_non_slider_attack_mask, [white_pawn_captures])
    f.write(",")
    write_mask_set(f, "black pawn push attack masks",
                   mask_gen.get_pawn_push_mask, ["BLACK"])
    f.write(",")
    black_pawn_captures = [(-1, 1), (-1, -1)]
    write_mask_set(f, "black pawn capture attack masks",
                   mask_gen.get_non_slider_attack_mask, [black_pawn_captures])
    f.write(",")
    knight_moves = [(2, 1), (2, -1), (1, 2), (1, -2), (-1, 2), (-1, -2),
                    (-2, 1), (-2, -1)]
    write_mask_set(f, "knight attack masks",
                   mask_gen.get_non_slider_attack_mask, [knight_moves])
    f.write(",")
    king_moves = [(1, 1), (1, -1), (-1, 1), (-1, -1), (0, 1), (1, 0), (0, -1),
                  (-1, 0)]
    write_mask_set(f, "king attack masks",
                   mask_gen.get_non_slider_attack_mask, [king_moves])
    f.write("\n};")

    f.write("\n\nconst Bitboard "
            + "kSliderPieceMasks[kNumSliderMasks][kNumSquares] = {")
    bishop_moves = [(1, 1), (1, -1), (-1, 1), (-1, -1)]
    write_mask_set(f, "bishop piece masks",
                   mask_gen.get_slider_piece_mask, [bishop_moves])
    f.write(",")
    rook_moves = [(0, 1), (1, 0), (0, -1), (-1, 0)]
    write_mask_set(f, "rook piece masks",
                   mask_gen.get_slider_piece_mask, [rook_moves])
    f.write("\n};")

    f.write("\n\nconst Bitboard "
            + "kSliderEndpointMasks[kNumSliderMasks][kNumSquares] = {")
    write_mask_set(f, "bishop endpoint masks",
                   mask_gen.get_slider_piece_mask, [bishop_moves, True])
    f.write(",")
    write_mask_set(f, "rook endpoint masks",
                   mask_gen.get_slider_piece_mask, [rook_moves, True])
    f.write("\n};")
