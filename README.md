# pm5-to-ftms

Bridges a Concept2 PM5 (Bluetooth) to a standard Bluetooth **FTMS rower** so apps that only speak FTMS (e.g. Peloton Row classes on a tablet/phone) can see your stroke rate, distance, pace, power, calories, heart rate, and elapsed time.

```
PM5 ──BLE──▶ [this computer: central + peripheral] ──BLE FTMS──▶ Peloton app
```

## Setup

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
```

- **macOS:** the first run asks for Bluetooth permission for your terminal app. Allow it in System Settings → Privacy & Security → Bluetooth.
- **Linux:** needs BlueZ ≥ 5.50 and an adapter that can be central and peripheral at the same time (most can). Run it as a user in the `bluetooth` group.

## Run

1. Wake the PM5. It advertises while the main menu is showing. Don't pair it with any other app.
2. `python pm5_ftms.py`
3. In the Peloton app, add a rower/FTMS device and pick **"PM5 Row"**.

Options: `--address <PM5 addr/UUID>`, `--name <advertised name>`, `--simulate` (fake data, no PM5 needed), `-v`.

Try `--simulate` first to check that the Peloton side pairs before you bring in the rower.

## ESP32 / ESPHome version

`components/pm5_ftms` is an ESPHome external component that does the same bridging on an ESP32, and also adds the FTMS "rower" service data to the broadcast, which macOS can't send. See [example/weather-bridge-with-pm5.yaml](example/weather-bridge-with-pm5.yaml) for it running alongside the Acurite weather bridge.

```yaml
esp32_ble:
  name: "PM5 Row"
esp32_ble_tracker:
esp32_ble_server:
ble_client:
  - mac_address: "D1:23:45:67:89:AB"   # your PM5
    id: pm5
pm5_ftms:
  ble_client_id: pm5
```

Host unit tests for the protocol layer: `make test`.
