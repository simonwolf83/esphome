import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32_camera import ESP32Camera
from esphome.components.http_request import CONF_HTTP_REQUEST_ID, HttpRequestComponent
from esphome.const import CONF_ID

# In neueren ESPHome-Versionen sind diese Keys nicht mehr in esphome.const:
CONF_HOST = "host"
CONF_PASSWORD = "password"

DEPENDENCIES = ["esp32_camera", "network", "http_request", "json"]

# Add PlatformIO Arduino library paths
AUTO_LOAD = ["esp32"]

CONF_POLL_INTERVAL = "poll_interval"
CONF_SAVE_TO_SD = "save_to_sd"
CONF_WEBHOOK_URL = "webhook_url"
CONF_LAYER_TOLERANCE = "layer_tolerance"
CONF_CAMERA = "camera"

prusa_ns = cg.esphome_ns.namespace("prusa_link_timelapse")
PrusaLinkTimelapse = prusa_ns.class_("PrusaLinkTimelapse", cg.PollingComponent)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PrusaLinkTimelapse),
        cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(HttpRequestComponent),
        cv.Required(CONF_CAMERA): cv.use_id(ESP32Camera),
        cv.Required(CONF_HOST): cv.string_strict,
        cv.Required(CONF_PASSWORD): cv.string_strict,
        cv.Optional(
            CONF_POLL_INTERVAL, default="2s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SAVE_TO_SD, default=False): cv.boolean,
        cv.Optional(CONF_WEBHOOK_URL): cv.string_strict,
        cv.Optional(CONF_LAYER_TOLERANCE, default=0.01): cv.float_range(min=0.0),
    }
).extend(cv.polling_component_schema("2s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_password(config[CONF_PASSWORD]))
    cg.add(var.set_layer_tolerance(config[CONF_LAYER_TOLERANCE]))
    cg.add(var.set_save_to_sd(config[CONF_SAVE_TO_SD]))
    if CONF_WEBHOOK_URL in config:
        cg.add(var.set_webhook_url(config[CONF_WEBHOOK_URL]))
    if config[CONF_SAVE_TO_SD]:
        # SDMMC in 4-Bit-Modus (AI Thinker ESP32-CAM Pinbelegung)
        cg.add_build_flag("-DCONFIG_SDMMC_DEFAULT_SLOT_WIDTH=4")
        # Enable Arduino SD_MMC library
        cg.add_library("SD_MMC", None)

    # Get the http_request component
    http_request = await cg.get_variable(config[CONF_HTTP_REQUEST_ID])
    cg.add(var.set_http_request(http_request))

    # Get the camera component
    camera = await cg.get_variable(config[CONF_CAMERA])
    cg.add(var.set_camera(camera))
