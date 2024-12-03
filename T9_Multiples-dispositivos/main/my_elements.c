#include "my_elements.h"

static const char *TAG_ELEMENTS = "My elements";

adc_oneshot_unit_handle_t adc1_handle;

void gpio_init() {
   gpio_config_t io_conf;
   io_conf.intr_type = GPIO_INTR_DISABLE;
   io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
   io_conf.pin_bit_mask = (1ULL << LED_INTERNAL);
   io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
   io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
   gpio_config(&io_conf);

   io_conf.intr_type = GPIO_INTR_DISABLE;
   io_conf.mode = GPIO_MODE_INPUT;
   io_conf.pin_bit_mask = (1ULL << ADC_SELECTED);
   io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
   io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
   gpio_config(&io_conf);

   io_conf.intr_type = GPIO_INTR_NEGEDGE;
   io_conf.mode = GPIO_MODE_INPUT;
   io_conf.pin_bit_mask = (1 << BUTTON_SEND_MESSAGE | 1 << BUTTON_RESTART_PIN);
   io_conf.pull_down_en = 1;
   io_conf.pull_up_en = 0;
   gpio_config(&io_conf);
}

void adc_init() {
   adc_oneshot_unit_init_cfg_t adc_config = {
       .unit_id = ADC_UNIT_1,
   };

   if (adc_oneshot_new_unit(&adc_config, &adc1_handle) == ESP_FAIL) {
      ESP_LOGE(TAG_ELEMENTS, "Failed to initialize ADC unit");
      return;
   }

   adc_oneshot_chan_cfg_t adc_channel_config = {
       .atten = ADC_ATTEN,
       .bitwidth = ADC_WIDTH,
   };

   if (adc_oneshot_config_channel(adc1_handle, ADC1_CHANNEL, &adc_channel_config) == ESP_FAIL) {
      ESP_LOGE(TAG_ELEMENTS, "Failed to configure ADC channel");
      adc_oneshot_del_unit(adc1_handle);
      return;
   }
   ESP_LOGI(TAG_ELEMENTS, "ADC initialized");
}

void ledc_init() {
   ledc_timer_config_t ledc_timer = {
       .duty_resolution = LEDC_TIMER_10_BIT,
       .freq_hz = LEDC_FREQUENCY,
       .speed_mode = LEDC_HIGH_SPEED_MODE,
       .timer_num = LEDC_TIMER_0,
       .clk_cfg = LEDC_AUTO_CLK,
   };
   ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

   ledc_channel_config_t ledc_channel = {
       .speed_mode = LEDC_MODE,
       .channel = LEDC_CHANNEL,
       .timer_sel = LEDC_TIMER,
       .intr_type = LEDC_INTR_DISABLE,
       .gpio_num = LED_PWM,
       .duty = 0,  // Set duty to 0%
       .hpoint = 0,
   };
   ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
   ESP_LOGI(TAG_ELEMENTS, "LEDC initialized");
}

void set_led(int value) {
   gpio_set_level(LED_INTERNAL, value);
   ESP_LOGI(TAG_ELEMENTS, "LED_INTERNAL set to: %d", value);
}

void set_pwm(uint16_t percentage) {
   // Formula for value = (2 ^ LEDC_DUTY_RESOLUTION) * percentage / 100
   int32_t value = (TWO_TO_THE_POWER_OF_10 * percentage) / 100;
   ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, value);
   ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
   ESP_LOGI(TAG_ELEMENTS, "PWM LED set to: %%%d, %ld", percentage, value);
}

int read_led() {
   int led_state = gpio_get_level(LED_INTERNAL);
   ESP_LOGI(TAG_ELEMENTS, "LED_INTERNAL state is: %d", led_state);
   return led_state;
}

uint16_t read_pwm() {
   uint16_t pwm_value = ledc_get_duty(LEDC_MODE, LEDC_CHANNEL);
   pwm_value = (pwm_value * 100) / TWO_TO_THE_POWER_OF_10;
   ESP_LOGI(TAG_ELEMENTS, "PWM LED is: %d", pwm_value);
   return pwm_value;
}

int read_adc_value() {
   int adc_value = 0;
   if (adc_oneshot_read(adc1_handle, ADC1_CHANNEL, &adc_value) == ESP_OK) {
      ESP_LOGI(TAG_ELEMENTS, "ADC value: %d", adc_value);
      return adc_value;
   }
   ESP_LOGE(TAG_ELEMENTS, "Failed to read ADC value");
   return ESP_FAIL;
}

int read_element(Elements_t element) {
   switch (element) {
      case ELEMENT_LED_INTERNAL:
         return read_led();
      case ELEMENT_ADC_SELECTED:
         return read_adc_value();
      case ELEMENT_LED_PWM:
         return read_pwm();
      default:
         return ESP_FAIL;
   }
}
