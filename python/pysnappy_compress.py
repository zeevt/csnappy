import os, sys
from struct import pack as struct_pack
from struct import unpack as struct_unpack
from array import array
from collections import defaultdict

def encode_varint32(f, n):
  while n:
    b = n & 127
    n >>= 7
    if n:
      b |= 128
    f.write(chr(b))

def snappy_emit_literal(f, s, begin, end):
  l = end - begin - 1 # end is one past the literal, length is encoded -1
  if l < 60:
    f.write(chr(l << 2))
  elif l < 256:
    f.write(chr(240)) # 60 << 2
    f.write(chr(l))
  else: # a length > 65536 is not supported by this encoder!
    f.write(chr(244)) # 61 << 2
    f.write(chr(l & 255))
    f.write(chr(l >> 8))
  f.write(s[begin : end])

def snappy_emit_backref(f, offset, length):
  if 4 <= length <= 11 and offset < 2048:
    f.write(chr(1 | ((length - 4) << 2) | ((offset >> 8) << 5)))
    f.write(chr(offset & 255))
  else: # a back offset with offset > 65536 is not supported by this encoder!
    encoded_offset = struct_pack("<H", offset)
    while length > 0:
      curr_len_chunk = min(length, 64)
      f.write(chr(2 | ((curr_len_chunk - 1) << 2)))
      f.write(encoded_offset)
      length -= curr_len_chunk

N = 4096 # up to 64K is allowed by this encoder
MIN_LENGTH = 4 # snappy back references with length < 4 encodes to 3 bytes

def snappy_compress_block_dict(ofile, s, ilen, wm = defaultdict(list)):
  """A compressor that does not miss matches and uses unlimited memory"""
  wm.clear()
  literal_start = 0
  i = 1
  while i < ilen - MIN_LENGTH:
    longest_match_length = MIN_LENGTH - 1
    longest_match_start = 0
    length_limit = ilen - i
    hash_chain = wm[s[i : i + MIN_LENGTH]]
    for j in hash_chain:
      length = MIN_LENGTH
      while length < length_limit and s[i + length] == s[j + length]:
        length += 1
      if length > longest_match_length:
        longest_match_length = length
        longest_match_start = j
    hash_chain.insert(0, i)
    if longest_match_length >= MIN_LENGTH:
      if i - 1 >= literal_start:
        snappy_emit_literal(ofile, s, literal_start, i)
      snappy_emit_backref(ofile, i - longest_match_start, longest_match_length)
      i += longest_match_length
      literal_start = i
    else:
      i += 1
  if i < ilen:
    snappy_emit_literal(ofile, s, literal_start, ilen)

TABLE_ITEMS_ORDER = 12
MASK = ((1 << TABLE_ITEMS_ORDER) - 1)
def snappy_compress_block_table(ofile, s, ilen, \
                                wm = array('H', [0]*(1 << TABLE_ITEMS_ORDER))):
  """A compressor that uses limited memory, but misses matches"""
  for i in xrange(len(wm)): wm[i] = 0
  literal_start = 0
  i = 1
  while i < ilen - MIN_LENGTH:
    hash_key = hash(s[i : i + MIN_LENGTH]) & MASK
    match_start = wm[hash_key]
    wm[hash_key] = i
    length = 0
    length_limit = ilen - i
    while length < length_limit and s[i + length] == s[match_start + length]:
      length += 1
    if length >= MIN_LENGTH:
      if i - 1 >= literal_start:
        snappy_emit_literal(ofile, s, literal_start, i)
      snappy_emit_backref(ofile, i - match_start, length)
      i += length
      literal_start = i
    else:
      i += 1
  if i < ilen:
    snappy_emit_literal(ofile, s, literal_start, ilen)

with open(sys.argv[1], "rb") as ifile:
  with open(sys.argv[2], "wb") as ofile:
    ifile.seek(0, os.SEEK_END)
    encode_varint32(ofile, ifile.tell())
    ifile.seek(0, os.SEEK_SET)
    while True:
      s = ifile.read(N)
      if not s:
        break
      snappy_compress_block_table(ofile, s, len(s))
