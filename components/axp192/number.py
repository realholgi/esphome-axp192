"""AXP192 native display-brightness number platform."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID, CONF_INITIAL_VALUE, CONF_RESTORE_VALUE

from .sensor import AXP192Component, axp192_ns

CONF_AXP192_ID = "axp192_id"

AXP192BrightnessNumber = axp192_ns.class_(
    "AXP192BrightnessNumber", number.Number, cg.Component
)

CONFIG_SCHEMA = number.number_schema(
    AXP192BrightnessNumber,
    icon="mdi:brightness-6",
    unit_of_measurement="%",
).extend(
    {
        cv.GenerateID(CONF_AXP192_ID): cv.use_id(AXP192Component),
        cv.Optional(CONF_INITIAL_VALUE, default=100.0): cv.float_range(
            min=0.0, max=100.0
        ),
        cv.Optional(CONF_RESTORE_VALUE, default=True): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await number.register_number(var, config, min_value=0, max_value=100, step=1)

    parent = await cg.get_variable(config[CONF_AXP192_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_initial_value(config[CONF_INITIAL_VALUE]))
    cg.add(var.set_restore_value(config[CONF_RESTORE_VALUE]))
