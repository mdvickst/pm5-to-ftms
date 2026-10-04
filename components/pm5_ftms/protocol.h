#pragma once
// Pure PM5 -> FTMS translation, no ESPHome deps (unit-tested on the host).
#include <cstddef>
#include <cstdint>

namespace pm5_proto {

struct RowState {
  uint32_t elapsed_cs{0};      // centiseconds
  uint32_t distance_dm{0};     // decimeters
  uint8_t stroke_rate_spm{0};
  uint16_t stroke_count{0};
  uint16_t pace_cs_per_500{0};  // centiseconds per 500m
  int16_t power_w{0};
  uint16_t avg_power_w{0};
  uint16_t calories{0};
  uint8_t heart_rate{0};  // 0 = unknown
};

// PM5 rowing service notifications (CE06003x). Return false if too short.
bool decode_general_status(const uint8_t *d, size_t len, RowState &s);         // 0x0031
bool decode_additional_status(const uint8_t *d, size_t len, RowState &s);      // 0x0032
bool decode_additional_status2(const uint8_t *d, size_t len, RowState &s);     // 0x0033
bool decode_additional_stroke_data(const uint8_t *d, size_t len, RowState &s);  // 0x0036

// Zero the "instantaneous" fields when the rower stops sending.
void clear_live_fields(RowState &s);

// FTMS Rower Data (0x2AD1). `out` must hold >= 32 bytes. Returns length.
size_t encode_rower_data(const RowState &s, uint8_t *out);

// FTMS Fitness Machine Feature (0x2ACC), 8 bytes.
void encode_feature(uint8_t *out);

// Advertising service data: FTMS UUID, flags (available), machine type (rower).
static constexpr uint8_t FTMS_SERVICE_DATA[] = {0x26, 0x18, 0x01, 0x10, 0x00};

}  // namespace pm5_proto
