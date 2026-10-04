#include <cstdio>
#include <cstring>

#include "protocol.h"

using namespace pm5_proto;

static int failures = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                \
    }                                                            \
  } while (0)

static void test_decode() {
  RowState s;
  // elapsed 300.00s (30000 cs = 0x007530), distance 1234.5m (12345 dm = 0x003039)
  const uint8_t gen[] = {0x30, 0x75, 0x00, 0x39, 0x30, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  CHECK(decode_general_status(gen, sizeof(gen), s));
  CHECK(s.elapsed_cs == 30000);
  CHECK(s.distance_dm == 12345);

  // spm 24, hr 255 (invalid), pace 2:05.00 = 12500 cs
  const uint8_t add[] = {0, 0, 0, 0, 0, 24, 255, 0xD4, 0x30, 0, 0, 0, 0, 0, 0, 0, 0};
  CHECK(decode_additional_status(add, sizeof(add), s));
  CHECK(s.stroke_rate_spm == 24);
  CHECK(s.heart_rate == 0);
  CHECK(s.pace_cs_per_500 == 12500);

  const uint8_t add2[] = {0, 0, 0, 0, 180, 0, 50, 0, 0, 0};
  CHECK(decode_additional_status2(add2, sizeof(add2), s));
  CHECK(s.avg_power_w == 180);
  CHECK(s.calories == 50);

  const uint8_t stroke2[] = {0, 0, 0, 200, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0};
  CHECK(decode_additional_stroke_data(stroke2, sizeof(stroke2), s));
  CHECK(s.power_w == 200);
  CHECK(s.stroke_count == 10);

  CHECK(!decode_general_status(gen, 3, s));
}

static void test_encode() {
  RowState s;
  s.elapsed_cs = 30000;
  s.distance_dm = 12345;
  s.stroke_rate_spm = 24;
  s.stroke_count = 10;
  s.pace_cs_per_500 = 12000;
  s.power_w = 200;
  s.avg_power_w = 180;
  s.calories = 50;
  uint8_t out[32];
  size_t n = encode_rower_data(s, out);
  // Same bytes the Python bridge produced for these values.
  const uint8_t expect[] = {0x6c, 0x09, 0x30, 0x0a, 0x00, 0xd2, 0x04, 0x00, 0x78, 0x00, 0xc8,
                            0x00, 0xb4, 0x00, 0x32, 0x00, 0xff, 0xff, 0xff, 0x2c, 0x01};
  CHECK(n == sizeof(expect));
  CHECK(std::memcmp(out, expect, sizeof(expect)) == 0);

  s.heart_rate = 140;
  n = encode_rower_data(s, out);
  CHECK(n == sizeof(expect) + 1);
  CHECK((out[1] & 0x02) != 0);  // HR flag (bit 9)
  CHECK(out[19] == 140);

  clear_live_fields(s);
  CHECK(s.power_w == 0 && s.stroke_rate_spm == 0 && s.pace_cs_per_500 == 0);
  CHECK(s.distance_dm == 12345);
}

int main() {
  test_decode();
  test_encode();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
