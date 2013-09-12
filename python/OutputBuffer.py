# -*- coding: utf-8 -*-
from array import array

class OutputBuffer(object):
  """A ring buffer that holds 32K of the last output,
     flushing a page at a time until flush() is called."""
  def __init__(self, f, checksummer):
    self.f = f
    self.checksummer = checksummer
    a_list = [0] * 4096
    self.pages = [array('B', a_list) for i in xrange(9)]
    self.top_page_index, self.top_offset, self.isize, self.checksum = 0, 0, 0, 0
  def _flush_page(self):
    self.checksum = self.checksummer(self.pages[0], self.checksum)
    self.isize += 4096
    self.pages[0].tofile(self.f)
    self.pages.append(self.pages.pop(0))
    #for i in xrange(4096): self.pages[-1][i] = '\x00' # DEBUG
  def _maybe_flush_page(self):
    if self.top_offset == 4096:
      self.top_offset = 0
      if self.top_page_index == len(self.pages) - 1:
        self._flush_page()
      else:
        self.top_page_index += 1
  def put_byte(self, b):
    self.pages[self.top_page_index][self.top_offset] = b
    self.top_offset += 1
    self._maybe_flush_page()
  def flush(self):
    self.top_offset -= 1
    if self.top_offset < 0:
      self.top_offset = 4095
      self.top_page_index -= 1
    while self.top_page_index > 0:
      self._flush_page()
      self.top_page_index -= 1
    data = self.pages[self.top_page_index][:self.top_offset+1]
    self.checksum = self.checksummer(data, self.checksum)
    self.isize += self.top_offset + 1
    self.f.write(data.tostring())
    return (self.checksum & 0xffffffff, self.isize)
  def num_bufferred_bytes(self):
    return (self.top_page_index << 12) + self.top_offset
  def get_remaining_space(self):
    return 4096 - self.top_offset
  def put_bytes(self, bytes):
    curr_i = 0
    len_i = len(bytes)
    while curr_i < len_i:
      length = min(4096 - self.top_offset, len_i - curr_i)
      self.pages[self.top_page_index][self.top_offset : self.top_offset+length] = \
                                       array('B', bytes[curr_i : curr_i+length])
      curr_i += length
      self.top_offset += length
      self._maybe_flush_page()
  def repeat_chunk(self, length, distance_back):
    begin = (self.top_page_index << 12) + self.top_offset - distance_back
    if begin < 0:
      raise ValueError("distance back is too big. pos [%d %d], distance: %d" % \
                               (self.top_page_index, self.top_offset, distance))
    input_page_index, input_offset = begin >> 12, begin & 4095
    while True:
      input_page, output_page = self.pages[input_page_index], self.pages[self.top_page_index]
      remaining_space, input_to_end_of_page = 4096 - self.top_offset, 4096 - input_offset
      bytes_to_copy = min(length, remaining_space, input_to_end_of_page)
      if distance_back == 1:
        output_page[self.top_offset : self.top_offset + bytes_to_copy] = \
                         array('B', (input_page[input_offset],)) * bytes_to_copy
      else:
        for i in xrange(bytes_to_copy):
          output_page[self.top_offset + i] = input_page[input_offset + i]
      input_offset += bytes_to_copy
      if input_offset == 4096:
        input_offset = 0
        input_page_index += 1
      self.top_offset += bytes_to_copy
      if self.top_offset == 4096:
        self.top_offset = 0
        if self.top_page_index == len(self.pages) - 1:
          self._flush_page()
          input_page_index -= 1
        else:
          self.top_page_index += 1
      length -= bytes_to_copy
      if length == 0: break
