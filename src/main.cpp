#include <Adafruit_GFX.h>     // Graphics Lib
#include <Adafruit_SH110X.h>  // Diplay Driver
#include <Arduino.h>
#include <FS.h>      // File system
#include <RTClib.h>  // RTC Driver
#include <SD.h>      // SD-Card Driver
#include <SPI.h>     // SPI-Driver
#include <Wire.h>    // I2C Driver
#include <bsec2.h>   // BME680 Driver

#include "data_structs.h"
#include "driver/rtc_io.h"  // For controlling pullups during deepsleep
#include "sd_card.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDR 0x3c

#define RAIN_SENSOR_ANALOG_PIN 39
#define RAIN_SENSOR_DIGITAL_PIN 14

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)

#define LIGHT_SENSOR_PIN 35

#define SDA_PIN 21
#define SCL_PIN 22

#define BACK_BTN_PIN 25
#define UP_BTN_PIN 26
#define DOWN_BTN_PIN 15

#define SD_CS_PIN 13

#define BME_SAMPLE_RATE BSEC_SAMPLE_RATE_LP
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define SLEEP_DURATION_US (TIME_TO_SLEEP * uS_TO_S_FACTOR)

#define BME680_I2C_ADDR BME68X_I2C_ADDR_HIGH

enum ui_screen_t {
    SCREEN_DASHBOARD,
    SCREEN_SETTINGS,
    SCREEN_DETAILS,
};

bool in_ulp_mode = false;

int TIME_TO_SLEEP = 3;

bool burn_in_mode = true;

volatile bool display_timer_fired = false;
volatile bool back_btn_pressed = false;

volatile bool up_btn_pressed = false;
volatile bool down_btn_pressed = false;

RTC_DATA_ATTR ui_screen_t current_screen = SCREEN_DASHBOARD;

hw_timer_t* display_timer = NULL;

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

RTC_DS1307 rtc;

Bsec2 envSensor;

sensor_data global_sensor_data;
system_status global_system_status;

bsec_virtual_sensor_t sensor_list[12] = {
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_STABILIZATION_STATUS,
    BSEC_OUTPUT_RUN_IN_STATUS,
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
};

RTC_DATA_ATTR uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE];
RTC_DATA_ATTR bool hasBsecState = false;
RTC_DATA_ATTR int bootCount = 0;

void IRAM_ATTR on_display_timer() {
    display_timer_fired = true;
}

void IRAM_ATTR on_back_btn_pressed() {
    unsigned long now = millis();
    static unsigned long lastPress = 0;
    if (now - lastPress < 500) return;  // debounce
    lastPress = now;

    back_btn_pressed = true;
    Serial.println("BACK");
}

void IRAM_ATTR on_up_btn_pressed() {
    static unsigned long lastPress = 0;
    unsigned long now = millis();
    if (now - lastPress < 500) return;
    lastPress = now;
    up_btn_pressed = true;
    Serial.println("UP");
}

void IRAM_ATTR on_down_btn_pressed() {
    static unsigned long lastPress = 0;
    unsigned long now = millis();
    if (now - lastPress < 500) return;
    lastPress = now;
    down_btn_pressed = true;
    Serial.println("DOWN");
}

void print_sensor_data(const sensor_data& data) {
    Serial.println("=== Sensor Data ===");
    Serial.printf("  Timestamp     : %lld\n", data.timestamp);
    Serial.printf("  Raw Temp      : %.2f °C\n", data.raw_temperature);
    Serial.printf("  Raw Humidity  : %.2f %%\n", data.raw_humidity);
    Serial.printf("  Raw Pressure  : %.2f hPa\n", data.raw_pressure);
    Serial.printf("  Comp Temp     : %.2f °C\n", data.c_temperature);
    Serial.printf("  Comp Humidity : %.2f %%\n", data.c_humidity);
    Serial.printf("  CO2 equiv     : %.1f ppm\n", data.co2_equivalent);
    Serial.printf("  bVOC equiv    : %.2f ppm\n", data.bVoc);
    Serial.printf("  IAQ           : %.1f  (accuracy %d/3)\n", data.iaq, data.iaq_accuracy);
    Serial.printf("  Light level   : %d\n", data.light_level);
    Serial.printf("  BME680 run-in : %s\n", data.bme680_run_in ? "done" : "in progress");
    Serial.printf("  Is Raining : %s\n", data.is_raining ? "yes" : "no");
    Serial.printf("  Rain Voltage   : %d\n", data.raing_voltage);

    Serial.println("===================");
}

void print_system_status(const system_status& status) {
    Serial.println("=== System Status ===");
    Serial.printf("  SAMPLE MODE   : %s\n", status.in_ulp_mode ? "ULP" : "LP");
    Serial.printf("  BURN IN MODE:   : %s\n", status.burn_in_mode ? "YES" : "NO");
    Serial.println("===================");
}

void draw_ui() {
    if (!global_system_status.display_avalible ||
        !global_system_status.display_active) return;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    switch (current_screen) {
        case SCREEN_DASHBOARD:
            display.setCursor(0, 0);
            display.println("Screen 1: Messwerte");
            display.printf("Temp: %.2f\n", global_sensor_data.c_temperature);
            display.printf("Pressure: %.2f\n", global_sensor_data.raw_pressure);
            display.printf("Humidity: %.2f\n", global_sensor_data.c_humidity);
            display.printf("IAQ: %.2f\n", global_sensor_data.iaq);
            display.printf("ACC: %u\n", global_sensor_data.iaq_accuracy);
            display.printf("Light Level: %u\n", global_sensor_data.light_level);
            break;

        case SCREEN_DETAILS:
            display.setCursor(0, 0);
            display.printf("Pres:  %.1f hPa\n", global_sensor_data.raw_pressure / 100.0f);
            display.printf("bVOC:  %.2f ppm\n", global_sensor_data.bVoc);
            display.printf("Rain:  %s\n", global_sensor_data.is_raining ? "yes" : "no");
            display.printf("RunIn: %s\n", global_sensor_data.bme680_run_in ? "done" : "...");
            display.setCursor(0, 56);
            display.print("BACK=dashboard");
            break;

        case SCREEN_SETTINGS:
            display.setCursor(0, 0);
            display.println("== Settings ==");
            display.printf("SD:  %s\n", global_system_status.sd_card_avalible ? "ok" : "none");
            display.printf("RTC: %s\n", global_system_status.rtc_avalible ? "ok" : "none");
            display.printf("Boot: %d\n", bootCount);
            display.printf("Sample Mode: %s\n", global_system_status.in_ulp_mode ? "ULP" : "LP");
            display.setCursor(0, 56);
            display.print("BACK=dashboard");
            break;
    }

    display.display();
}

void handle_ui() {
    if (!global_system_status.display_active) return;

    bool changed = false;

    if (back_btn_pressed) {
        back_btn_pressed = false;
        timerRestart(display_timer);
        timerAlarmEnable(display_timer);
        current_screen = SCREEN_DASHBOARD;
        changed = true;
    }

    if (up_btn_pressed) {
        up_btn_pressed = false;
        timerRestart(display_timer);
        timerAlarmEnable(display_timer);
        current_screen = SCREEN_DETAILS;
        changed = true;
    }

    if (down_btn_pressed) {
        down_btn_pressed = false;
        timerRestart(display_timer);
        timerAlarmEnable(display_timer);
        current_screen = SCREEN_SETTINGS;
        changed = true;
    }

    if (changed) draw_ui();
}

void go_to_sleep() {
    envSensor.getState(bsecState);
    hasBsecState = true;

    Serial.println("Saving state, going to sleep...");
    Serial.flush();

    if (global_system_status.display_avalible) {
        display.oled_command(SH110X_DISPLAYOFF);
    }

    delay(100);

    rtc_gpio_init(gpio_num_t(BACK_BTN_PIN));
    rtc_gpio_set_direction(gpio_num_t(BACK_BTN_PIN), RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(gpio_num_t(BACK_BTN_PIN));
    rtc_gpio_pullup_en(gpio_num_t(BACK_BTN_PIN));

    esp_sleep_enable_ext0_wakeup(gpio_num_t(BACK_BTN_PIN), 0);
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);

    esp_deep_sleep_start();
}

void bsec_data_callback(const bme68x_data data, const bsecOutputs outputs, Bsec2 bsec) {
    if (!outputs.nOutputs) return;

    global_sensor_data.timestamp = (int64_t)time(nullptr);

    for (uint8_t i = 0; i < outputs.nOutputs; i++) {
        const bsecData& o = outputs.output[i];
        switch (o.sensor_id) {
            case BSEC_OUTPUT_RAW_TEMPERATURE:
                global_sensor_data.raw_temperature = o.signal;
                break;
            case BSEC_OUTPUT_RAW_HUMIDITY:
                global_sensor_data.raw_humidity = o.signal;
                break;
            case BSEC_OUTPUT_RAW_PRESSURE:
                global_sensor_data.raw_pressure = o.signal;
                break;

            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                global_sensor_data.c_temperature = o.signal;
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                global_sensor_data.c_humidity = o.signal;
                break;

            case BSEC_OUTPUT_CO2_EQUIVALENT:
                global_sensor_data.co2_equivalent = o.signal;
                break;
            case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                global_sensor_data.bVoc = o.signal;
                break;

            case BSEC_OUTPUT_IAQ:
                global_sensor_data.iaq = o.signal;
                global_sensor_data.iaq_accuracy = o.accuracy;
                break;

            case BSEC_OUTPUT_RUN_IN_STATUS:
                global_sensor_data.bme680_run_in = o.accuracy;
                break;
        }
    }

    if (global_system_status.sd_card_avalible) {
        write_sensor_data_to_csv(SD, &global_sensor_data, &global_system_status, "/sens.csv");
    }

    if (!digitalRead(RAIN_SENSOR_DIGITAL_PIN)) {
        Serial.println("RAINING!");
        int rain_volt = analogRead(RAIN_SENSOR_ANALOG_PIN);
        global_sensor_data.is_raining = true;
        global_sensor_data.raing_voltage = rain_volt;
    } else {
        global_sensor_data.is_raining = false;
    }

    global_sensor_data.light_level = analogRead(LIGHT_SENSOR_PIN);

    if (global_sensor_data.iaq_accuracy < 3) {
        global_system_status.burn_in_mode = true;
    }

    print_sensor_data(global_sensor_data);
    print_system_status(global_system_status);
    draw_ui();
}

void checkBsecStatus(Bsec2 bsec) {
    if (bsec.status < BSEC_OK) {
        Serial.println("BSEC error code : " + String(bsec.status));

    } else if (bsec.status > BSEC_OK) {
        Serial.println("BSEC warning code : " + String(bsec.status));
    }

    if (bsec.sensor.status < BME68X_OK) {
        Serial.println("BME68X error code : " + String(bsec.sensor.status));
    } else if (bsec.sensor.status > BME68X_OK) {
        Serial.println("BME68X warning code : " + String(bsec.sensor.status));
    }
}

void init_sd_card() {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("Card Mount Failed - continuing without SD");
        global_system_status.sd_card_avalible = false;
    } else {
        uint8_t cardType = SD.cardType();
        if (cardType == CARD_NONE) {
            Serial.println("No SD card attached");
            global_system_status.sd_card_avalible = false;
        } else {
            Serial.println("Found SD card!");
            global_system_status.sd_card_avalible = true;
            // testFileIO(SD, "/test.txt");
        }
    }
}

void init_bme680() {
    // Check if the Sensor is avalible
    if (!envSensor.begin(BME680_I2C_ADDR, Wire)) {
        Serial.println("Error with BME680!");
        checkBsecStatus(envSensor);
        while (true) delay(1000);
    }

    bool ret;
    if (global_system_status.in_ulp_mode) {
        ret = envSensor.updateSubscription(sensor_list, ARRAY_LEN(sensor_list), BSEC_SAMPLE_RATE_ULP);
    } else {
        ret = envSensor.updateSubscription(sensor_list, ARRAY_LEN(sensor_list), BSEC_SAMPLE_RATE_LP);
    }

    if (!ret) {
        Serial.println("Could not Subscribe to sensors!");
    }

    // Check if there is already a state in the RTC RAM
    if (hasBsecState) {
        Serial.println("Restoring BSEC2 state...");
        if (!envSensor.setState(bsecState)) {
            Serial.println("State restore failed, starting fresh.");
            hasBsecState = false;
            memset(bsecState, 0, sizeof(bsecState));
        }
    } else {
        Serial.println("First boot, no BSEC2 state yet.");
        burn_in_mode = true;
    }

    if (in_ulp_mode) {
        envSensor.setTemperatureOffset(TEMP_OFFSET_ULP);
    } else {
        envSensor.setTemperatureOffset(TEMP_OFFSET_LP);
    }

    envSensor.attachCallback(bsec_data_callback);
}

void init_rtc() {
    if (!rtc.begin()) {
        Serial.println("DS1307 not found!");
        global_system_status.rtc_avalible = false;

    } else {
        if (!rtc.isrunning()) {
            Serial.println("DS1307 not running, setting time...");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }

        DateTime now = rtc.now();
        struct timeval tv = {now.unixtime(), 0};
        settimeofday(&tv, NULL);

        Serial.printf("Time set to: %04d-%02d-%02d %02d:%02d:%02d\n",
                      now.year(), now.month(), now.day(),
                      now.hour(), now.minute(), now.second());

        global_system_status.rtc_avalible = true;
    }
}

void init_display() {
    if (!display.begin(SCREEN_ADDR, true)) {
        Serial.println("Display Init failed!");
        global_system_status.display_avalible = false;
        return;
    }
    display.display();
    delay(1000);
    display.clearDisplay();
    global_system_status.display_avalible = true;
}

void setup() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);

    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {  // changed from EXT0 to EXT1
        delay(300);
        Serial.println("Woke up from button press - activating display");
        global_system_status.display_active = true;
    } else if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("Woke up from timer - silent reading");
        global_system_status.display_active = false;
    } else {
        Serial.println("First boot");
        global_system_status.display_active = true;
    }

    pinMode(RAIN_SENSOR_ANALOG_PIN, INPUT);
    pinMode(RAIN_SENSOR_DIGITAL_PIN, INPUT);

    pinMode(BACK_BTN_PIN, INPUT_PULLUP);
    pinMode(UP_BTN_PIN, INPUT_PULLUP);
    pinMode(DOWN_BTN_PIN, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(BACK_BTN_PIN), on_back_btn_pressed, FALLING);
    attachInterrupt(digitalPinToInterrupt(UP_BTN_PIN), on_up_btn_pressed, FALLING);
    attachInterrupt(digitalPinToInterrupt(DOWN_BTN_PIN), on_down_btn_pressed, FALLING);

    init_rtc();
    init_sd_card();

    display_timer = timerBegin(0, 80, true);
    timerAttachInterrupt(display_timer, &on_display_timer, true);
    timerAlarmWrite(display_timer, 30000000, false);

    if (global_system_status.display_active) {
        init_display();
        timerAlarmEnable(display_timer);
    }

    init_bme680();
    Serial.println("BME680 + BSEC2 ready.");
    bootCount++;
}

void loop() {
    // handle button wakeup display init
    if (back_btn_pressed && !global_system_status.display_active) {
        back_btn_pressed = false;
        global_system_status.display_active = true;
        if (!global_system_status.display_avalible) {
            init_display();
        } else {
            display.oled_command(SH110X_DISPLAYON);
        }
        timerRestart(display_timer);
        timerAlarmEnable(display_timer);
        draw_ui();
        return;
    }

    handle_ui();

    if (display_timer_fired && global_system_status.burn_in_mode == false) {
        display_timer_fired = false;
        global_system_status.display_active = false;
        if (global_system_status.display_avalible) {
            display.oled_command(SH110X_DISPLAYOFF);
        }

        go_to_sleep();
    }
    if (global_sensor_data.iaq_accuracy > 2) {
        global_system_status.burn_in_mode = false;
        global_system_status.in_ulp_mode = true;
    }

    if (!envSensor.run()) {
        if (envSensor.status < BSEC_OK || envSensor.sensor.status < BME68X_OK) {
            checkBsecStatus(envSensor);
            while (true) delay(1000);
        }
    }
}