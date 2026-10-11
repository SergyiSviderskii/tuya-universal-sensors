// pir, water-leak (tywe3s) (cbu)
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
//#include "esphome/components/wifi/wifi_component.h"
#include <vector>
#include <string>
// ЖИЗНЕННО НЕОБХОДИМЫЙ ВКЛЮЧАТЕЛЬ ДЛЯ КЛАССА BinarySensor:
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"

#if defined(ESPHOME_LOG_LEVEL) && (ESPHOME_LOG_LEVEL >= 1)
#define ENABLE_DIAGNOSTICS 
#endif

namespace esphome {
namespace pir_water_leak_uart_tuya_component {

enum class TuyaDatapointType : uint8_t {
  INTEGER = 0x02,  // 4 byte  
  ENUM = 0x04,     // 1 byte
};

struct TuyaDatapoint {
  uint8_t id;
  TuyaDatapointType type;
  size_t len;
  union {
    uint32_t value_uint;
    uint8_t value_enum;
  };
};  

enum class TuyaCommandType : uint8_t {
  HEARTBEAT = 0x00,
  PRODUCT_QUERY = 0x01,
  CONF_QUERY = 0x02,
  WIFI_RESET = 0X03,
  WIFI_OTA = 0x04,
  DATAPOINT_REPORT = 0x05,
  DATAPOINT_QUERY = 0x07,
  WIFI_STREAM_STATUS = 0x10
};  

enum class TuyaInitState : uint8_t {
  INIT_HEARTBEAT = 0x00,
  INIT_PRODUCT,
  INIT_CONF,
  INIT_WIFI,
  INIT_WIFI_OTA,
  INIT_DATAPOINT,
  INIT_WIFI_RESET,
  INIT_QUERY,
  INIT_QUERY_MALFUN,
  INIT_DONE,
  INIT_WIFI_STREAM_STATUS  
};

struct TuyaCommand {
  TuyaCommandType cmd;
  std::vector<uint8_t> payload;
}; 

#ifdef ENABLE_DIAGNOSTICS
struct UartFrame {
  TuyaInitState g_init = TuyaInitState::INIT_HEARTBEAT;
  uint32_t g_time = 0;
  std::string g_inout;
  std::vector<uint8_t> g_delay;
};
#endif 

class TuComponent : public Component, public uart::UARTDevice {
  public:
    void setup() override;
    void loop() override;
   
    bool get_restart_swith_factory = false;
    optional<float>set_battery_adc {};
    
    bool set_flag_conect_wifi = false;
    bool set_flag_conect_mqtt = false;
    
    // Создаем методы, через который YAML передаст ссылку на датчики    
    void set_pir_leak_binary_sensor(binary_sensor::BinarySensor *sensor) { 
      this->pir_leak_sensor_ptr_ = sensor; 
    }
    
    void set_pir_leak_battery(sensor::Sensor *sensor) { 
      this->battery_pir_leak_scale_prt_ = sensor; 
    }    
  
  protected: 
  
    // В секцию protected добавляем сам указатель
    binary_sensor::BinarySensor *pir_leak_sensor_ptr_{nullptr};
    sensor::Sensor *battery_pir_leak_scale_prt_ {nullptr};
    
    // Переменные вектора создаются только если рубильник включен
#ifdef ENABLE_DIAGNOSTICS
    std::vector<UartFrame> g_1_{};
    bool flag_ota_ = false;
    uint8_t flag_battery_ = 0;  
    uint32_t flag_battery_timestamp_ = 0;
    uint32_t timestamp_ota_ = 0;
    int final_percent_ = 0;
    TuyaInitState flag_battery_init_ = TuyaInitState::INIT_HEARTBEAT;
    //uint32_t keepalive_start_ms_{0};
    //bool keepalive_sent_{false}; 
    
  #if defined(USE_BK72XX) || defined(LT_BK7231N)
    bool flag_log_bk_ = false;    
  #endif  
  
#endif  

    TuyaInitState init_state_ = TuyaInitState::INIT_HEARTBEAT; 
    std::vector<uint8_t> rx_message_;
    std::vector<TuyaCommand> command_queue_;
    std::string product_ = "";
    optional<uint8_t> battery_pir_leak_ {};
    optional<TuyaCommandType> expected_response_{};
    uint32_t last_rx_char_timestamp_ = 0;
    uint32_t last_command_timestamp_ = 0;
    int init_retries_{0};
    bool init_failed_{false};
    bool flag_mqtt_ {false};

   void handle_char_(uint8_t c);
   void handle_datapoints_(const uint8_t *buffer, size_t len);  
   bool validate_message_();
   
   void handle_command_(uint8_t command, uint8_t version, const uint8_t *buffer, size_t len);
   void send_raw_command_(TuyaCommand command);
   void process_command_queue_();
   void send_command_(const TuyaCommand &command);
   void send_empty_command_(TuyaCommandType command);   
};

}  // namespace pir_water_leak_uart_tuya_component
}  // namespace esphome
