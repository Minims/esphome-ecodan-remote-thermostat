import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, uart
import esphome.final_validate as fv
from esphome.core import CORE
from esphome.const import CONF_ID, CONF_UART_ID, CONF_RX_PIN, CONF_TX_PIN, CONF_NUMBER

CODEOWNERS = ["@gekkekoe"]

AUTO_LOAD = ["climate", "binary_sensor", "sensor", "text_sensor", "uart"]

CONF_ECODAN_ID = "ecodan_id"
CONF_PROXY_UART_ID = "proxy_uart_id"
CONF_PROXY_MODE = "proxy_mode"
CONF_PROXY_CONTROLLER = "proxy_controller"
CONF_PROXY_OVERRIDE_TIMEOUT = "proxy_override_timeout"

uart_ns = cg.esphome_ns.namespace("uart")
UARTComponent = uart_ns.class_("UARTComponent")

hub_ns = cg.esphome_ns.namespace('ecodan')

ECODAN = hub_ns.class_('EcodanHeatpump', cg.PollingComponent)
ECODAN_CLIMATE = hub_ns.class_('EcodanClimate', climate.Climate, cg.PollingComponent, uart.UARTDevice)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(ECODAN),
        cv.Optional(CONF_PROXY_UART_ID): cv.use_id(UARTComponent),
        cv.Optional(CONF_PROXY_MODE, default=False): cv.boolean,
        cv.Optional(CONF_PROXY_CONTROLLER, default=1): cv.int_range(min=1, max=8),
        cv.Optional(CONF_PROXY_OVERRIDE_TIMEOUT, default="10min"): cv.All(
            cv.positive_not_null_time_period, cv.positive_time_period_milliseconds
        ),
    }
    ).extend(cv.polling_component_schema('3000ms')
    .extend(uart.UART_DEVICE_SCHEMA))


def validate_proxy(config):
    if not config[CONF_PROXY_MODE]:
        return config
    if not CORE.is_esp32 or CORE.target_framework != "esp-idf":
        raise cv.Invalid("Le mode proxy exige ESP32 avec ESP-IDF")
    if CONF_PROXY_UART_ID not in config:
        raise cv.Invalid("proxy_uart_id est requis en mode proxy")
    if config[CONF_UART_ID] == config[CONF_PROXY_UART_ID]:
        raise cv.Invalid("Le proxy exige deux UART distinctes")
    if config[CONF_PROXY_OVERRIDE_TIMEOUT].total_milliseconds > 2147483647:
        raise cv.Invalid("proxy_override_timeout ne peut pas dépasser 24 jours")
    full = fv.full_config.get()
    proxy_pins = []
    for key in (CONF_UART_ID, CONF_PROXY_UART_ID):
        uart.final_validate_device_schema(
            "ecodan proxy", uart_bus=key, baud_rate=2400,
            require_tx=True, require_rx=True, data_bits=8, parity="EVEN", stop_bits=1,
        )(config)
        bus = full.get_config_for_path(full.get_path_for_id(config[key])[:-1])
        if "debug" in bus:
            raise cv.Invalid("Le proxy possède ses UART : retirer debug")
        if not bus[CONF_ID].type.inherits_from(uart.IDFUARTComponent):
            raise cv.Invalid("Le proxy exige des UART matérielles ESP-IDF")
        for pin in (CONF_TX_PIN, CONF_RX_PIN):
            if bus[pin].get("inverted", False):
                raise cv.Invalid("Le proxy ne prend pas en charge des UART inversées")
            proxy_pins.append(bus[pin][CONF_NUMBER])
    config["_proxy_pins"] = proxy_pins
    return config


FINAL_VALIDATE_SCHEMA = validate_proxy


async def to_code(config):
    hp = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(hp, config)
    await uart.register_uart_device(hp, config)
    if proxy_uart_id := config.get(CONF_PROXY_UART_ID):
        proxy_uart = await cg.get_variable(proxy_uart_id)
        cg.add(hp.set_proxy_uart(proxy_uart))

    if config[CONF_PROXY_MODE]:
        cg.add_define("USE_ECODAN_CNRF_PROXY")
        cg.add(hp.set_cnrf_proxy(config[CONF_PROXY_CONTROLLER], *config["_proxy_pins"],
                                 config[CONF_PROXY_OVERRIDE_TIMEOUT].total_milliseconds))
