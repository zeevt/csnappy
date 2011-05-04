import os, sys
from struct import pack as struct_pack
from array import array

def encode_varint32(f, n):
  while n:
    b = n & 127
    n >>= 7
    if n:
      b |= 128
    f.write(chr(b))

def snappy_emit_literal(f, s, begin, end):
  l = end - begin - 1
  if l < 60:
    f.write(chr(l << 2))
  else:
    if l < 256:
      f.write(chr(60 << 2))
      f.write(chr(l))
    else: # a length > 65536 is not supported by this encoder!
      f.write(chr(61 << 2))
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
MIN_LENGTH = 4 # snappy does not support back references with length < 4

def process_block(ofile, s, ilen):
  literal_start = 0
  i = 1
  while i < ilen - MIN_LENGTH:
    max_length = 0
    offset = 0
    j = i - 1
    while j >= 0: # technically, j >= max(0, i - N)
      length = 0
      length_limit = ilen - i
      while length < length_limit and s[i+length] == s[j+length]:
        length += 1
      if length >= MIN_LENGTH and length > max_length:
        max_length = length
        offset = i - j
      j -= 1
    if max_length:
      if i - 1 >= literal_start:
        snappy_emit_literal(ofile, s, literal_start, i)
      snappy_emit_backref(ofile, offset, max_length)
      i += max_length
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
    a = array('B')
    while True:
      try:
        a.fromfile(ifile, N)
      except EOFError:
        pass
      ilen = len(a)
      if not ilen:
        break
      process_block(ofile, a, ilen)
      del a[:]
