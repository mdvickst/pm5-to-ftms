#include "protocol.h"

namespace pm5_proto {

static uint32_t u24(const uint8_t *b) { return b[0] | (b[1] << 8) | (uint32_t(b[2]) << 16); }
static uint16_t u16(const uint8_t *b) { return b[0] | (b[1] << 8); }

bool decode_general_status(const uint8_t *d, size_t len, RowState &s) {
  if (len < 6)
    return false;
  s.elapsed_cs = u24(d);
  s.distance_dm = u24(d + 3);
  return true;
}

bool decode_additional_status(const uint8_t *d, size_t len, RowState &s) {
  if (len < 11)
    return false;
  s.stroke_rate_spm = d[5];
  s.heart_rate = d[6] == 255 ? 0 : d[6];
  s.pace_cs_per_500 = u16(d + 7);
  return true;
}

bool decode_additional_status2(const uint8_t *d, size_t len, RowState &s) {
  if (len < 8)
    return false;
  s.avg_power_w = u16(d + 4);
  s.calories = u16(d + 6);
  return true;
}

bool decode_additional_stroke_data(const uint8_t *d, size_t len, RowState &s) {
  if (len < 9)
    return false;
  s.power_w = static_cast<int16_t>(u16(d + 3));
  s.stroke_count = u16(d + 7);
  return true;
}

void clear_live_fields(RowState &s) {
  s.stroke_rate_spm = 0;
  s.power_w = 0;
  s.pace_cs_per_500 = 0;
}

static void put16(uint8_t *&p, uint16_t v) {
  *p++ = v & 0xFF;
  *p++ = v >> 8;
}

size_t encode_rower_data(const RowState &s, uint8_t *out) {
  // bit0 = 0 -> stroke rate + count present; total distance, inst pace,
  // inst power, avg power, expended energy, elapsed time; HR when known.
  uint16_t flags = (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 11);
  if (s.heart_rate)
    flags |= 1 << 9;
  uint8_t *p = out;
  put16(p, flags);
  uint16_t rate_half = uint16_t(s.stroke_rate_spm) * 2;
  *p++ = rate_half > 255 ? 255 : rate_half;
  put16(p, s.stroke_count);
  uint32_t dist_m = s.distance_dm / 10;
  *p++ = dist_m & 0xFF;
  *p++ = (dist_m >> 8) & 0xFF;
  *p++ = (dist_m >> 16) & 0xFF;
  put16(p, s.pace_cs_per_500 / 100);
  put16(p, static_cast<uint16_t>(s.power_w));
  put16(p, s.avg_power_w);
  put16(p, s.calories);
  put16(p, 0xFFFF);  // energy per hour: n/a
  *p++ = 0xFF;       // energy per minute: n/a
  if (s.heart_rate)
    *p++ = s.heart_rate;
  uint32_t secs = s.elapsed_cs / 100;
  put16(p, secs > 0xFFFF ? 0xFFFF : secs);
  return p - out;
}

void encode_feature(uint8_t *out) {
  // total distance(2), pace(5), expended energy(9), heart rate(10),
  // elapsed time(12), power measurement(14). No target settings.
  uint32_t f = (1u << 2) | (1u << 5) | (1u << 9) | (1u << 10) | (1u << 12) | (1u << 14);
  for (int i = 0; i < 4; i++)
    out[i] = (f >> (8 * i)) & 0xFF;
  for (int i = 4; i < 8; i++)
    out[i] = 0;
}

}  // namespace pm5_proto
