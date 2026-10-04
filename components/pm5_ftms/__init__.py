import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, esp32_ble_server
from esphome.const import CONF_ID

CODEOWNERS = ["@mvickstrom"]
DEPENDENCIES = ["ble_client", "esp32_ble_server"]

CONF_BLE_SERVER_ID = "ble_server_id"

pm5_ftms_ns = cg.esphome_ns.namespace("pm5_ftms")
PM5FTMSComponent = pm5_ftms_ns.class_(
    "PM5FTMSComponent", cg.Component, ble_client.BLEClientNode
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PM5FTMSComponent),
            cv.GenerateID(CONF_BLE_SERVER_ID): cv.use_id(esp32_ble_server.BLEServer),
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
