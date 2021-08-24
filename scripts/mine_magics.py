"""Noah Himed

Generates magic numbers for move generation in the board implementation.

Randomly generate numbers to find "magic" numbers which can hash a sparsely
populated key of a blocking piecess to an attack mask for bishops and rooks.
Please note that this file is not meant to be manually executed, and instead
should be ran by the included project Makefile.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

from generate_masks import format_hex, MaskGenerator
import os
import random


class MagicsGenerator:
    """Finds a set of magic numbers and their corresponding hashmap"""
    def __init__(self, max_attempts):
        self.num_ranks = 8
        self.num_files = 8
        self.num_sq = 64
        self.bishop_magics_lengths = [
          [6, 5, 5, 5, 5, 5, 5, 6],
          [5, 5, 5, 5, 5, 5, 5, 5],
          [5, 5, 7, 7, 7, 7, 5, 5],
          [5, 5, 7, 9, 9, 7, 5, 5],
          [5, 5, 7, 9, 9, 7, 5, 5],
          [5, 5, 7, 7, 7, 7, 5, 5],
          [5, 5, 5, 5, 5, 5, 5, 5],
          [6, 5, 5, 5, 5, 5, 5, 6]
        ]
        self.bishop_moves = [(1, 1), (1, -1), (-1, 1), (-1, -1)]
        self.rook_magics_lengths = [
          [12, 11, 11, 11, 11, 11, 11, 12],
          [11, 10, 10, 10, 10, 10, 10, 11],
          [11, 10, 10, 10, 10, 10, 10, 11],
          [11, 10, 10, 10, 10, 10, 10, 11],
          [11, 10, 10, 10, 10, 10, 10, 11],
          [11, 10, 10, 10, 10, 10, 10, 11],
          [11, 10, 10, 10, 10, 10, 10, 11],
          [12, 11, 11, 11, 11, 11, 11, 12]
        ]
        self.rook_moves = [(0, 1), (1, 0), (0, -1), (-1, 0)]
        self.mask_gen = MaskGenerator()
        self.max_attempts = max_attempts

        self.magics_map = {}
        self.bishop_magics = []
        self.rook_magics = []

    def count_set_bits(self, n):
        """Counts the numbers of set bits in n"""
        n_bn = bin(n)
        n_bn = n_bn[2:]
        set_bits = [bit for bit in n_bn if bit == '1']
        num_set_bits = len(set_bits)
        return num_set_bits

    def get_single_magic(self, rank, file, piece_mask, magic_len, piece_type):
        """Generates a random value and checks if it is a valid magic."""
        num_set_bits = self.count_set_bits(piece_mask)
        num_blocker_masks = 1 << num_set_bits
        blocker_to_attack_map = {}
        if piece_type == "BISHOP":
            moves = [(1, 1), (1, -1), (-1, 1), (-1, -1)]
        elif piece_type == "ROOK":
            moves = [(0, 1), (1, 0), (0, -1), (-1, 0)]
        # Compute all possible blocker masks given a piece mask
        for blocker_mask_index in range(num_blocker_masks):
            blocker_mask = piece_mask
            set_bit_index = 0
            for square_index in range(self.num_sq):
                # Check if the bit at square_index is set in blocker_mask
                if blocker_mask & (1 << square_index):
                    # If the bit at set_bit_index is not set, clear the bit
                    # set at blocker_mask_index
                    if not (blocker_mask_index & (1 << set_bit_index)):
                        blocker_mask &= ~(1 << square_index)
                    set_bit_index += 1
            attack_mask = self.mask_gen.get_slider_attack_mask(moves,
                                                               blocker_mask,
                                                               rank, file)
            blocker_to_attack_map[blocker_mask] = attack_mask
        index_to_attack_map = {}
        for trial in range(self.max_attempts):
            magic_is_valid = True
            # Compute a 64 bit random number with only a few set bits
            magic = (random.getrandbits(self.num_sq)
                     & random.getrandbits(self.num_sq)
                     & random.getrandbits(self.num_sq))
            # Discard magics with less than six bits set in the upper byte
            # of a 64-bit product since they are unlikely to be valid
            upper_bits = (magic ** 2) & 0xFF00000000000000
            if self.count_set_bits(upper_bits) < 6:
                continue
            # Check that the magic number hashes all occupancy masks correctly
            # to each corresponding attack set
            for blocker_mask in blocker_to_attack_map:
                attack_mask = blocker_to_attack_map[blocker_mask]
                index = magic * blocker_mask >> self.num_sq - magic_len
                # Check that the index is not bigger than magic_len bits
                if index > 0XFFFFFFFFFFFFFFFF:
                    magic_is_valid = False
                    break
                elif index not in index_to_attack_map:
                    index_to_attack_map[index] = attack_mask
                elif (index_to_attack_map[index] != attack_mask):
                    magic_is_valid = False
                    break
            if magic_is_valid:
                return magic, index_to_attack_map
        print("FAILED TO FIND SUITABLE MAGIC FOR RANK " + str(rank) + ", FILE "
              + str(file))

    def mine_magics(self, piece_type):
        """Generate a list of valid magics and their corresponding hash map"""
        magics_map = {}
        magics = []
        for rank in range(self.num_ranks):
            for file in range(self.num_files):
                if piece_type == "BISHOP":
                    bishop_mask = self.mask_gen.get_slider_piece_mask(
                        rank, file, self.bishop_moves
                    )
                    magic_len = self.bishop_magics_lengths[rank][file]
                    magic, index_to_attack_map = self.get_single_magic(
                        rank, file, bishop_mask, magic_len, piece_type
                    )
                elif piece_type == "ROOK":
                    rook_mask = self.mask_gen.get_slider_piece_mask(
                        rank, file, self.rook_moves
                    )
                    magic_len = self.rook_magics_lengths[rank][file]
                    magic, index_to_attack_map = self.get_single_magic(
                        rank, file, rook_mask, magic_len, piece_type
                    )
                magics.append(magic)
                magics_map = {**magics_map, **index_to_attack_map}
        if piece_type == "BISHOP":
            self.bishop_magics = magics
        elif piece_type == "ROOK":
            self.rook_magics = magics
        self.magics_map = {**self.magics_map, **magics_map}


def write_magics(f, piece_name, magics):
    """Formats the list magics neatly in a file"""
    f.write("\n  // Define " + piece_name + " magics.\n  {")
    print_count = 0
    for magic in magics:
        formatted_magic = format_hex(magic, 16)
        f.write(formatted_magic)
        print_count += 1
        if magic == magics[-1]:
            f.write('}')
        elif print_count == 3:
            f.write(",\n   ")
            print_count = 0
        else:
            f.write(", ")


def write_magics_map(f, magics_map):
    """Formats the dictionary magics_map neatly in a file"""
    for index in magics_map:
        formatted_index = format_hex(index, 16)
        formatted_attack_mask = format_hex(magics_map[index], 16)
        f.write("  {" + formatted_index + ", " + formatted_attack_mask
                + "}")
        if index == list(magics_map)[-1]:
            f.write("\n};")
        else:
            f.write(",\n")


if __name__ == "__main__":
    magics_gen = MagicsGenerator(10**6)
    magics_gen.mine_magics("BISHOP")
    magics_gen.mine_magics("ROOK")
    # Write the mined magic numbers to the C++ file "magics.cc"
    print("Found magics\nWriting to file (this will take a few minutes)...")
    boiler_plate = ("/* Noah Himed" + "\n*"
                    + "\n* Define the magic numbers and hashmap used to "
                    + "generate attack masks \n* for sliding pieces "
                    + "(bishop, rook, and queen).\n*\n* Licensed under "
                    + "MIT License. Terms and conditions enclosed in "
                    + "\"LICENSE.txt\".\n*/\n\n#include \"board.h\""
                    + "\n#include \"constants.h\"\n\n#include <cstdint>"
                    + "\n#include <unordered_map>\n\n")
    f = open(os.path.join(os.getcwd(), "src/magics.cc"), 'w')
    f.write(boiler_plate)
    f.write("const uint64_t kMagics[kNumSliderMasks][kNumSquares] = {")
    write_magics(f, "bishop", magics_gen.bishop_magics)
    f.write(",\n")
    write_magics(f, "rook", magics_gen.rook_magics)
    f.write("\n};")
    f.write("\n\nconst std::unordered_map<uint64_t, Bitboard> "
            + "kMagicIndexToAttackMap = {\n")
    write_magics_map(f, magics_gen.magics_map)
    f.write("\n")
    f.close()
