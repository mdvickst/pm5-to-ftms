#pragma once
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_server/ble_characteristic.h"
#include "esphome/components/esp32_ble_server/ble_server.h"
#include "esphome/core/component.h"

#include "protocol.h"

#ifdef USE_ESP32

namespace esphome {
namespace pm5_ftms {

namespace espbt = esphome::esp32_ble_tracker;

// Connects to a Concept2 PM5 (via ble_client) and republishes its data as a
// Bluetooth FTMS rower (via esp32_ble_server).
//
// The PM5 never sleeps while something is connected to it, so the PM5 link
// follows the FTMS side: connect when an app (Peloton) connects to us, drop
// when it disconnects, and also drop after `idle_timeout` without rowing
// progress. After an idle drop, wait `reconnect_holdoff` (long enough for
// the PM5 to fall asleep) before listening for it again, so the next time
// it's woken up we reconnect.
class PM5FTMSComponent : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  // Builds the FTMS GATT service on the server; called from codegen.
  void set_server(esp32_ble_server::BLEServer *server);
  void set_idle_timeout(uint32_t ms) { this->idle_timeout_ms_ = ms; }
  void set_reconnect_holdoff(uint32_t ms) { this->reconnect_holdoff_ms_ = ms; }
  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }

 protected:
  void on_control_point_write_(std::span<const uint8_t> value);
  void set_pm5_enabled_(bool enabled, const char *reason);
  void manage_connection_(uint32_t now);
  void set_pm5_connected_(bool connected);

  pm5_proto::RowState state_{};
  uint32_t last_data_ms_{0};
  uint32_t last_notify_ms_{0};
  bool pm5_connected_{false};
  bool advertising_requested_{false};

  // Connection management (see class comment).
  uint32_t idle_timeout_ms_{600000};
  uint32_t reconnect_holdoff_ms_{900000};
  uint8_t app_clients_{0};
  bool initialized_{false};
  bool idle_dropped_{false};
  uint32_t idle_dropped_ms_{0};
  uint32_t last_progress_ms_{0};
  uint32_t last_distance_dm_{0};
  uint16_t last_stroke_count_{0};
  binary_sensor::BinarySensor *connected_sensor_{nullptr};

  // PM5 notification handles we registered for.
  uint16_t h_general_{0}, h_additional_{0}, h_additional2_{0}, h_stroke2_{0};
  uint8_t pending_regs_{0};

  esp32_ble_server::BLECharacteristic *rower_data_{nullptr};
  esp32_ble_server::BLECharacteristic *control_point_{nullptr};
};

}  // namespace pm5_ftms
}  // namespace esphome

#endif
