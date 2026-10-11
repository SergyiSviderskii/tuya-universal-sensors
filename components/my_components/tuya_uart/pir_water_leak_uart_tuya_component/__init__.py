# pir, water-leak (tywe3s) (cbu)

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, binary_sensor, sensor  # Добавляем импорт binary_sensor
from esphome.const import CONF_ID

DEPENDENCIES = ['uart']

pir_water_leak_uart_tuya_component_ns = cg.esphome_ns.namespace('pir_water_leak_uart_tuya_component')
TuComponent = pir_water_leak_uart_tuya_component_ns.class_('TuComponent', cg.Component, uart.UARTDevice)

# Это имя ключа, которое мы будем писать в основном YAML-файле
CONF_PIR_LEAK_SENSOR = "pir_leak_sensor"
CONF_PIR_LEAK_BATTERY = "pir_leak_battery"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(TuComponent),
    # Говорим ESPHome, что в YAML этот параметр обязателен
    cv.Required(CONF_PIR_LEAK_SENSOR): cv.use_id(binary_sensor.BinarySensor),
    cv.Required(CONF_PIR_LEAK_BATTERY): cv.use_id(sensor.Sensor),
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield uart.register_uart_device(var, config)
    
    # Забираем датчик из конфига по нашему объявленному ключу
    sens = yield cg.get_variable(config[CONF_PIR_LEAK_SENSOR])
    # Передаем указатель в метод C++ класса
    cg.add(var.set_pir_leak_binary_sensor(sens))
    
    # Забираем датчик из конфига по нашему объявленному ключу
    sens = yield cg.get_variable(config[CONF_PIR_LEAK_BATTERY])
    # Передаем указатель в метод C++ класса
    cg.add(var.set_pir_leak_battery(sens))    
