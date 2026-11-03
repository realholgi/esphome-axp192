"""Native display backlight switch for AXP192."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID, CONF_RESTORE_VALUE, CONF_INITIAL_VALUE

from .sensor import AXP192Component, axp192_ns

CONF_AXP192_ID = "axp192_id"

AXP192DisplaySwitch = axp192_ns.class_(
    "AXP192DisplaySwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = switch.switch_schema(
    AXP192DisplaySwitch,
    icon="mdi:monitor",
).extend({
    cv.GenerateID(CONF_AXP192_ID): cv.use_id(AXP192Component),
    cv.Optional(CONF_INITIAL_VALUE, default=True): cv.boolean,
    cv.Optional(CONF_RESTORE_VALUE, default=True): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_AXP192_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_initial_value(config[CONF_INITIAL_VALUE]))
    cg.add(var.set_restore_value(config[CONF_RESTORE_VALUE]))
