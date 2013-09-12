from OutputBuffer import OutputBuffer
from struct import unpack as struct_unpack

def read_varint32(f):
  v = 0
  offset = 0
  while True:
    c = f.read(1)
    if not c:
      raise ValueError("header is malformed")
    b = ord(c)
    v |= (b & 127) << offset
    offset += 7
    if b < 128:
      break
    if offset >= 32:
      raise ValueError("header is malformed")
  return v

def read_le_bytes(f, n):
  s = f.read(n)
  if len(s) < n:
    raise ValueError("data malformed")
  if n == 1:
    return ord(s)
  elif n == 2:
    return struct_unpack("<H", s)[0]
  elif n == 3:
    return ord(s[0]) | (ord(s[1]) << 8) | (ord(s[2]) << 16)
  elif n == 4:
    return struct_unpack("<I", s)[0]
  else:
    raise ValueError("number of bytes must be in range [1..4]")

def snappy_decompress(ifile, ob):
  expected_olen = read_varint32(ifile)
  while True:
    s = ifile.read(1)
    if not s:
      break
    c = ord(s)
    cmd_type = c & 3
    length = (c >> 2) + 1
    if cmd_type == 0:
      if length > 60:
        length = read_le_bytes(ifile, length - 60) + 1
      ob.put_bytes(ifile.read(length))
    else:
      if cmd_type == 1:
        length = ((length - 1) & 7) + 4
        offset = ((c >> 5) << 8) + read_le_bytes(ifile, 1)
      elif cmd_type == 2:
        offset = read_le_bytes(ifile, 2)
      else:
        offset = read_le_bytes(ifile, 4)
      ob.repeat_chunk(length, offset)
  if ob.isize + ob.num_bufferred_bytes() == expected_olen:
    ob.flush()
  else:
    raise ValueError("input not consumed")

if __name__ == "__main__":
  import sys
  with file(sys.argv[1], "rb") as ifile:
    with file(sys.argv[2], "wb") as ofile:
      ob = OutputBuffer(ofile, lambda x,y: y)
      snappy_decompress(ifile, ob)
