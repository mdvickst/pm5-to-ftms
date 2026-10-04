#include "pm5_ftms.h"

#ifdef USE_ESP32

#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/esp32_ble_server/ble_2902.h"
#include "esphome/components/esp32_ble_server/ble_service.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pm5_ftms {

static const char *const TAG = "pm5_ftms";

using esp32_ble_server::BLE2902;
using esp32_ble_server::BLECharacteristic;

static constexpr uint32_t NOTIFY_INTERVAL_MS = 250;
static constexpr uint32_t STALE_MS = 3000;

static espbt::ESPBTUUID c2_uuid(uint16_t short_id) {
  char buf[37];
  snprintf(buf, sizeof(buf), "ce06%04x-43e5-11e4-916c-0800200c9a66", short_id);
  return espbt::ESPBTUUID::from_raw(buf);
}

void PM5FTMSComponent::set_server(esp32_ble_server::BLEServer *server) {
  // 1 service + 4 chars * 2 + 3 CCCDs
  auto *svc = server->create_service(espbt::ESPBTUUID::from_uint16(0x1826), true, 12);

  auto *feature = svc->create_characteristic(espbt::ESPBTUUID::from_uint16(0x2ACC), BLECharacteristic::PROPERTY_READ);
  std::vector<uint8_t> fv(8);
  pm5_proto::encode_feature(fv.data());
  feature->set_value(std::move(fv));

  this->rower_data_ = svc->create_characteristic(espbt::ESPBTUUID::from_uint16(0x2AD1), BLECharacteristic::PROPERTY_NOTIFY);
  this->rower_data_->add_descriptor(new BLE2902());  // NOLINT(cppcoreguidelines-owning-memory)

  this->control_point_ = svc->create_characteristic(
      espbt::ESPBTUUID::from_uint16(0x2AD9), BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_INDICATE);
  this->control_point_->add_descriptor(new BLE2902());  // NOLINT(cppcoreguidelines-owning-memory)
  this->control_point_->on_write(
      [this](std::span<const uint8_t> value, uint16_t) { this->on_control_point_write_(value); });

  auto *status = svc->create_characteristic(espbt::ESPBTUUID::from_uint16(0x2ADA), BLECharacteristic::PROPERTY_NOTIFY);
  status->add_descriptor(new BLE2902());  // NOLINT(cppcoreguidelines-owning-memory)

  server->enqueue_start_service(svc);
}

void PM5FTMSComponent::setup() {
  // FTMS service data (machine type = rower) so apps filtering on it find us.
  esp32_ble::global_ble->advertising_set_service_data(
      std::vector<uint8_t>(std::begin(pm5_proto::FTMS_SERVICE_DATA), std::end(pm5_proto::FTMS_SERVICE_DATA)));
}

void PM5FTMSComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "PM5 -> FTMS bridge:\n  PM5 address: %s", this->parent()->address_str());
}

void PM5FTMSComponent::on_control_point_write_(std::span<const uint8_t> value) {
  if (value.empty())
    return;
  uint8_t op = value[0];
  ESP_LOGD(TAG, "FTMS control point op 0x%02X", op);
  if (op == 0x01)  // Reset
    this->state_ = pm5_proto::RowState{};
  // Response code: 0x80, request op, result success. The rower is driven by
  // the PM5, so we accept but ignore start/stop/target requests.
  this->control_point_->set_value({0x80, op, 0x01});
  this->control_point_->notify();
}

void PM5FTMSComponent::loop() {
  // A server whose services are created in code (not YAML) doesn't advertise
  // on its own in ESPHome 2026.9+, so ask for it once BLE is up.
  if (!this->advertising_requested_ && esp32_ble::global_ble->is_active()) {
    esp32_ble::global_ble->advertising_start();
    this->advertising_requested_ = true;
    ESP_LOGI(TAG, "Advertising FTMS rower");
  }

  uint32_t now = millis();
  if (now - this->last_notify_ms_ < NOTIFY_INTERVAL_MS)
    return;
  this->last_notify_ms_ = now;
  if (this->rower_data_ == nullptr)
    return;
  if (now - this->last_data_ms_ > STALE_MS)
    pm5_proto::clear_live_fields(this->state_);
  uint8_t buf[32];
  size_t len = pm5_proto::encode_rower_data(this->state_, buf);
  this->rower_data_->set_value(std::vector<uint8_t>(buf, buf + len));
  this->rower_data_->notify();
}

void PM5FTMSComponent::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                           esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status == ESP_GATT_OK)
        ESP_LOGI(TAG, "Connected to PM5");
      break;

    case ESP_GATTC_CLOSE_EVT:
      ESP_LOGW(TAG, "PM5 disconnected");
      this->pm5_connected_ = false;
      this->h_general_ = this->h_additional_ = this->h_additional2_ = this->h_stroke2_ = 0;
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto svc = c2_uuid(0x0030);
      auto handle_of = [&](uint16_t id) -> uint16_t {
        auto *chr = this->parent()->get_characteristic(svc, c2_uuid(id));
        return chr ? chr->handle : 0;
      };
      this->h_general_ = handle_of(0x0031);
      this->h_additional_ = handle_of(0x0032);
      this->h_additional2_ = handle_of(0x0033);
      this->h_stroke2_ = handle_of(0x0036);
      uint16_t h_rate = handle_of(0x0034);

      if (!this->h_general_ || !this->h_additional_ || !this->h_additional2_ || !this->h_stroke2_) {
        ESP_LOGE(TAG, "PM5 rowing service not found; is this a PM5?");
        break;
      }
      if (h_rate) {
        uint8_t rate = 3;  // 100 ms sample rate
        esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), h_rate, 1, &rate,
                                 ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
      }
      this->pending_regs_ = 0;
      for (uint16_t h : {this->h_general_, this->h_additional_, this->h_additional2_, this->h_stroke2_}) {
        if (esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), h) ==
            ESP_OK)
          this->pending_regs_++;
      }
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.status != ESP_GATT_OK)
        ESP_LOGW(TAG, "Notify registration failed for handle %d", param->reg_for_notify.handle);
      if (this->pending_regs_ > 0 && --this->pending_regs_ == 0) {
        this->node_state = espbt::ClientState::ESTABLISHED;
        this->pm5_connected_ = true;
        ESP_LOGI(TAG, "Subscribed to PM5 rowing data");
      }
      break;

    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.conn_id != this->parent()->get_conn_id())
        break;
      const uint8_t *d = param->notify.value;
      size_t len = param->notify.value_len;
      uint16_t h = param->notify.handle;
      bool ok = false;
      if (h == this->h_general_)
        ok = pm5_proto::decode_general_status(d, len, this->state_);
      else if (h == this->h_additional_)
        ok = pm5_proto::decode_additional_status(d, len, this->state_);
      else if (h == this->h_additional2_)
        ok = pm5_proto::decode_additional_status2(d, len, this->state_);
      else if (h == this->h_stroke2_)
        ok = pm5_proto::decode_additional_stroke_data(d, len, this->state_);
      if (ok)
        this->last_data_ms_ = millis();
      break;
    }

    default:
      break;
  }
}

}  // namespace pm5_ftms
}  // namespace esphome

#endif
