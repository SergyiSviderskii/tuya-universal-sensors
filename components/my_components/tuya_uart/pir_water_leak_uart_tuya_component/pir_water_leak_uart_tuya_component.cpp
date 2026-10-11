// pir, water-leak (tywe3s) (cbu)
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/components/wifi/wifi_component.h"
#include "pir_water_leak_uart_tuya_component.h"


#ifdef USE_ESP8266
extern "C" {
  #include <ets_sys.h>
  // Явно указываем компилятору сигнатуру функции из SDK
  void uart_tx_one_char(uint8_t uart_no, uint8_t data);
}
#define UART0 0
#endif


namespace esphome {
namespace pir_water_leak_uart_tuya_component {

static const char *const TAG = "tuya";
static const int COMMAND_DELAY = 10;
static const int RECEIVE_TIMEOUT = 300;
static const int MAX_RETRIES = 5;

    void  TuComponent::setup() {

#ifdef USE_ESP8266
      // 1. Отправляем 0x00 напрямую в регистр
      uart_tx_one_char(UART0, 0x00); 
      delay(20); // Используем delay() вместо delayMicroseconds

      // 2. Отправляем 0xFF напрямую в регистр
      uart_tx_one_char(UART0, 0xFF); 
      delay(10);
#endif     
      
#if defined(USE_BK72XX) || defined(LT_BK7231N)
      const uint8_t bk_tx_pin = 11; 
      
      // 1. Удерживаем линию, выжигая мусор загрузчика Beken
      pinMode(bk_tx_pin, OUTPUT);
      digitalWrite(bk_tx_pin, LOW);  
      delay(100);                    
    
      digitalWrite(bk_tx_pin, HIGH); 
      delay(20);                     
    
      // 2. Вместо перевода в INPUT возвращаем пин в режим аппаратного UART!
      // В ядре LibreTiny для Beken за переключение пина 11 (TX1) в режим UART отвечает макрос 
      // или прямой вызов ре-инициализации порта. 
      // Самый надежный способ вернуть Alt-функцию UART1 в Arduino-слое LibreTiny:
      
  #if defined(LT_BK7231N)
          // Принудительно стартуем Serial1, чтобы он перехватил пины 10 и 11 обратно в UART режим
          Serial1.begin(115200); 
  #endif
#endif
      

    
      this->send_empty_command_(TuyaCommandType::HEARTBEAT);  
      //this->send_empty_command_(TuyaCommandType::PRODUCT_QUERY);  
     
    }     
    
    void  TuComponent::loop()  {
      while (this->available()) {
        uint8_t c;
        this->read_byte(&c);
        this->handle_char_(c);
      }
      process_command_queue_();
      
    }

   
    void  TuComponent::handle_char_(uint8_t c) {      
      this->rx_message_.push_back(c);             
      if (!this->validate_message_()) {
        this->rx_message_.clear();
      } else {
        last_rx_char_timestamp_ = millis();
      }     
    }     
    
      bool  TuComponent::validate_message_() {
        uint32_t at = this->rx_message_.size() - 1;
        auto *data = &this->rx_message_[0];
        uint8_t new_byte = data[at];
 
        // Byte 0: HEADER1 (always 0x55)
        if (at == 0)
          return new_byte == 0x55;
        // Byte 1: HEADER2 (always 0xAA)
        if (at == 1)
          return new_byte == 0xAA;
 
        // Byte 2: VERSION
        // no validation for the following fields:
        uint8_t version = data[2];
        if (at == 2)
          return true;
        // Byte 3: COMMAND
        uint8_t command = data[3]; 
        if (at == 3)
          return true;
 
        // Byte 4: LENGTH1
        // Byte 5: LENGTH2
        if (at <= 5) {
          // no validation for these fields
          return true;
        }
 
        uint16_t length = (uint16_t(data[4]) << 8) | (uint16_t(data[5]));
 
        // wait until all data is read
        if (at - 6 < length)
          return true;
 
        // Byte 6+LEN: CHECKSUM - sum of all bytes (including header) modulo 256
        uint8_t rx_checksum = new_byte;
        uint8_t calc_checksum = 0;
        for (uint32_t i = 0; i < 6 + length; i++)
          calc_checksum += data[i];
        
        if (rx_checksum != calc_checksum) {
          return false;
        }
 
        // valid message
        const uint8_t *message_data = data + 6;
        
#ifdef ENABLE_DIAGNOSTICS
          // Вместо глобального this->g_0_ создаем чистый локальный объект прямо в точке вызова:
          UartFrame local_frame; 
          local_frame.g_init = this->init_state_;     
          local_frame.g_time = millis(); // Теперь это время железно зафиксировано только для этого фрейма!
          local_frame.g_delay = rx_message_;
          local_frame.g_inout = "Received"; 
        
          this->g_1_.push_back(local_frame);
#endif        
        
        this->handle_command_(command, version, message_data, length);

        // return false to reset rx buffer
        return false;
      }


      void  TuComponent::handle_command_(uint8_t command, uint8_t version, const uint8_t *buffer, size_t len) {
        TuyaCommandType command_type = (TuyaCommandType) command;
       
        if (this->expected_response_.has_value() && this->expected_response_ == command_type) {
          this->expected_response_.reset();
          this->command_queue_.erase(command_queue_.begin());
          this->init_retries_ = 0;
        }

        switch (command_type) {    
          case TuyaCommandType::HEARTBEAT:
          
              this->init_state_ = TuyaInitState::INIT_PRODUCT;
              this->send_empty_command_(TuyaCommandType::PRODUCT_QUERY);

            break;            
          case TuyaCommandType::PRODUCT_QUERY: {
            // check it is a valid string made up of printable characters
            bool valid = true;
            for (size_t i = 0; i < len; i++) {
              if (!std::isprint(buffer[i])) {
                valid = false;
                break;
              }
            }
            if (valid) {
              this->product_ = std::string(reinterpret_cast<const char *>(buffer), len);
            } else {
              this->product_ = R"({"p":"INVALID"})";
            }

// #ifdef ENABLE_DIAGNOSTICS 
//               this->keepalive_start_ms_ = millis();
//               this->keepalive_sent_ = false;
// #endif

              this->init_state_ = TuyaInitState::INIT_CONF;
              this->send_command_(TuyaCommand{.cmd = TuyaCommandType::CONF_QUERY, .payload = std::vector<uint8_t>{0x02}});

          break;
            
          }   
          case TuyaCommandType::CONF_QUERY: { 

            if (this->init_state_ == TuyaInitState::INIT_CONF) {
              this->init_state_ = TuyaInitState::INIT_WIFI;
              this->send_command_(TuyaCommand{.cmd = TuyaCommandType::CONF_QUERY, .payload = std::vector<uint8_t>{0x03}});
            }             
            
            break;
          }
          case TuyaCommandType::WIFI_RESET: {
            if (this->init_state_ == TuyaInitState::INIT_WIFI_RESET) {
              ESP_LOGE(TAG, "=========RESET=========  Время: %u мс", millis());
              get_restart_swith_factory = true;
            } 
            
#if defined(USE_BK72XX) || defined(LT_BK7231N)

            this->init_state_ = TuyaInitState::INIT_WIFI_OTA;  
            
  #ifdef ENABLE_DIAGNOSTICS              
            this->flag_ota_ = true;
            this->timestamp_ota_ = millis();
  #else
            // Отвечаем в MCU, чтобы подтвердить получение
            this->send_command_(TuyaCommand{.cmd = TuyaCommandType::WIFI_RESET, .payload = std::vector<uint8_t>{0x02}});
  #endif            

#endif
            break;
          }
          case TuyaCommandType::WIFI_OTA: {
            if (this->init_state_ == TuyaInitState::INIT_WIFI_OTA) {
              this->init_state_ = TuyaInitState::INIT_WIFI_RESET;
            } else {
              this->init_state_ = TuyaInitState::INIT_WIFI_OTA;
              
#ifdef ENABLE_DIAGNOSTICS              
              this->flag_ota_ = true;
              this->timestamp_ota_ = millis();
#endif              
             
            }
            
            this->send_command_(TuyaCommand{.cmd = TuyaCommandType::DATAPOINT_REPORT, .payload = std::vector<uint8_t>{0x00}});
            break;
          }
          case TuyaCommandType::DATAPOINT_REPORT: { 
            
            if (this->init_state_ == TuyaInitState::INIT_DATAPOINT) {
                
#ifdef USE_ESP8266
                this->init_state_ = TuyaInitState::INIT_DONE;
#endif

#if defined(USE_BK72XX) || defined(LT_BK7231N)
                this->init_state_ = TuyaInitState::INIT_WIFI_STREAM_STATUS;
#endif                

                this->send_command_(TuyaCommand{.cmd = TuyaCommandType::DATAPOINT_REPORT, .payload = std::vector<uint8_t>{0x00}});   
                
            } 
            
              this->handle_datapoints_(buffer, len);                
                
            break;
          }   
          case TuyaCommandType::DATAPOINT_QUERY: {
            break;
          } 
          
#if defined(USE_BK72XX) || defined(LT_BK7231N)      
          case TuyaCommandType::WIFI_STREAM_STATUS: { // Получили 0x10 от MCU --> BK7231

            if (this->init_state_ != TuyaInitState::INIT_WIFI_OTA && this->init_state_ == TuyaInitState::INIT_WIFI_STREAM_STATUS) { 
                
              this->init_state_ = TuyaInitState::INIT_DONE;                
                
            }  
              // Отправляем пустой ответ с тем же типом команды (ACK)
              this->send_empty_command_(TuyaCommandType::WIFI_STREAM_STATUS);

            break;  
          } 
#endif           
          default:
            break;
        }  
      }
  

        void TuComponent::process_command_queue_() {
          uint32_t now = millis();
          uint32_t delay = now - this->last_command_timestamp_;    
        
          if (now - this->last_rx_char_timestamp_ > RECEIVE_TIMEOUT) {               
            this->rx_message_.clear();
          }
          
          if (this->init_state_ != TuyaInitState::INIT_WIFI_OTA) {
              
            if (this->init_state_ == TuyaInitState::INIT_DONE) {  

              if (set_flag_conect_mqtt && !this->flag_mqtt_) {          
                  
                  this->flag_mqtt_ = true; // Закрываем затвор, повторов больше не будет
                  
                  // Ставим финальную команду в очередь
                  this->command_queue_.push_back(TuyaCommand{ .cmd = TuyaCommandType::DATAPOINT_REPORT, .payload = std::vector<uint8_t>{0x00} }); 
              
              }
              
            }     
            

            if (this->init_state_ == TuyaInitState::INIT_WIFI && set_flag_conect_wifi) {
            
                set_flag_conect_wifi = false;
                this->init_state_ = TuyaInitState::INIT_DATAPOINT;
                
                this->command_queue_.push_back(TuyaCommand{.cmd = TuyaCommandType::CONF_QUERY, .payload = std::vector<uint8_t>{0x04}}); 
            }

// #ifdef ENABLE_DIAGNOSTICS            
//             // Keepalive: принудительный DATAPOINT_REPORT через 6.5 сек от начала цикла
//             if (this->keepalive_start_ms_ != 0 && !this->keepalive_sent_ ) {
           
//               if (now - this->keepalive_start_ms_ > 6500UL) {
                  
//                 ESP_LOGE(TAG, "===============================");
//                 ESP_LOGE(TAG, "[KEEPALIVE] Forced DATAPOINT_REPORT at %lu ms (delta %lu)", now, now - this->keepalive_start_ms_);  
          
//                 this->command_queue_.push_back(TuyaCommand{ .cmd = TuyaCommandType::DATAPOINT_REPORT, .payload = std::vector<uint8_t>{0x00} });
//                 this->keepalive_sent_ = true;
              
//               }
//             }
// #endif             
            
          } 
          
#ifdef ENABLE_DIAGNOSTICS           
  #if defined(USE_BK72XX) || defined(LT_BK7231N)
            else {
            if (set_flag_conect_mqtt && !this->flag_log_bk_) {
                
              this->flag_log_bk_ = true;    
                
              // Отвечаем в MCU, чтобы подтвердить получение
              // когда поднимется mqtt для того чтобы увидеть логи
              this->send_command_(TuyaCommand{.cmd = TuyaCommandType::WIFI_RESET, .payload = std::vector<uint8_t>{0x02}});
              
            }  
            
          }    
  #endif            
#endif              
              
          
          
          if (this->expected_response_.has_value() && delay > RECEIVE_TIMEOUT) {
            this->expected_response_.reset();
            if (this->init_state_ != TuyaInitState::INIT_DONE && this->init_state_ != TuyaInitState::INIT_WIFI_OTA) {
              if (++this->init_retries_ >= MAX_RETRIES) {
                ESP_LOGE(TAG, "Initialization failed at init_state %u", static_cast<uint8_t>(this->init_state_));
                this->command_queue_.erase(command_queue_.begin());
                this->init_retries_ = 0;
              }
              if (this->init_state_ == TuyaInitState::INIT_HEARTBEAT || this->init_state_ == TuyaInitState::INIT_PRODUCT) {  
                this->command_queue_.clear();
                if (this->init_state_ == TuyaInitState::INIT_HEARTBEAT){      
                  this->init_state_ = TuyaInitState::INIT_PRODUCT;
                  this->command_queue_.push_back(TuyaCommand{.cmd = TuyaCommandType::PRODUCT_QUERY, .payload = std::vector<uint8_t>{}});    
                } else {   
                  this->init_state_ = TuyaInitState::INIT_HEARTBEAT;
                  this->command_queue_.push_back(TuyaCommand{.cmd = TuyaCommandType::HEARTBEAT, .payload = std::vector<uint8_t>{}});
                }
              }             
            } else {
              this->command_queue_.erase(command_queue_.begin());
            }
          }
          
          if (delay > COMMAND_DELAY && !this->command_queue_.empty() && this->rx_message_.empty() &&
            !this->expected_response_.has_value()) {
            this->send_raw_command_(command_queue_.front());
            if (!this->expected_response_.has_value())
              this->command_queue_.erase(command_queue_.begin());
          }
        }
          
      void  TuComponent::send_command_(const TuyaCommand &command) {
        command_queue_.push_back(command);
        process_command_queue_();
      }
 
 
      void  TuComponent::send_empty_command_(TuyaCommandType command) {
        send_command_(TuyaCommand{.cmd = command, .payload = std::vector<uint8_t>{}});
      } 
      
      
      void  TuComponent::send_raw_command_(TuyaCommand command) {
        uint8_t len_hi = (uint8_t)(command.payload.size() >> 8);
        uint8_t len_lo = (uint8_t)(command.payload.size() & 0xFF);
        uint8_t version = 0;
 
        this->last_command_timestamp_ = millis();
        switch (command.cmd) {
          case TuyaCommandType::HEARTBEAT:
            this->expected_response_ = TuyaCommandType::HEARTBEAT; 
            break;             
          case TuyaCommandType::PRODUCT_QUERY:
            this->expected_response_ = TuyaCommandType::PRODUCT_QUERY;
            break;
          case TuyaCommandType::CONF_QUERY:
            this->expected_response_ = TuyaCommandType::CONF_QUERY;
            break;
          case TuyaCommandType::DATAPOINT_REPORT:
            this->expected_response_ = TuyaCommandType::DATAPOINT_REPORT;
            break; 
          case TuyaCommandType::WIFI_STREAM_STATUS:
            this->expected_response_ = TuyaCommandType::WIFI_STREAM_STATUS;  
            break;            
          case TuyaCommandType::WIFI_RESET:
            this->expected_response_ = TuyaCommandType::WIFI_RESET;
            break;            
          default:
            break;            
        }
 
 
        // 1. Считаем контрольную сумму заранее
        uint8_t checksum = 0x55 + 0xAA + (uint8_t) command.cmd + len_hi + len_lo;
        for (auto &data : command.payload)
          checksum += data;
    
        // 2. Создаем один монолитный вектор для отправки в физический UART
        std::vector<uint8_t> tx_buffer;
        tx_buffer.reserve(7 + command.payload.size()); // Выделяем память сразу под весь фрейм
    
        // Собираем фрейм в памяти в единый кусок кремния
        tx_buffer.insert(tx_buffer.end(), {0x55, 0xAA, version, (uint8_t) command.cmd, len_hi, len_lo});
        if (!command.payload.empty()) {
            tx_buffer.insert(tx_buffer.end(), command.payload.begin(), command.payload.end());
        }
        tx_buffer.push_back(checksum);

#ifdef ENABLE_DIAGNOSTICS
          // Вместо глобального this->g_0_ создаем чистый локальный объект прямо в точке вызова:
          UartFrame local_frame; 
          local_frame.g_init = this->init_state_;     
          local_frame.g_time = millis(); // Теперь это время железно зафиксировано только для этого фрейма!
          local_frame.g_delay = tx_buffer; 
          local_frame.g_inout = "Sending";  
          
        this->g_1_.push_back(local_frame);          
        
        if (set_flag_conect_mqtt) {        
            
          ESP_LOGE(TAG, "===============================");
          ESP_LOGE(TAG, "=== ХРОНОЛОГИЯ ОБМЕНА С MCU === Время: %u мс", millis());
        
          const auto &vector_ref = this->g_1_;
        
          if (!vector_ref.empty()) {
            size_t total = vector_ref.size();
            
            // Быстрая таблица символов для HEX, чтобы не использовать sprintf
            static const char hex_chars[] = "0123456789ABCDEF";
            
            for (size_t i = 0; i < total; ++i) {
              const auto &frame = vector_ref.at(i);
        
              // Сверхбыстрая сборка HEX-строки напрямую без нагрузки на процессор
              std::string hex_str;
              hex_str.reserve(frame.g_delay.size() * 3); // Заранее выделяем место под строку
              
              for (uint8_t byte : frame.g_delay) {
                hex_str.push_back(hex_chars[(byte >> 4) & 0x0F]); // Старший полубайт
                hex_str.push_back(hex_chars[byte & 0x0F]);        // Младший полубайт
                hex_str.push_back(' ');                           // Пробел
              }
        
              std::string arrow = (frame.g_inout == "Sending") ? "----->" : "<-----"; 
        
              ESP_LOGE(TAG, "  [%s] %s [%s] [%u] Время: %u мс", 
                      frame.g_inout.c_str(),
                      arrow.c_str(),
                      hex_str.c_str(),
                      frame.g_init,
                      frame.g_time);                    
            }
            
            // Освобождаем элементы и ГАРАНТИРОВАННО возвращаем память системе
            this->g_1_.clear();
            this->g_1_.shrink_to_fit(); // <--- Защита от утечки памяти на ESP8266
            
          //   ESP_LOGE(TAG, "===============================");
            ESP_LOGE(TAG, "=== Приватный буфер успешно очищен ===");
        
          } else {
            ESP_LOGE(TAG, "Буфер обмена пуст.");
          }
        
          ESP_LOGE(TAG, "===============================");
          
          if (this->flag_battery_ == 1) {
            ESP_LOGE("tuya", "--- Батарея ESP8266 посчитана: %d%% ---[%u] Время: %u мс",this->final_percent_, this->flag_battery_init_, this->flag_battery_timestamp_); 
            this->flag_battery_ = 0;
          }    
          if (this->flag_battery_ == 2) {
            ESP_LOGE("tuya", "--- Батарея BK7231 (DP4) записана напрямую: %u%% ---[%u] Время: %u мс",this->final_percent_, this->flag_battery_init_, this->flag_battery_timestamp_); 
            this->flag_battery_ = 0;
          }              
          if (this->flag_battery_ == 3) {              
            ESP_LOGE("tuya", "--- Батарея BK7231 (DP10) посчитана: %d%% ---[%u] Время: %u мс",this->final_percent_, this->flag_battery_init_, this->flag_battery_timestamp_);   
            this->flag_battery_ = 0;
          }              
                  
          if (this->flag_ota_) {
            ESP_LOGE(TAG, "=========WIFI OTA=========  Время: %u мс", this->timestamp_ota_);
            this->flag_ota_ = false;

          }    
        }  
#endif

 
        // Даем микро-фоновое время сетевой карте начать отправку лога по Wi-Fi
        // пока контроллер физически пишет байты в UART шину
        if (command.cmd == TuyaCommandType::DATAPOINT_REPORT) {
            delay(20); // Задержка СТРОГО ДО write_array, чтобы лог улетел в сеть, а MCU еще не знал о команде «смерти»
        }
    
        // 3. Физический выстрел в UART
        this->write_array(tx_buffer.data(), tx_buffer.size());
        this->flush();          
      }    
    

        void TuComponent::handle_datapoints_(const uint8_t *buffer, size_t len) {
          size_t index = 0;
        
          while (index + 4 <= len) {
            TuyaDatapoint datapoint{};
            datapoint.id = buffer[index];
            datapoint.type = (TuyaDatapointType) buffer[index + 1];
            
            size_t data_size = (buffer[index + 2] << 8) + buffer[index + 3];
            const uint8_t *data = buffer + index + 4;
            
            if (index + 4 + data_size > len) {
              return; 
            }
        
            datapoint.len = data_size;
        
            switch (datapoint.type) {
        
              // ==========================================
              // ТИП ДАННЫХ: ENUM (Перечисление)
              // ==========================================
              case TuyaDatapointType::ENUM: {
                  if (data_size == 1) {
                    datapoint.value_enum = data[0];
#ifdef USE_ESP8266 
                    // --- ОБРАБОТКА ДЛЯ ESP8266 (модуль TYWE3S) ---
                    if (datapoint.id == 1) {
                      bool is_active = (datapoint.value_enum == 0x01 || datapoint.value_enum == 0x02); 
                      if (this->pir_leak_sensor_ptr_ != nullptr) 
                        this->pir_leak_sensor_ptr_->publish_state(!is_active);          
                    }
                    
                    // Получили статус батареи для ESP8266 (DP3)
                    if (datapoint.id == 3) {
                      this->battery_pir_leak_ = datapoint.value_enum;  
                      
                      // Математика пересчета прямо здесь (без внешних модулей)
                      if (set_battery_adc.has_value()) {
                        float adc_val = set_battery_adc.value();
                        int final_percent = -1;
                        
                        switch (datapoint.value_enum) {
                          case 0: if (adc_val > 1.5f) final_percent = (adc_val <= 3.2f) ? 0 : 25; break;
                          case 1: final_percent = 50; break;
                          case 2: final_percent = (adc_val < 1.5f) ? (int)(100 - ((1.0f - adc_val) / 0.205f * 100)) : 100; break;
                          default: break;
                        }
                        
                        if (final_percent >= 0) {
                          this->battery_pir_leak_scale_prt_->publish_state(final_percent);
  #ifdef ENABLE_DIAGNOSTICS                      
                          this->flag_battery_ = 1;
                          this->flag_battery_timestamp_ = millis();
                          this->final_percent_ = final_percent;
                          this->flag_battery_init_ = this->init_state_;                          
  #endif     
                        }
                      }
                    }          
#endif   
        
#if defined(USE_BK72XX) || defined(LT_BK7231N)
                    // --- ОБРАБОТКА ДЛЯ BK7231 (модуль CBU) ---
                    if (datapoint.id == 1) {
                      bool is_active = (datapoint.value_enum == 0x01 || datapoint.value_enum == 0x02);
                      if (this->pir_leak_sensor_ptr_ != nullptr) 
                        this->pir_leak_sensor_ptr_->publish_state(!is_active);          
                    }
        
                    // Старый формат батареи BK7231 (DP10 ENUM)
                    if (datapoint.id == 10) {
                      uint8_t mcu_bat = datapoint.value_enum;
                      uint8_t simulated_bat_val = 0;
                      bool valid_dp = true;
        
                      if (mcu_bat == 0x00) { simulated_bat_val = 2; } 
                      else if (mcu_bat == 0x01) { simulated_bat_val = 1; } 
                      else if (mcu_bat == 0x02 || mcu_bat == 0x03) { simulated_bat_val = 0; }
                      else { valid_dp = false; }
        
                      if (valid_dp) {
                        this->battery_pir_leak_ = simulated_bat_val;
        
                        // Пересчитываем по той же математике, используя ADC
                        if (set_battery_adc.has_value()) {
                          float adc_val = set_battery_adc.value();
                          int final_percent = -1;
        
                          switch (simulated_bat_val) {
                            case 0: if (adc_val > 1.5f) final_percent = (adc_val <= 3.2f) ? 0 : 25; break;
                            case 1: final_percent = 50; break;
                            case 2: final_percent = (adc_val < 1.5f) ? (int)(100 - ((1.0f - adc_val) / 0.205f * 100)) : 100; break;
                            default: break;
                          }
        
                          if (final_percent >= 0) {
                            this->battery_pir_leak_scale_prt_->publish_state(final_percent);
  #ifdef ENABLE_DIAGNOSTICS                      
                            this->flag_battery_ = 2;
                            this->flag_battery_timestamp_ = millis();
                            this->final_percent_ = final_percent;
                            this->flag_battery_init_ = this->init_state_;
  #endif     
                          }
                        }
                      }
                    }
  #endif
                  }
                  break;
                }
        
              // ==========================================
              // ТИП ДАННЫХ: VALUE (Числовые проценты)
              // ==========================================
#if defined(USE_BK72XX) || defined(LT_BK7231N)              
              case TuyaDatapointType::INTEGER: {
                    // Новый формат батареи BK7231 (DP4 INTEGER - прямая шкала)
                    if (datapoint.id == 4 && data_size == 4) {
                      datapoint.value_uint = encode_uint32(data[0], data[1], data[2], data[3]);    
                      this->battery_pir_leak_ = 3; 
                      // Прямая публикация готового значения из датапоинта
                      this->battery_pir_leak_scale_prt_->publish_state(datapoint.value_uint);
  #ifdef ENABLE_DIAGNOSTICS                      
                      this->flag_battery_ = 3;
                      this->flag_battery_timestamp_ = millis();
                      this->final_percent_ = datapoint.value_uint;
                      this->flag_battery_init_ = this->init_state_; 
  #endif     
                    }
                  break;
                }
#endif                
        
              default:
                break; 
            }
        
            index += 4 + data_size;
          }
        }



}  // namespace pir_water_leak_uart_tuya_component
}  // namespace esphome



