/*
 * (C) Copyright 2025, SECO Mind Srl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/posix/time.h>
LOG_MODULE_REGISTER(cpu_metrics_app, CONFIG_APP_LOG_LEVEL);

#if (!defined(CONFIG_ASTARTE_DEVICE_SDK_DEVELOP_USE_NON_TLS_HTTP)                                  \
    || !defined(CONFIG_ASTARTE_DEVICE_SDK_DEVELOP_USE_NON_TLS_MQTT))
#include <zephyr/net/tls_credentials.h>

#include "ca_certificates.h"
#endif

#include <astarte_device_sdk/device.h>

#ifdef CONFIG_EDGEHOG_DEVICE_ZBUS_OTA_EVENT
#include <edgehog_device/ota_event.h>
#endif
#include <edgehog_device/device.h>
#include <edgehog_device/telemetry.h>

#if defined(CONFIG_WIFI)
#include "wifi.h"
#else
#include "eth.h"
#endif
#include "generated_interfaces.h"

#include "sample_config.h"

/************************************************
 * Constants and defines
 ***********************************************/

#define MAIN_THREAD_PERIOD_MS 500
#define EDGEHOG_DEVICE_PERIOD_MS 100
#ifdef CONFIG_EDGEHOG_DEVICE_ZBUS_OTA_EVENT
#define ZBUS_PERIOD_MS 500
#endif

#define HTTP_TIMEOUT_MS (3 * MSEC_PER_SEC)
#define MQTT_FIRST_POLL_TIMEOUT_MS (3 * MSEC_PER_SEC)
#define MQTT_POLL_TIMEOUT_MS 200

#define TELEMETRY_PERIOD_S 5

#define EDGEHOG_DEVICE_THREAD_STACK_SIZE 16384
#define EDGEHOG_DEVICE_THREAD_PRIORITY 0
K_THREAD_STACK_DEFINE(edgehog_device_thread_stack_area, EDGEHOG_DEVICE_THREAD_STACK_SIZE);
static struct k_thread edgehog_device_thread_data;

#ifdef CONFIG_EDGEHOG_DEVICE_ZBUS_OTA_EVENT
#define ZBUS_THREAD_STACK_SIZE 16384
#define ZBUS_THREAD_PRIORITY 0
K_THREAD_STACK_DEFINE(zbus_thread_stack_area, ZBUS_THREAD_STACK_SIZE);
static struct k_thread zbus_thread_data;
#define EDGEHOG_OTA_SUBSCRIBER_NOTIFICATION_QUEUE_SIZE 5
#define EDGEHOG_OTA_OBSERVER_NOTIFY_PRIORITY 5
ZBUS_SUBSCRIBER_DEFINE(edgehog_ota_subscriber, EDGEHOG_OTA_SUBSCRIBER_NOTIFICATION_QUEUE_SIZE);
ZBUS_CHAN_ADD_OBS(edgehog_ota_chan, edgehog_ota_subscriber, EDGEHOG_OTA_OBSERVER_NOTIFY_PRIORITY);
#endif

#define STATS_THREAD_STACK_SIZE 16384
#define STATS_THREAD_PRIORITY 0
K_THREAD_STACK_DEFINE(stats_thread_stack_area, STATS_THREAD_STACK_SIZE);
static struct k_thread stats_thread_data;

#ifdef CONFIG_CPU_TEMP_SENSOR
static const struct device *const die_temp_sensor = DEVICE_DT_GET(DT_ALIAS(die_temp0));
#endif
#ifdef CONFIG_AMBIENT_TEMP_SENSOR
static const struct device *const ambient_temp_sensor = DEVICE_DT_GET(DT_ALIAS(ambient_temp0));
#endif

enum device_tread_flags
{
    DEVICE_THREADS_FLAGS_TERMINATION = 1U,
    DEVICE_THREADS_FLAGS_ASTARTE_CONNECTED,
};
static atomic_t device_threads_flags;

static edgehog_device_handle_t edgehog_device;

/************************************************
 * Static functions declaration
 ***********************************************/

/**
 * @brief Initialize System Time
 */
static void system_time_init(void);
/**
 * @brief Entry point for the Astarte device thread.
 *
 * @param arg1 Unused argument.
 * @param arg2 Unused argument.
 * @param arg3 Unused argument.
 */
static void edgehog_device_thread_entry_point(void *device_id, void *cred_secr, void *arg3);
#ifdef CONFIG_EDGEHOG_DEVICE_ZBUS_OTA_EVENT
/**
 * @brief Entry point for the Zbus reception thread.
 *
 * @param arg1 Unused argument.
 * @param arg2 Unused argument.
 * @param arg3 Unused argument.
 */
static void zbus_thread_entry_point(void *arg1, void *arg2, void *arg3);
#endif
#ifdef CONFIG_EDGEHOG_DEVICE_ZBUS_OTA_EVENT
/**
 * @brief Listen on the Edgehog zbus channel for events.
 *
 * @param timeout The timeout used for listening. In case of a received notification this timeout
 * is also used for reading the message.
 */
static void edgehog_listen_zbus_channel(k_timeout_t timeout);
#endif
/**
 * @brief Callback handler for Astarte connection events.
 *
 * @param event Astarte device connection event.
 */
static void astarte_connection_callback(astarte_device_connection_event_t event);
/**
 * @brief Callback handler for Astarte disconnection events.
 *
 * @param event Astarte device disconnection event.
 */
static void astarte_disconnection_callback(astarte_device_disconnection_event_t event);
/**
 * @brief Entry point for the stats thread.
 *
 * @param arg1 Unused argument.
 * @param arg2 Unused argument.
 * @param arg3 Unused argument.
 */
static void stats_thread_entry_point(void *arg1, void *arg2, void *arg3);

/************************************************
 * Global functions definition
 ***********************************************/

// NOLINTNEXTLINE(hicpp-function-size)
int main(void)
{
    LOG_INF("Edgehog device sample");
    LOG_INF("Board: %s", CONFIG_BOARD);

    struct sample_config cfg_from_file = { 0 };
    sample_config_get(&cfg_from_file);
    LOG_INF("Configured device ID: %s", cfg_from_file.device_id);
    LOG_INF("Configured credential secret: %s", cfg_from_file.credential_secret);
#if defined(CONFIG_WIFI)
    LOG_INF("Configured WiFi SSID: %s", cfg_from_file.wifi_ssid);
    LOG_INF("Configured WiFi password: %s", cfg_from_file.wifi_pwd);
#endif

#ifdef CONFIG_CPU_TEMP_SENSOR
    if (!device_is_ready(die_temp_sensor)) {
        LOG_ERR("sensor: device %s not ready.", die_temp_sensor->name);
        return -ENODEV;
    }
#endif
#ifdef CONFIG_AMBIENT_TEMP_SENSOR
    if (!device_is_ready(ambient_temp_sensor)) {
        LOG_ERR("sensor: device %s not ready.", ambient_temp_sensor->name);
        return -ENODEV;
    }
#endif

#if defined(CONFIG_WIFI)
    LOG_INF("Initializing WiFi driver."); // NOLINT
    app_wifi_init();
    k_sleep(K_SECONDS(5));
    enum wifi_security_type sec = WIFI_SECURITY_TYPE_PSK;
    if (app_wifi_connect(cfg_from_file.wifi_ssid, sec, cfg_from_file.wifi_pwd) != 0) {
        LOG_ERR("Connectivity intialization failed!"); // NOLINT
        return -1;
    }
#else
    LOG_INF("Initializing Ethernet driver."); // NOLINT
    if (eth_connect() != 0) {
        LOG_ERR("Connectivity intialization failed!"); // NOLINT
        return -1;
    }
#endif

    // Add TLS certificate for Astarte if required
#if (!defined(CONFIG_ASTARTE_DEVICE_SDK_DEVELOP_USE_NON_TLS_HTTP)                                  \
    || !defined(CONFIG_ASTARTE_DEVICE_SDK_DEVELOP_USE_NON_TLS_MQTT))
    tls_credential_add(CONFIG_ASTARTE_DEVICE_SDK_HTTPS_CA_CERT_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
        ca_certificate_root, sizeof(ca_certificate_root));
#endif
    // Add TLS certificate for Edgehog if required
#if !defined(CONFIG_EDGEHOG_DEVICE_DEVELOP_DISABLE_OR_IGNORE_TLS)
    tls_credential_add(CONFIG_EDGEHOG_DEVICE_CA_CERT_OTA_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
        ota_ca_certificate_root, sizeof(ota_ca_certificate_root));
#endif

    // Initalize the system time
    system_time_init();

    // Spawn a new thread for the Edgehog device
    k_thread_create(&edgehog_device_thread_data, edgehog_device_thread_stack_area,
        K_THREAD_STACK_SIZEOF(edgehog_device_thread_stack_area), edgehog_device_thread_entry_point,
        cfg_from_file.device_id, cfg_from_file.credential_secret, NULL,
        EDGEHOG_DEVICE_THREAD_PRIORITY, 0, K_NO_WAIT);

    // Spawn a new thread for the stats
    k_thread_create(&stats_thread_data, stats_thread_stack_area,
        K_THREAD_STACK_SIZEOF(stats_thread_stack_area), stats_thread_entry_point, NULL, NULL, NULL,
        STATS_THREAD_PRIORITY, 0, K_NO_WAIT);

    // Wait for a predefined operational time.
    k_timeout_t finish_timeout = (CONFIG_SAMPLE_DURATION_SECONDS == 0)
        ? K_FOREVER
        : K_SECONDS(CONFIG_SAMPLE_DURATION_SECONDS);
    k_timepoint_t finish_timepoint = sys_timepoint_calc(finish_timeout);
    while (!K_TIMEOUT_EQ(sys_timepoint_timeout(finish_timepoint), K_NO_WAIT)) {
        k_timepoint_t timepoint = sys_timepoint_calc(K_MSEC(MAIN_THREAD_PERIOD_MS));
#if !defined(CONFIG_WIFI)
        eth_poll();
#endif
        k_sleep(sys_timepoint_timeout(timepoint));
    }

    // Signal to the Device threads that they should terminate.
    atomic_set_bit(&device_threads_flags, DEVICE_THREADS_FLAGS_TERMINATION);

    // Wait for the Edgehog thread to terminate.
    if (k_thread_join(&edgehog_device_thread_data, K_FOREVER) != 0) {
        LOG_ERR("Failed in waiting for the Edgehog thread to terminate.");
    }

    LOG_INF("Edgehog device sample finished.");
    k_sleep(K_MSEC(MSEC_PER_SEC));

    return 0;
}

/************************************************
 * Static functions definitions
 ***********************************************/

static void system_time_init()
{
#ifdef CONFIG_SNTP
    int ret = 0;
    struct sntp_time now;
    struct timespec tspec;

    ret = sntp_simple(
        CONFIG_NET_CONFIG_SNTP_INIT_SERVER, CONFIG_NET_CONFIG_SNTP_INIT_TIMEOUT, &now);
    if (ret == 0) {
        tspec.tv_sec = (time_t) now.seconds;
        // NOLINTBEGIN(bugprone-narrowing-conversions, hicpp-signed-bitwise,
        // readability-magic-numbers)
        tspec.tv_nsec = ((uint64_t) now.fraction * (1000lu * 1000lu * 1000lu)) >> 32;
        // NOLINTEND(bugprone-narrowing-conversions, hicpp-signed-bitwise,
        // readability-magic-numbers)
        clock_settime(CLOCK_REALTIME, &tspec);
    }
#endif
}

static void edgehog_device_thread_entry_point(void *device_id, void *cred_secr, void *arg3)
{
    ARG_UNUSED(arg3);

    // Configuring the Astarte device used by the Edgehog device to communicate with the cloud
    // Edgehog instance. This device can also be leveraged by the user to send and receive data
    // through Astarte, check out the sample README for more information.

    const astarte_interface_t *interfaces[] = {
        &com_example_poc_CpuMetrics,
#ifdef CONFIG_CPU_TEMP_SENSOR
        &com_example_poc_CpuTemp,
#endif
#ifdef CONFIG_AMBIENT_TEMP_SENSOR
        &com_example_poc_AmbientTemp,
#endif
    };

    astarte_device_config_t astarte_device_config = { 0 };
    astarte_device_config.http_timeout_ms = HTTP_TIMEOUT_MS;
    astarte_device_config.mqtt_connection_timeout_ms = MQTT_FIRST_POLL_TIMEOUT_MS;
    astarte_device_config.mqtt_poll_timeout_ms = MQTT_POLL_TIMEOUT_MS;
    astarte_device_config.connection_cbk = astarte_connection_callback;
    astarte_device_config.disconnection_cbk = astarte_disconnection_callback;
    astarte_device_config.interfaces = interfaces;
    astarte_device_config.interfaces_size = ARRAY_SIZE(interfaces);
    memcpy(astarte_device_config.cred_secr, cred_secr, ASTARTE_PAIRING_CRED_SECR_LEN + 1);
    memcpy(astarte_device_config.device_id, device_id, ASTARTE_DEVICE_ID_LEN + 1);

    edgehog_result_t eres = EDGEHOG_RESULT_OK;

    edgehog_telemetry_config_t telemetry_config = {
        .type = EDGEHOG_TELEMETRY_SYSTEM_STATUS,
        .period_seconds = TELEMETRY_PERIOD_S,
    };
    edgehog_device_config_t edgehog_conf = {
        .astarte_device_config = astarte_device_config,
        .telemetry_config = &telemetry_config,
        .telemetry_config_len = 1,
    };
    eres = edgehog_device_new(&edgehog_conf, &edgehog_device);
    if (eres != EDGEHOG_RESULT_OK) {
        LOG_ERR("Unable to create edgehog device handle");
        goto exit;
    }

    eres = edgehog_device_start(edgehog_device);
    if (eres != EDGEHOG_RESULT_OK) {
        LOG_ERR("Unable to start edgehog device");
        goto exit;
    }

#ifdef CONFIG_EDGEHOG_DEVICE_ZBUS_OTA_EVENT
    // Spawn a new thread for listening on Zbus device
    k_thread_create(&zbus_thread_data, zbus_thread_stack_area,
        K_THREAD_STACK_SIZEOF(zbus_thread_stack_area), zbus_thread_entry_point, NULL, NULL, NULL,
        ZBUS_THREAD_PRIORITY, 0, K_NO_WAIT);
#endif

    while (!atomic_test_bit(&device_threads_flags, DEVICE_THREADS_FLAGS_TERMINATION)) {
        k_timepoint_t timepoint = sys_timepoint_calc(K_MSEC(EDGEHOG_DEVICE_PERIOD_MS));

        eres = edgehog_device_poll(edgehog_device);
        if (eres != EDGEHOG_RESULT_OK) {
            LOG_ERR("Edgehog device poll failure.");
            goto exit;
        }

        k_sleep(sys_timepoint_timeout(timepoint));
    }

    LOG_INF("End of sample, stopping Edgehog.");
    eres = edgehog_device_stop(edgehog_device, K_FOREVER);
    if (eres != EDGEHOG_RESULT_OK) {
        LOG_ERR("Unable to stop the edgehog device");
        goto exit;
    }

    LOG_INF("Edgehog device will now be destroyed.");
    edgehog_device_destroy(edgehog_device);

exit:
    // Ensure to set the thread termination flag
    atomic_set_bit(&device_threads_flags, DEVICE_THREADS_FLAGS_TERMINATION);

#ifdef CONFIG_EDGEHOG_DEVICE_ZBUS_OTA_EVENT
    // Wait for the Zbus thread to terminate.
    if (k_thread_join(&zbus_thread_data, K_SECONDS(10)) != 0) {
        LOG_ERR("Failed in waiting for the Zbus thread to terminate.");
    }
#endif
}

#ifdef CONFIG_EDGEHOG_DEVICE_ZBUS_OTA_EVENT
static void zbus_thread_entry_point(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (!atomic_test_bit(&device_threads_flags, DEVICE_THREADS_FLAGS_TERMINATION)) {
        edgehog_listen_zbus_channel(K_MSEC(ZBUS_PERIOD_MS));
    }
}
#endif

#ifdef CONFIG_EDGEHOG_DEVICE_ZBUS_OTA_EVENT
static void edgehog_listen_zbus_channel(k_timeout_t timeout)
{
    const struct zbus_channel *chan = NULL;
    edgehog_ota_chan_event_t ota = { 0 };
    if ((zbus_sub_wait(&edgehog_ota_subscriber, &chan, timeout) == 0) && (&edgehog_ota_chan == chan)
        && (zbus_chan_read(chan, &ota, timeout) == 0)) {
        switch (ota.event) {
            case EDGEHOG_OTA_INIT_EVENT:
                LOG_WRN("To subscriber -> EDGEHOG_OTA_INIT_EVENT");
                break;
            case EDGEHOG_OTA_PENDING_REBOOT_EVENT:
                LOG_WRN("To subscriber -> EDGEHOG_OTA_PENDING_REBOOT_EVENT");
                edgehog_ota_chan_event_t ota_chan_event
                    = { .event = EDGEHOG_OTA_CONFIRM_REBOOT_EVENT };
                zbus_chan_pub(&edgehog_ota_chan, &ota_chan_event, K_SECONDS(1));
                break;
            case EDGEHOG_OTA_CONFIRM_REBOOT_EVENT:
                LOG_WRN("To subscriber -> EDGEHOG_OTA_CONFIRM_REBOOT_EVENT");
                break;
            case EDGEHOG_OTA_FAILED_EVENT:
                LOG_WRN("To subscriber -> EDGEHOG_OTA_FAILED_EVENT");
                break;
            case EDGEHOG_OTA_SUCCESS_EVENT:
                LOG_WRN("To subscriber -> EDGEHOG_OTA_SUCCESS_EVENT");
                break;
            default:
                LOG_WRN("To subscriber -> EDGEHOG_OTA_INVALID_EVENT");
        }
    }
}
#endif

static void astarte_connection_callback(astarte_device_connection_event_t event)
{
    ARG_UNUSED(event);
    LOG_INF("Astarte device connected.");
    atomic_set_bit(&device_threads_flags, DEVICE_THREADS_FLAGS_ASTARTE_CONNECTED);
}

static void astarte_disconnection_callback(astarte_device_disconnection_event_t event)
{
    ARG_UNUSED(event);
    LOG_INF("Astarte device disconnected");
    atomic_clear_bit(&device_threads_flags, DEVICE_THREADS_FLAGS_ASTARTE_CONNECTED);
}

static void stats_thread_entry_point(void *arg1, void *arg2, void *arg3)
{
    while (!atomic_test_bit(&device_threads_flags, DEVICE_THREADS_FLAGS_TERMINATION)) {
        k_timepoint_t timepoint = sys_timepoint_calc(K_SECONDS(5));

        int rc;
#ifdef CONFIG_ENABLE_TRANSMISSION
        astarte_result_t ares = ASTARTE_RESULT_OK;
#endif

        if (!atomic_test_bit(&device_threads_flags, DEVICE_THREADS_FLAGS_ASTARTE_CONNECTED)) {
            LOG_ERR("Skipping stats transmission, Astarte is not connected.");
            k_sleep(sys_timepoint_timeout(timepoint));
            continue;
        }

        int64_t timestamp_ms = 0;
        struct timespec tspec;
        rc = clock_gettime(CLOCK_REALTIME, &tspec);
        if (rc != 0) {
            LOG_ERR("Failed getting time.");
        }
        timestamp_ms = (int64_t) tspec.tv_sec * MSEC_PER_SEC + (tspec.tv_nsec / NSEC_PER_MSEC);

#ifdef CONFIG_ENABLE_TRANSMISSION
        astarte_device_handle_t astarte_device = edgehog_device_get_astarte_device(edgehog_device);
#endif

        static uint64_t prev_total_cycles = 0U; // Non idle cycles
        static uint64_t prev_execution_cycles = 0U; // Sum of idle + non idle cycles

        k_thread_runtime_stats_t stats;
        rc = k_thread_runtime_stats_cpu_get(0, &stats);
        if (rc) {
            LOG_ERR("Failed reading CPU stats (%d)", rc);
        } else {
            double cpu_usage = 100.0f * (stats.total_cycles - prev_total_cycles)
                / (stats.execution_cycles - prev_execution_cycles);
            prev_total_cycles = stats.total_cycles;
            prev_execution_cycles = stats.execution_cycles;

            LOG_INF("CPU usage: %.2lf %%", cpu_usage);

#ifdef CONFIG_ENABLE_TRANSMISSION
            ares = astarte_device_send_individual(astarte_device, com_example_poc_CpuMetrics.name,
                "/loadavg", astarte_data_from_double(cpu_usage), &timestamp_ms);
            if (ares != ASTARTE_RESULT_OK) {
                LOG_ERR("Astarte device transmission failure.");
            }
#else
            LOG_INF("Data transmission is disabled.");
#endif
        }

#ifdef CONFIG_CPU_TEMP_SENSOR
        rc = sensor_sample_fetch(die_temp_sensor);
        if (rc) {
            LOG_ERR("Failed to fetch temperature sample (%d)", rc);
            return;
        }

        struct sensor_value die_temp_val;
        rc = sensor_channel_get(die_temp_sensor, SENSOR_CHAN_DIE_TEMP, &die_temp_val);
        if (rc) {
            LOG_ERR("Failed to get temperature reading (%d)", rc);
            return;
        }

        double die_temp = sensor_value_to_double(&die_temp_val);
        LOG_INF("CPU Die temperature: %.1f °C", die_temp);

#ifdef CONFIG_ENABLE_TRANSMISSION
        ares = astarte_device_send_individual(astarte_device, com_example_poc_CpuTemp.name, "/temp",
            astarte_data_from_double(die_temp), &timestamp_ms);
        if (ares != ASTARTE_RESULT_OK) {
            LOG_ERR("Astarte device transmission failure.");
        }
#else
        LOG_INF("Data transmission is disabled.");
#endif
#endif
#ifdef CONFIG_AMBIENT_TEMP_SENSOR
        rc = sensor_sample_fetch_chan(ambient_temp_sensor, SENSOR_CHAN_AMBIENT_TEMP);
        if (rc) {
            LOG_ERR("Failed to fetch temperature sample (%d)", rc);
            return;
        }

        struct sensor_value ambient_temp_val;
        rc = sensor_channel_get(ambient_temp_sensor, SENSOR_CHAN_AMBIENT_TEMP, &ambient_temp_val);
        if (rc) {
            LOG_ERR("Failed to get temperature reading (%d)", rc);
            return;
        }

        double ambient_temp = sensor_value_to_double(&ambient_temp_val);
        LOG_INF("Ambient temperature: %.1f °C", ambient_temp);

#ifdef CONFIG_ENABLE_TRANSMISSION
        ares = astarte_device_send_individual(astarte_device, com_example_poc_AmbientTemp.name,
            "/temp", astarte_data_from_double(ambient_temp), &timestamp_ms);
        if (ares != ASTARTE_RESULT_OK) {
            LOG_ERR("Astarte device transmission failure.");
        }
#else
        LOG_INF("Data transmission is disabled.");
#endif
#endif

        k_sleep(sys_timepoint_timeout(timepoint));
    }
}
