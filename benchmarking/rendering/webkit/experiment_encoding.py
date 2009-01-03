#!/usr/bin/env python


#
# What is not working:
#    -delta compress glyphs... the diff is too random
#     the glyph_index gets pretty high with the more
#     font faces we have....reducing the number of faces
#     will reduce this number...
#
# What might work:
#    -omit the sign of the x-delta and let it wrap
#     around at the page width. So the maximum x
#     number would be the page width...
#
#

#
# Experiment with the encoding of the data
last_glyph_index = 0
package_one_byte = 0
package_two_byte = 0
package_three_byte = 0
glyph_map = {}

class BitWriter:
    def __init__(self):
        self.bits = []

    def write_4bits(self, bit):
        assert bit == (bit&0xf)
        self.write_bit( (bit&0x8)>>3 )
        self.write_bit( (bit&0x4)>>2 )
        self.write_bit( (bit&0x2)>>1 )
        self.write_bit( (bit&0x1)>>0 )

    def write_8bits(self, bit):
        assert bit == (bit&0xff)
        self.write_4bits( (bit&0xf0)>>4)
        self.write_4bits( (bit&0x0f)>>0)

    def write_bit(self, bit):
        assert bit == (bit&0x1)
        if bit:
            self.bits.append(1)
        else:
            self.bits.append(0)

    def consume(self):
        "Consume bits to a packed 8byte"
        import struct

        result = ""
        while len(self.bits) >= 8:
            operate = self.bits[0:8]
            self.bits = self.bits[8:]

            data = (operate[0]<<7) | (operate[1]<<6) | (operate[2]<<5) | (operate[3]<<4) | (operate[4]<<3) | (operate[5]<<2) | (operate[6]<<1) | (operate[7]<<0)
            result = "%s%s" % (result,struct.pack("<B", data))
        return result

    def finish(self):
        "Consume everything that is still left"
        remainder = len(self.bits)%8
        for i in range(0,8-remainder):
            self.write_bit(0)
        assert len(self.bits)%8 == 0
        return self.consume()


bit_writer = BitWriter()


# Load glyphs... share this with render_text.py
def load():
    glyphs = []
    for line in open("render_text.blib"):
        split = line.strip().split(',')

        # Throw away glyphs we will not render (normally zero spaced)
        if split[3] == '3':
            continue

        # Throw out invisible text
        if int(split[0]) > 240:
            continue

        glyph = { 'x'     : int(split[0]),
            'y'     : int(split[1]),
            'font'  : split[2],
            'glyph' : split[3] }
        glyphs.append(glyph)

    return glyphs

def sort(glyphs):
    # Sort glyphs so that glyphs are on the same line are next to each other
    def glyph_cmp(left, right):
        if left['y'] < right['y']:
            return -1
        elif left['y'] > right['y']:
            return 1

        if left['x'] < right['x']:
            return -1
        elif left['x'] > right['x']:
            return 1
        return 0

    glyphs.sort(glyph_cmp)
    return glyphs

def delta_compress(glyphs):
    """
    Save the delta between the last and the current glyph. On a line
    wrap the delat will be insanely huge and negative... we can detect
    that later and enter a return -XYZ => - Positive option...
    """
    new_glyphs = []
    last_x = 0
    last_y = 0

    for glyph in glyphs:
        new_glyph = { 'x' : glyph['x'] - last_x,
                      'y' : glyph['y'] - last_y,
                      'font' : glyph['font'],
                      'glyph': glyph['glyph'] }

        # Handle the line wrap...
        # The maximum delta can be 239.
        if new_glyph['x'] < 0:
            x = 240 - last_x
            new_glyph['x'] = x+glyph['x']

        # Sanity
        assert glyph['x'] >= 0
        assert new_glyph['x'] >= 0

        last_x = glyph['x']
        last_y = glyph['y']
        assert last_x >= 0
        assert last_y > 0

        new_glyphs.append(new_glyph)
    return new_glyphs

def map_font_description_to_glyph_index(glyph):
    key = "Glyph:%s-Font:%s" % (glyph['glyph'], glyph['font'])
    if not key in glyph_map:
        global last_glyph_index
        glyph_map[key] = last_glyph_index
        last_glyph_index = last_glyph_index + 1

    return glyph_map[key]

def rle_encode(glyphs):
    import math

    def highest_bit(x):
        """
        Runtime length encode the number
        """
        if x == 0:
            return 1

        # find the highest bit
        highest_bit = 0
        for i in range(0, int(math.ceil(math.log(x,2)))+1) :
            mask = 1 << i
            if (x & mask)>>i == 1:
                highest_bit = i

        return highest_bit+1

    def pack_glyph(x, y, glyph_index):
        """
        Pack the glyph together

        Simple bitcode...
        00 = X,Glyph Position 4 byte
            Legal combination
            X-Glyph-X
            X-Y-Glyph-X
            X-X would not be legal.. so combining these
            is legal

        01 = X,Glyph Position Position 8 byte
            Legal combinations as above
        10 = Y Position 4 Byte
        11 = Extended package...
        111 - 8-Byte Y,Glyph
        110 - Extended...

        """
        x_bits = highest_bit(x)
        if x_bits <= 4:
            bit_writer.write_bit(0)
            bit_writer.write_bit(0)
            bit_writer.write_4bits(x)
        elif x_bits <= 8:
            bit_writer.write_bit(0)
            bit_writer.write_bit(1)
            bit_writer.write_8bits(x)
        else:
            print x, x_bits, glyph_index
            assert False

        y_bits = highest_bit(y)
        # If we have no delta... omit the data
        if y == 0:
            pass
        elif y_bits <= 4:
            bit_writer.write_bit(1)
            bit_writer.write_bit(0)
            bit_writer.write_4bits(y)
        elif y_bits <= 8:
            bit_writer.write_bit(1)
            bit_writer.write_bit(1)
            bit_writer.write_bit(1)
            bit_writer.write_8bits(y)
        else:
            print y, y_bits, glyph_index
            assert False


        index_bits = highest_bit(glyph_index)
        if index_bits <= 4:
            bit_writer.write_bit(0)
            bit_writer.write_bit(0)
            bit_writer.write_4bits(glyph_index)
        elif index_bits <= 8:
            bit_writer.write_bit(0)
            bit_writer.write_bit(1)
            bit_writer.write_8bits(glyph_index)
        else:
            print x, x_bits, index_bits, glyph_index
            assert False


        return bit_writer.consume()



    import copy
    glyphs = copy.deepcopy(glyphs)

    delta_compressed = open("delta_compressed", "w")
    delta_compressed_glyph_index = open("delta_compressed_glyph_index", "w")

    prev = None
    smallest_x = 0
    largest_x = 0
    for glyph in glyphs:
        glyph['glyph_index'] = map_font_description_to_glyph_index(glyph)

        # Gather some information
        if glyph['x'] < smallest_x:
            smallest_x = glyph['x']
        elif glyph['x'] > largest_x:
            largest_x = glyph['x']
        prev = glyph

        if glyph['y'] == 0:
            delta_compressed.write("%(x)d;%(glyph_index)d" % glyph)
        else:
            delta_compressed.write("%(x)d,%(y)d,%(glyph_index)d" % glyph)
        delta_compressed_glyph_index.write(pack_glyph(glyph['x'], glyph['y'], glyph['glyph_index']))

    # Write the pending bits
    delta_compressed_glyph_index.write(bit_writer.finish())
    print "Larges and smallest x delta", largest_x, smallest_x


glyphs = sort(load())
delta = delta_compress(glyphs)
rle_encode(delta)
print "Last glyph", last_glyph_index