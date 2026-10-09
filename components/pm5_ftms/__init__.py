import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, ble_client, esp32_ble_server
from esphome.const import CONF_ID, DEVICE_CLASS_CONNECTIVITY

CODEOWNERS = ["@mvickstrom"]
DEPENDENCIES = ["ble_client", "esp32_ble_server"]
AUTO_LOAD = ["binary_sensor"]

CONF_BLE_SERVER_ID = "ble_server_id"
CONF_IDLE_TIMEOUT = "idle_timeout"
CONF_RECONNECT_HOLDOFF = "reconnect_holdoff"
CONF_PM5_CONNECTED = "pm5_connected"

pm5_ftms_ns = cg.esphome_ns.namespace("pm5_ftms")
PM5FTMSComponent = pm5_ftms_ns.class_(
    "PM5FTMSComponent", cg.Component, ble_client.BLEClientNode
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PM5FTMSComponent),
            cv.GenerateID(CONF_BLE_SERVER_ID): cv.use_id(esp32_ble_server.BLEServer),
            cv.Optional(
                CONF_IDLE_TIMEOUT, default="10min"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_RECONNECT_HOLDOFF, default="15min"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_PM5_CONNECTED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
            ),
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    server = await cg.get_variable(config[CONF_BLE_SERVER_ID])
    cg.add(var.set_server(server))
    cg.add(var.set_idle_timeout(config[CONF_IDLE_TIMEOUT].total_milliseconds))
    cg.add(var.set_reconnect_holdoff(config[CONF_RECONNECT_HOLDOFF].total_milliseconds))
    if CONF_PM5_CONNECTED in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_PM5_CONNECTED])
        cg.add(var.set_connected_sensor(sens))
