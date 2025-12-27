#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <math.h>
#include <curl/curl.h>

#include "sensirion_i2c_hal.h"
#include "sfa3x_i2c.h"
#include "scd30_i2c.h"
#include "sen44_i2c.h"
#include "sen66_i2c.h"
#include "sen5x_i2c.h"

/* --- Global running flag --- */
static volatile sig_atomic_t running = 1;

/* ---------- Signal Handler ---------- */
static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

/* ---------- Time Helpers ---------- */
static void get_local_timestamp(char* buf, size_t len) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_info;
    if (!localtime_r(&tv.tv_sec, &tm_info)) {
        snprintf(buf, len, "1970-01-01 00:00:00");
        return;
    }
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_info);
}

/* ---------- Temperature Helpers ---------- */
static inline float c_to_f(float c) {
    return (c * 9.0f / 5.0f) + 32.0f;
}

static inline void print_temp(float c) {
    if (isnan(c)) {
        printf("Temp: NaN");
    } else {
        printf("Temp: %.2f °C (%.2f °F)", c, c_to_f(c));
    }
}

/* ---------- InfluxDB 1.8 (no auth, database=sensors) ---------- */
#define INFLUXDB_URL "http://127.0.0.1:8086/write?db=sensors"

static void influx_write(const char* payload) {
    CURL *curl = curl_easy_init();
    if (!curl) return;

    curl_easy_setopt(curl, CURLOPT_URL, INFLUXDB_URL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);

    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        fprintf(stderr, "InfluxDB write failed: %s\n", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
}

/* ---------- Main ---------- */
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* --- I2C init --- */
    sensirion_i2c_hal_init();

    /* --- Initialize Sensors --- */
    sfa3x_init(SFA3X_I2C_ADDR_5D);
    sfa3x_device_reset();
    sensirion_i2c_hal_sleep_usec(1000000);
    sfa3x_start_continuous_measurement();

    scd30_init(SCD30_I2C_ADDR_61);
    scd30_stop_periodic_measurement();
    scd30_soft_reset();
    sensirion_i2c_hal_sleep_usec(2000000);
    scd30_start_periodic_measurement(0);

    sen44_device_reset();
    sen44_start_measurement();

    sen5x_device_reset();
    sensirion_i2c_hal_sleep_usec(1000000);
    sen5x_set_temperature_offset_simple(0.0f);
    sen5x_start_measurement();

    sen66_init(SEN66_I2C_ADDR_6B);
    sen66_device_reset();
    sensirion_i2c_hal_sleep_usec(1200000);
    sen66_start_continuous_measurement();

    /* --- 10-second countdown before starting readings --- */
    printf("Sensors initialized. Starting in 10 seconds...\n");
    for (int i = 10; i > 0; i--) {
        printf("\rStarting in %2d seconds... ", i);
        fflush(stdout);
        sensirion_i2c_hal_sleep_usec(1000000);
    }
    printf("\rStarting multi-sensor measurement loop!\n");

    /* --- Main measurement loop --- */
    while (running) {
        sensirion_i2c_hal_sleep_usec(1000000);

        char timestamp[32];
        get_local_timestamp(timestamp, sizeof(timestamp));
        printf("\n--- %s ---\n", timestamp);

        char influx_payload[1024] = "";
        int offset = 0;

        /* --- SFA3X --- */
        float hcho=0, sfa_hum=0, sfa_temp=0;
        if (!sfa3x_read_measured_values(&hcho, &sfa_hum, &sfa_temp)) {
            printf("SFA3X -> HCHO: %.2f ppb, ", hcho);
            print_temp(sfa_temp);
            printf(", Hum: %.2f %%\n", sfa_hum);

            offset += snprintf(influx_payload + offset, sizeof(influx_payload) - offset,
                               "sfa3x hcho=%.2f,temperature=%.2f,humidity=%.2f\n",
                               hcho, sfa_temp, sfa_hum);
        }

        /* --- SCD30 --- */
        float co2=0, scd_temp=0, scd_hum=0;
        if (!scd30_blocking_read_measurement_data(&co2, &scd_temp, &scd_hum)) {
            printf("SCD30 -> CO2: %.2f ppm, ", co2);
            print_temp(scd_temp);
            printf(", Hum: %.2f %%\n", scd_hum);

            offset += snprintf(influx_payload + offset, sizeof(influx_payload) - offset,
                               "scd30 co2=%.2f,temperature=%.2f,humidity=%.2f\n",
                               co2, scd_temp, scd_hum);
        }

        /* --- SEN44 --- */
        uint16_t pm1p0_44=0, pm2p5_44=0, pm4p0_44=0, pm10p0_44=0;
        float voc_44=0, hum_44=0, temp_44=0;
        if (!sen44_read_measured_mass_concentration_and_ambient_values(
                &pm1p0_44, &pm2p5_44, &pm4p0_44, &pm10p0_44,
                &voc_44, &hum_44, &temp_44)) {
            printf("SEN44 -> PM1.0: %u, PM2.5: %u, PM4.0: %u, PM10: %u, VOC: %.2f, ",
                   pm1p0_44, pm2p5_44, pm4p0_44, pm10p0_44, voc_44);
            print_temp(temp_44);
            printf(", Hum: %.2f %%\n", hum_44);

            offset += snprintf(influx_payload + offset, sizeof(influx_payload) - offset,
                               "sen44 pm1=%.2f,pm2_5=%.2f,pm4=%.2f,pm10=%.2f,voc=%.2f,temperature=%.2f,humidity=%.2f\n",
                               (float)pm1p0_44, (float)pm2p5_44, (float)pm4p0_44, (float)pm10p0_44,
                               voc_44, temp_44, hum_44);
        }

        /* --- SEN55 --- */
        float pm1p0_5x=0, pm2p5_5x=0, pm4p0_5x=0, pm10p0_5x=0;
        float hum_5x=0, temp_5x=0, voc_5x=0, nox_5x=0;
        if (!sen5x_read_measured_values(
                &pm1p0_5x, &pm2p5_5x, &pm4p0_5x, &pm10p0_5x,
                &hum_5x, &temp_5x, &voc_5x, &nox_5x)) {
            printf("SEN55 -> PM1.0: %.2f, PM2.5: %.2f, PM4.0: %.2f, PM10: %.2f, VOC: %.2f, NOx: %.2f, ",
                   pm1p0_5x, pm2p5_5x, pm4p0_5x, pm10p0_5x, voc_5x, nox_5x);
            print_temp(temp_5x);
            printf(", Hum: %.2f %%\n", hum_5x);

            offset += snprintf(influx_payload + offset, sizeof(influx_payload) - offset,
                               "sen55 pm1=%.2f,pm2_5=%.2f,pm4=%.2f,pm10=%.2f,voc=%.2f,nox=%.2f,temperature=%.2f,humidity=%.2f\n",
                               pm1p0_5x, pm2p5_5x, pm4p0_5x, pm10p0_5x, voc_5x, nox_5x, temp_5x, hum_5x);
        }

        /* --- SEN66 --- */
        float pm1p0_66=0, pm2p5_66=0, pm4p0_66=0, pm10p0_66=0;
        float hum_66=0, temp_66=0, voc_66=0, nox_66=0;
        uint16_t co2_66=0;
        if (!sen66_read_measured_values(
                &pm1p0_66, &pm2p5_66, &pm4p0_66, &pm10p0_66,
                &hum_66, &temp_66, &voc_66, &nox_66, &co2_66)) {
            printf("SEN66 -> PM1.0: %.2f, PM2.5: %.2f, PM4.0: %.2f, PM10: %.2f, VOC: %.2f, NOx: %.2f, CO2: %u, ",
                   pm1p0_66, pm2p5_66, pm4p0_66, pm10p0_66, voc_66, nox_66, co2_66);
            print_temp(temp_66);
            printf(", Hum: %.2f %%\n", hum_66);

            offset += snprintf(influx_payload + offset, sizeof(influx_payload) - offset,
                               "sen66 pm1=%.2f,pm2_5=%.2f,pm4=%.2f,pm10=%.2f,voc=%.2f,nox=%.2f,co2=%u,temperature=%.2f,humidity=%.2f\n",
                               pm1p0_66, pm2p5_66, pm4p0_66, pm10p0_66, voc_66, nox_66, co2_66, temp_66, hum_66);
        }

        /* --- Send to InfluxDB only if at least one sensor responded --- */
        if (offset > 0) {
            influx_write(influx_payload);
        }
    }

    /* --- Stop sensors --- */
    sfa3x_stop_measurement();
    scd30_stop_periodic_measurement();
    sen44_stop_measurement();
    sen5x_stop_measurement();
    sen66_stop_measurement();

    return 0;
}
