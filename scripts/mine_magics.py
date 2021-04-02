# Noah Himed
# 23 March 2021
#
# Randomly generate numbers to find "magic" numbers which can hash a sparsely
# populated key of a blocking piecess to an attack mask for bishops and rooks.
#
# Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

from datetime import date
import math
import random

from generate_masks import format_hex, MaskGenerator

class MagicsGenerator:

    def __init__(self, num_attempts):
        self.num_ranks = 8
        self.num_files = 8
        self.num_squares = 64
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
        self.num_attempts = num_attempts

        self.magics_map = {}
        self.bishop_magics = []
        self.rook_magics = []

    def count_set_bits(self, n):
        n_bn = bin(n)
        n_bn = n_bn[2:]
        set_bits = [bit for bit in n_bn if bit == '1']
        num_set_bits = len(set_bits)
        return num_set_bits

    def get_single_magic(self, rank, file, piece_mask, magic_len, piece_type):
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
            for square_index in range(self.num_squares):
                if blocker_mask & (1 << square_index):
                    if not (blocker_mask_index & (1 << set_bit_index)):
                        blocker_mask &= ~(1 << square_index)
                    set_bit_index += 1
            attack_mask = self.mask_gen.get_slider_attack_mask(moves,
                                                               blocker_mask,
                                                               rank, file)
            blocker_to_attack_map[blocker_mask] = attack_mask
        index_to_attack_map = {}
        for trial in range(self.num_attempts):
            magic_is_valid = True
            # Compute a 64 bit random number with only a few set bits
            magic = (random.getrandbits(self.num_squares)
                     & random.getrandbits(self.num_squares)
                     & random.getrandbits(self.num_squares))

            # Discard magics with less than six bits set in the upper byte
            # of a 64-bit product since they are unlikely to be valid
            upper_bits = (magic ** 2) & 0xFF00000000000000
            if self.count_set_bits(upper_bits) < 6:
                continue
            # Check that the magic number hashes all occupancy masks correctly
            # to each corresponding attack set
            for blocker_mask in blocker_to_attack_map:
                attack_mask = blocker_to_attack_map[blocker_mask]
                index = (magic * blocker_mask) >> (self.num_squares - magic_len)
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

                # Debugging code
                if piece_type == "BISHOP" and rank == 3 and file == 4:
                    print("MAGIC:", hex(magic))
                    # Blocker mask for bishop on e4 with starting layout
                    blocker_mask = 0X2000000004400
                    print("Is blocker mask in blocker > attack mask?",
                          str("YES" if blocker_mask in blocker_to_attack_map
                              else "NO"))
                    # Should print 55
                    print("shift amount:", str(self.num_squares - magic_len))
                    index = (magic * blocker_mask) >> (self.num_squares - magic_len)
                    print("index:", hex(index))
                    # Should print "0x82442800284400"
                    print("attack set:", hex(index_to_attack_map[index]))

                return magic, index_to_attack_map
        print("FAILED TO FIND SUITABLE MAGIC FOR RANK " + str(rank) + ", FILE "
              + str(file))

    def mine_magics(self, piece_type):
        magics_map = {}
        magics = []
        for rank in range(self.num_ranks):
            for file in range(self.num_files):
                if piece_type == "BISHOP":
                    bishop_mask = self.mask_gen.get_slider_piece_mask(rank,
                                                        file, self.bishop_moves)
                    magic_len = self.bishop_magics_lengths[rank][file]
                    magic, index_to_attack_map = self.get_single_magic(rank,
                                                                file,
                                                                bishop_mask,
                                                                magic_len,
                                                                piece_type)
                elif piece_type == "ROOK":
                    rook_mask = self.mask_gen.get_slider_piece_mask(rank,
                                                                file,
                                                                self.rook_moves)
                    magic_len = self.rook_magics_lengths[rank][file]
                    magic, index_to_attack_map = self.get_single_magic(rank,
                                                                file, rook_mask,
                                                                magic_len,
                                                                piece_type)
                magics.append(magic)
                magics_map = {**magics_map, **index_to_attack_map}
        if piece_type == "BISHOP":
            self.bishop_magics = magics
        elif piece_type == "ROOK":
            self.rook_magics = magics
        self.magics_map = {**self.magics_map, **magics_map}


def write_magics(f, piece_name, magics):
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
    for index in magics_map:
        formatted_index = format_hex(index, 16)
        formatted_attack_mask = format_hex(magics_map[index], 16)
        f.write("  {" + formatted_index + ", " + formatted_attack_mask
                + "}")
        if index == list(magics_map)[-1]:
            f.write("};")
        else:
            f.write(",\n")

if __name__ == "__main__":
    magics_gen = MagicsGenerator(10**6)
    magics_gen.mine_magics("BISHOP")
    magics_gen.mine_magics("ROOK")

    print("Found magics\nWriting to file...")
    date = date.today().strftime("%d %B %Y")
    boiler_plate = ("// Noah Himed" + "\n// " + date + "\n//"
        + "\n// Define the magic numbers and hashmap used to generate attack"
        + " masks \n// for sliding pieces (bishop, rook, and queen).\n//"
        + "\n// Licensed under MIT License. Terms and conditions enclosed in"
        + "\"LICENSE.txt\".\n\n#include \"board.h\"\n#include \"constants.h\""
        + "\n\n#include<cstdint>\n#include <unordered_map>\n\n")
    f = open("../src/magics.cpp", 'w')
    f.write(boiler_plate)
    f.write("const uint64_t kMagics[kNumSliderMasks][kNumSquares] = {")
    write_magics(f, "bishop", magics_gen.bishop_magics)
    f.write(",\n")
    write_magics(f, "rook", magics_gen.rook_magics)
    f.write("\n};")
    f.write("\n\nconst std::unordered_map<uint64_t, Bitboard> "
            + "kMagicIndexToAttackMap = {\n")
    write_magics_map(f, magics_gen.magics_map)

    f.close()
