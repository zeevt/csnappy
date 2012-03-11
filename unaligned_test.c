#include <stdint.h>

struct __attribute__((__packed__)) una_u32 { uint32_t x; };

static inline uint32_t read_aligned_uint32(const void *p) {
  const uint32_t *ptr = (const uint32_t *)p;
  return *ptr;
}

static inline uint32_t read_unaligned_ps_uint32(const void *p) {
  const struct una_u32 *ptr = (const struct una_u32 *)p;
  return ptr->x;
}

static inline uint32_t read_unaligned_bsle_uint32(const void *p) {
  const uint8_t *b = (const uint8_t *)p;
  return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

static inline void write_aligned_uint32(void *p, uint32_t v) {
  uint32_t *ptr = (uint32_t *)p;
  *ptr = v;
}

static inline void write_unaligned_ps_uint32(void *p, uint32_t v) {
  struct una_u32 *ptr = (struct una_u32 *)p;
  ptr->x = v;
}

static inline void write_unaligned_bsle_uint32(void *p, uint32_t v) {
  uint8_t *b = (uint8_t *)p;
  b[0] = v & 0xff;
  b[1] = (v >> 8) & 0xff;
  b[2] = (v >> 16) & 0xff;
  b[3] = (v >> 24) & 0xff;
}

#include <endian.h>
#include <byteswap.h>

static const uint32_t wordmask[] = {
  0u, 0xffu, 0xffffu, 0xffffffu, 0xffffffffu
};

uint32_t get_unaligned_le_x86(const void *p, uint32_t n) {
  uint32_t ret = *(const uint32_t *)p & wordmask[n];
  return ret;
}

uint32_t get_unaligned_le_v1(const void *p, uint32_t n) {
  const uint8_t *b = (const uint8_t *)p;
  uint32_t ret;
  ret = b[0];
  if (n > 1) {
    ret |= b[1] << 8;
    if (n > 2) {
      ret |= b[2] << 16;
      if (n > 3) {
        ret |= b[3] << 24;
      }
    }
  }
  return ret;
}

uint32_t get_unaligned_le_v2(const void *p, uint32_t n) {
  const uint8_t *b = (const uint8_t *)p;
  uint32_t ret = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
  ret &= wordmask[n];
  return ret;
}

uint32_t get_unaligned_le_v4(const void *p, uint32_t n) {
  const uint8_t *b = (const uint8_t *)p;
  uint32_t ret;
  int mask1 = -(n>1); /* 0 if n<=1, 0xFF otherwise */
  int mask2 = -(n>2);
  int mask3 = -(n>3);
  ret = b[0];
  ret |= (b[1] << 8) & mask1;
  ret |= (b[2] << 16) & mask2;
  ret |= (b[3] << 24) & mask3;
  return ret;
}

uint32_t get_unaligned_le_v5(const void *p, uint32_t n) {
  uint32_t mask = (1U << (8 * n)) - 1;
  uint32_t ret = *(const uint32_t *)p & mask;
  return ret;
}

extern uint32_t get_unaligned_le_armv5(const void *p, uint32_t n);

#include <stdlib.h>
#include <memory.h>
#include <string.h>

int main(int argc, char *argv[]) {
  if (argc < 2)
    return 2;
  const unsigned int data_size = 1000000;
  uint8_t *data = malloc(data_size);
  if (!data)
    return 0;
  uint32_t x, i;
  for (i = 0; i < data_size; i++)
    data[i] = i & 255;
  const uint8_t *p = data, * const end = data + data_size - 10;

#define TEST_LOOP(func)        \
  for (i = 0; i < 100; i++) {  \
    x = 0xffffffff;            \
    p = data;                  \
    while (p < end) {          \
      x ^= func(p, 1); p += 1; \
      x ^= func(p, 2); p += 2; \
      x ^= func(p, 3); p += 3; \
      x ^= func(p, 4); p += 4; \
    }                          \
  }

  switch (argv[1][0] - '0') {
    case 0:
      TEST_LOOP(get_unaligned_le_v1);
      break;
    case 1:
      TEST_LOOP(get_unaligned_le_v2);
      break;
    case 2:
      TEST_LOOP(get_unaligned_le_v4);
      break;
    case 3:
      TEST_LOOP(get_unaligned_le_armv5);
      break;
    case 4:
      TEST_LOOP(get_unaligned_le_x86);
      break;
    case 5:
      TEST_LOOP(get_unaligned_le_v5);
      break;
    default:
      goto out;
  }
out:
  free(data);
  return 0;
}
