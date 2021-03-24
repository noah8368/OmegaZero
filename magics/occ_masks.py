# Noah Himed
# 22 March 2021
#
# Generate all possible LERF formatted bitboards for attack sets of pieces
# for empty boards. Squares are indexed MSB to LSB and sets include edge pieces.
#
# Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".


class MaskFinder:

    def __init__(self):
        self.num_ranks = 8
        self.num_files = 8
        self.empty_board = [[0 for i in range(self.num_files + 4)] for j in \
                             range(self.num_ranks + 4)]
        # Initialize the outer two "rings" of the board with -1's for off board
        # detection purposes
        for row in range(self.num_ranks + 4):
            for col in range(self.num_files + 4):
                if (row in [0, 1, self.num_ranks + 2, self.num_ranks + 3] or
                    col in [0, 1, self.num_files + 2, self.num_files + 3]):
                    self.empty_board[row][col] = -1
        self.origin_rank = 2
        self.origin_file = 2

    # Get bitboards for forward pawn pushes, excluding captures
    def get_pawn_push_mask(self, rank, file, color):
        if color == "WHITE":
            if rank == 1:
                self.get_non_slider_mask(rank, file, [(1, 0), (2, 0)])
            else:
                self.get_non_slider_mask(rank, file, [(1, 0)])
        elif color == "BLACK":
            if rank == 6:
                self.get_non_slider_mask(rank, file, [(-1, 0), (-2, 0)])
            else:
                self.get_non_slider_mask(rank, file, [(-1, 0)])

    # Used to get attack masks for "sliding" pieces (Bishop and Rook)
    # The argument move_dirs is a list of two element tuples denoting single
    # move directions in the format (rank, file)
    def get_slider_mask(self, rank, file, move_dirs):
        slider_board = [[0 for file in range(self.num_files)] for rank in \
                         range(self.num_ranks)]
        for dir in move_dirs:
            move_rank = rank + dir[0]
            move_file = file + dir[1]
            # Keep moving in one diagonal direction until piece is off board
            while self.empty_board[self.origin_rank + move_rank]\
                                  [self.origin_file + move_file] != -1:
                slider_board[move_rank][move_file] = 1
                move_rank += dir[0]
                move_file += dir[1]
        self.print_bitboard(slider_board)

    # Used to get attack masks for "non sliding" pieces (Pawn, Knight, King)
    # The argument moves is a list of two element tuples denoting moves
    # in the format (rank, file)
    def get_non_slider_mask(self, rank, file, non_slider_moves):
        non_slider_board = [[0 for i in range(self.num_files)] for j in \
                         range(self.num_ranks)]
        for move in non_slider_moves:
            move_rank = rank + move[0]
            move_file = file + move[1]
            # Detect if the move goes off the board.
            if self.empty_board[self.origin_rank + move_rank]\
                               [self.origin_file + move_file] != -1:
                non_slider_board[move_rank][move_file] = 1;
        self.print_bitboard(non_slider_board)

    def print_bitboard(self, attack_mask):
        hex_mapping = {"0000":'0', "0001":'1', "0010":'2', "0011":'3',
                       "0100":'4', "0101":'5', "0110":'6', "0111":'7',
                       "1000":'8', "1001":'9', "1010":'A', "1011":'B',
                       "1100":'C', "1101":'D', "1110":'E', "1111":'F'}
        bitboard_bn = ""
        for rank in range(self.num_ranks):
            for file in range(self.num_files):
                if attack_mask[rank][file] == 1:
                    bitboard_bn = '1' + bitboard_bn
                else:
                    bitboard_bn = '0' + bitboard_bn
        num_hex_digits = 16
        bitboard_hex = "0x"
        for digit_count in range(num_hex_digits):
            bin_pattern = bitboard_bn[digit_count * 4:(digit_count + 1) * 4]
            hex_pattern = hex_mapping[bin_pattern]
            bitboard_hex += hex_pattern
        print(bitboard_hex + ',')

def get_masks(set):
    mask_finder = MaskFinder()
    for rank in range(mask_finder.num_ranks):
        for file in range(mask_finder.num_files):
            if set == "WHITE PAWN PUSH":
                mask_finder.get_pawn_push_mask(rank, file, "WHITE")
            elif set == "WHITE PAWN CAPTURE":
                mask_finder.get_non_slider_mask(rank, file, [(1, 1), (1, -1)])
            elif set == "BLACK PAWN PUSH":
                mask_finder.get_pawn_push_mask(rank, file, "BLACK")
            elif set == "BLACK PAWN CAPTURE":
                mask_finder.get_non_slider_mask(rank, file, [(-1, 1), (-1, -1)])
            elif set == "KNIGHT":
                mask_finder.get_non_slider_mask(rank, file,
                                               [(2, 1), (2, -1), (1, 2),
                                               (1, -2), (-1, 2), (-1, -2),
                                               (-2, 1), (-2, -1)])
            elif set == "BISHOP":
                mask_finder.get_slider_mask(rank, file,
                                           [(1, 1), (1, -1), (-1, 1), (-1, -1)])
            elif set == "ROOK":
                mask_finder.get_slider_mask(rank, file,
                                           [(0, 1), (1, 0), (0, -1), (-1, 0)])
            elif set == "KING":
                mask_finder.get_non_slider_mask(rank, file,
                                               [(1, 1), (1, -1), (-1, 1),
                                               (-1, -1), (0, 1), (1, 0),
                                               (0, -1), (-1, 0)])

if __name__ == "__main__":
    print("WHITE PAWN PUSH MASKS")
    get_masks("WHITE PAWN PUSH")
    print("\n\nWHITE PAWN CAPTURE MASKS")
    get_masks("WHITE PAWN CAPTURE")
    print("\n\nBLACK PAWN PUSH MASKS")
    get_masks("BLACK PAWN PUSH")
    print("\n\nBLACK PAWN CAPTURE MASKS")
    get_masks("BLACK PAWN CAPTURE")
    print("\n\nKNIGHT MASKS")
    get_masks("KNIGHT")
    print("\n\nBISHOP MASKS")
    get_masks("BISHOP")
    print("\n\nROOK MASKS")
    get_masks("ROOK")
    print("\n\nKING MASKS")
    get_masks("KING")
