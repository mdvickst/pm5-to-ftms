#pragma once
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

 protected:
  void on_control_point_write_(std::span<const uint8_t> value);

  pm5_proto::RowState state_{};
  uint32_t last_data_ms_{0};
  uint32_t last_notify_ms_{0};
  bool pm5_connected_{false};

  // PM5 notification handles we registered for.
  uint16_t h_general_{0}, h_additional_{0}, h_additional2_{0}, h_stroke2_{0};
  uint8_t pending_regs_{0};

  esp32_ble_server::BLECharacteristic *rower_data_{nullptr};
  esp32_ble_server::BLECharacteristic *control_point_{nullptr};
};

}  // namespace pm5_ftms
}  // namespace esphome

#endif
