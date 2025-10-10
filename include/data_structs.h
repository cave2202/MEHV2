#ifndef DATA_STRUCTS_H
#define DATA_STRUCTS_H
struct sensor_data {
    float raw_temperature; // Celsius
    float raw_humidity; // % 
    float raw_pressure; // Pa

    float c_temperature; // Celsius
    float c_humidity; // %
    // float c_pressure;
    
    float co2_equivalent; // ppm
    float bVoc; // ppm

    float iaq; // 0-500
    int iaq_accuracy;

    int timestamp; // Unix timestamp, when the first measurement was created

    bool bme680_run_in;

};

struct system_status {
    bool sd_card; // true if an sdcard is pressent
    bool is_charing;
    float battery_percentage;
    float battery_voltage;
}; 

#endif