/*
 * (C) Copyright 2025, SECO Mind Srl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sample_config.h"

#include <stdint.h>
#include <stdio.h>

#if defined(CONFIG_GET_CONFIG_FROM_FLASH)
#include <zephyr/data/json.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(config_file, CONFIG_APP_LOG_LEVEL); // NOLINT

/************************************************
 *       Checks over configuration values       *
 ***********************************************/

#if !defined(CONFIG_GET_CONFIG_FROM_FLASH)
BUILD_ASSERT(sizeof(CONFIG_ASTARTE_DEVICE_ID) == ASTARTE_DEVICE_ID_LEN + 1,
    "Missing device ID in datastreams example");

BUILD_ASSERT(sizeof(CONFIG_ASTARTE_CREDENTIAL_SECRET) == ASTARTE_PAIRING_CRED_SECR_LEN + 1,
    "Missing credential secret in datastreams example");
#endif

/************************************************
 *        Defines, constants and typedef        *
 ***********************************************/

#if defined(CONFIG_GET_CONFIG_FROM_FLASH)
/* Matches LFS_NAME_MAX */
#define MAX_PATH_LEN 255
#define MAX_CONFIG_FILE_SIZE 4096

#define PARTITION_NODE DT_NODELABEL(lfs1)

#if DT_NODE_EXISTS(PARTITION_NODE)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);
#else /* PARTITION_NODE */
#error "Could not find the littlefs partition!"
#endif /* PARTITION_NODE */

struct fs_mount_t *mountpoint = &FS_FSTAB_ENTRY(PARTITION_NODE);
#endif

/************************************************
 *         Static functions declaration         *
 ***********************************************/

static int copy_configuration(const char *device_id, const char *credential_secret,
    const char *wifi_ssid, const char *wifi_pwd, struct sample_config *out_cfg);
#if defined(CONFIG_GET_CONFIG_FROM_FLASH)
static int read_configuration_file(
    char fname[static MAX_PATH_LEN], char fcontent[static MAX_CONFIG_FILE_SIZE]);
static int write_configuration_file(
    char fname[static MAX_PATH_LEN], char fcontent[static MAX_CONFIG_FILE_SIZE]);
static int parse_configuration_file(char *fcontent, struct sample_config *out_cfg);
static int update_wifi_configuration(char *fcontent,
    char new_ssid[static SAMPLE_CONFIG_WIFI_MAX_STRINGS],
    char new_pwd[static SAMPLE_CONFIG_WIFI_MAX_STRINGS], char *output_buffer, size_t buffer_size);
#endif

/************************************************
 * Global functions definition
 ***********************************************/

int sample_config_get(struct sample_config *cfg)
{
#if defined(CONFIG_GET_CONFIG_FROM_FLASH)
    int ret;
    char config_fname[MAX_PATH_LEN] = { 0 };
    char config_fcontent[MAX_CONFIG_FILE_SIZE] = { 0 };

    ret = snprintf(
        config_fname, sizeof(config_fname), "%s/configuration.json", mountpoint->mnt_point);
    if (ret >= sizeof(config_fname)) {
        LOG_ERR("FAIL: snprinf [rd:%d]", ret);
        goto out;
    }

    ret = read_configuration_file(config_fname, config_fcontent);
    if (ret < 0) {
        goto out;
    }
    LOG_DBG("%s read content:%s (bytes: %d)", config_fname, config_fcontent, ret);

    ret = parse_configuration_file(config_fcontent, cfg);
    if (ret != 0) {
        goto out;
    }

out:
    return (ret != 0) ? -1 : 0;
#else
    return copy_configuration(CONFIG_ASTARTE_DEVICE_ID, CONFIG_ASTARTE_CREDENTIAL_SECRET,
#if defined(CONFIG_WIFI)
        CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD,
#else
        NULL, NULL,
#endif
        cfg);
#endif
}
#if defined(CONFIG_WIFI)
int sample_config_get_wifi_ssid(char output[static SAMPLE_CONFIG_WIFI_MAX_STRINGS])
{
#if defined(CONFIG_GET_CONFIG_FROM_FLASH)
    struct sample_config cfg = { 0 };
    int config_get_rc = sample_config_get(&cfg);
    if (config_get_rc != 0) {
        return -1;
    }
    int snprintf_rc = snprintf(output, SAMPLE_CONFIG_WIFI_MAX_STRINGS, "%s", cfg.wifi_ssid);
#else
    int snprintf_rc
        = snprintf(output, SAMPLE_CONFIG_WIFI_MAX_STRINGS, "WiFi configured at build time.");
#endif
    if ((snprintf_rc < 0) || (snprintf_rc >= SAMPLE_CONFIG_WIFI_MAX_STRINGS)) {
        LOG_ERR("Error encoding the WiFi SSID.");
        return -1;
    }
    return 0;
}
int sample_config_update_wifi_creds(char ssid[static SAMPLE_CONFIG_WIFI_MAX_STRINGS],
    char pwd[static SAMPLE_CONFIG_WIFI_MAX_STRINGS])
{
    int ret;
    char config_fname[MAX_PATH_LEN] = { 0 };
    char config_fcontent[MAX_CONFIG_FILE_SIZE] = { 0 };
    char config_fcontent_updated[MAX_CONFIG_FILE_SIZE] = { 0 };

    ret = snprintf(
        config_fname, sizeof(config_fname), "%s/configuration.json", mountpoint->mnt_point);
    if (ret >= sizeof(config_fname)) {
        LOG_ERR("FAIL: snprinf [rd:%d]", ret);
        goto out;
    }

    ret = read_configuration_file(config_fname, config_fcontent);
    if (ret < 0) {
        LOG_ERR("FAIL: read configuration file [rd:%d]", ret);
        goto out;
    }
    LOG_DBG("%s read content:%s (bytes: %d)", config_fname, config_fcontent, ret);

    ret = update_wifi_configuration(
        config_fcontent, ssid, pwd, config_fcontent_updated, MAX_CONFIG_FILE_SIZE);
    if (ret != 0) {
        LOG_ERR("FAIL: updated wifi configuration [rd:%d]", ret);
        goto out;
    }
    ret = write_configuration_file(config_fname, config_fcontent_updated);
    if (ret < 0) {
        LOG_ERR("FAIL: write new wifi configuration [rd:%d]", ret);
        goto out;
    }
    LOG_DBG("%s", config_fcontent_updated);

out:
    return (ret < 0) ? -1 : 0;
}
#endif

/************************************************
 * Static functions definitions
 ***********************************************/

static int copy_configuration(const char *device_id, const char *credential_secret,
    const char *wifi_ssid, const char *wifi_pwd, struct sample_config *out_cfg)
{
    // Copy the received credential secret in the output buffer
    int snprintf_rc = snprintf(out_cfg->device_id, ARRAY_SIZE(out_cfg->device_id), "%s", device_id);
    if ((snprintf_rc < 0) || (snprintf_rc >= ARRAY_SIZE(out_cfg->device_id))) {
        LOG_ERR("Error extracting the device ID from the parsed json.");
        return -1;
    }
    snprintf_rc = snprintf(out_cfg->credential_secret, ARRAY_SIZE(out_cfg->credential_secret), "%s",
        credential_secret);
    if ((snprintf_rc < 0) || (snprintf_rc >= ARRAY_SIZE(out_cfg->credential_secret))) {
        LOG_ERR("Error extracting the credential secret from the parsed json.");
        return -1;
    }
#if defined(CONFIG_WIFI)
    snprintf_rc = snprintf(out_cfg->wifi_ssid, ARRAY_SIZE(out_cfg->wifi_ssid), "%s", wifi_ssid);
    if ((snprintf_rc < 0) || (snprintf_rc >= ARRAY_SIZE(out_cfg->wifi_ssid))) {
        LOG_ERR("Error extracting the WiFi SSID from the parsed json.");
        return -1;
    }
    snprintf_rc = snprintf(out_cfg->wifi_pwd, ARRAY_SIZE(out_cfg->wifi_pwd), "%s", wifi_pwd);
    if ((snprintf_rc < 0) || (snprintf_rc >= ARRAY_SIZE(out_cfg->wifi_pwd))) {
        LOG_ERR("Error extracting the WiFi password from the parsed json.");
        return -1;
    }
#else
    ARG_UNUSED(wifi_ssid);
    ARG_UNUSED(wifi_pwd);
#endif
    return 0;
}
#if defined(CONFIG_GET_CONFIG_FROM_FLASH)
static int read_configuration_file(
    char fname[static MAX_PATH_LEN], char fcontent[static MAX_CONFIG_FILE_SIZE])
{
    struct fs_file_t file;
    int ret;

    fs_file_t_init(&file);
    ret = fs_open(&file, fname, FS_O_READ);
    if (ret < 0) {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
        return ret;
    }

    ret = fs_read(&file, fcontent, MAX_CONFIG_FILE_SIZE);
    if (ret <= 0) {
        LOG_ERR("FAIL: read %s: [rd:%d]", fname, ret);
        goto out;
    }

out:
    int close_ret = fs_close(&file);
    if (close_ret < 0) {
        LOG_ERR("FAIL: close %s: %d", fname, close_ret);
        return close_ret;
    }

    return ret;
}
static int write_configuration_file(
    char fname[static MAX_PATH_LEN], char fcontent[static MAX_CONFIG_FILE_SIZE])
{
    struct fs_file_t file;
    int ret;

    fs_file_t_init(&file);
    ret = fs_open(&file, fname, FS_O_TRUNC | FS_O_WRITE);
    if (ret < 0) {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
        return ret;
    }

    ret = fs_write(&file, fcontent, strnlen(fcontent, MAX_CONFIG_FILE_SIZE) + 1);
    if (ret <= 0) {
        LOG_ERR("FAIL: write %s: [rd:%d]", fname, ret);
        goto out;
    }

out:
    int close_ret = fs_close(&file);
    if (close_ret < 0) {
        LOG_ERR("FAIL: close %s: %d", fname, close_ret);
        return close_ret;
    }

    return ret;
}

static int parse_configuration_file(char *fcontent, struct sample_config *out_cfg)
{
    // Define the structure of the expected JSON file
    struct full_json_s
    {
        const char *deviceID;
        const char *credentialSecret;
        const char *wifiSsid;
        const char *wifiPassword;
    };
    // Create the descriptors list containing each element of the struct
    static const struct json_obj_descr full_json_descr[] = {
        JSON_OBJ_DESCR_PRIM(struct full_json_s, deviceID, JSON_TOK_STRING),
        JSON_OBJ_DESCR_PRIM(struct full_json_s, credentialSecret, JSON_TOK_STRING),
        JSON_OBJ_DESCR_PRIM(struct full_json_s, wifiSsid, JSON_TOK_STRING),
        JSON_OBJ_DESCR_PRIM(struct full_json_s, wifiPassword, JSON_TOK_STRING),
    };
    // This is only for the outside level, nested elements should be checked individually.
    int expected_return_code = (1U << (size_t) ARRAY_SIZE(full_json_descr)) - 1;
    // Parse the json into a structure
    struct full_json_s parsed_json
        = { .deviceID = NULL, .credentialSecret = NULL, .wifiSsid = NULL, .wifiPassword = NULL };
    int64_t ret = json_obj_parse(
        fcontent, strlen(fcontent) + 1, full_json_descr, ARRAY_SIZE(full_json_descr), &parsed_json);
    if (ret != expected_return_code) {
        LOG_ERR("JSON Parse Error: %lld", ret);
        return -1;
    }
    if (!parsed_json.deviceID) {
        LOG_ERR("Parsed JSON is missing the deviceID field.");
        return -1;
    }
    if (!parsed_json.credentialSecret) {
        LOG_ERR("Parsed JSON is missing the credentialSecret field.");
        return -1;
    }
    if (!parsed_json.wifiSsid) {
        LOG_ERR("Parsed JSON is missing the wifiSsid field.");
        return -1;
    }
    if (!parsed_json.wifiPassword) {
        LOG_ERR("Parsed JSON is missing the wifiPassword field.");
        return -1;
    }

    // Copy the received credential secret in the output buffer
    return copy_configuration(parsed_json.deviceID, parsed_json.credentialSecret,
        parsed_json.wifiSsid, parsed_json.wifiPassword, out_cfg);
}
static int update_wifi_configuration(char *fcontent,
    char new_ssid[static SAMPLE_CONFIG_WIFI_MAX_STRINGS],
    char new_pwd[static SAMPLE_CONFIG_WIFI_MAX_STRINGS], char *output_buffer, size_t buffer_size)
{
    // Define the structure of the expected JSON file
    struct full_json_s
    {
        const char *deviceID;
        const char *credentialSecret;
        const char *wifiSsid;
        const char *wifiPassword;
    };

    // Create the descriptors list
    static const struct json_obj_descr full_json_descr[] = {
        JSON_OBJ_DESCR_PRIM(struct full_json_s, deviceID, JSON_TOK_STRING),
        JSON_OBJ_DESCR_PRIM(struct full_json_s, credentialSecret, JSON_TOK_STRING),
        JSON_OBJ_DESCR_PRIM(struct full_json_s, wifiSsid, JSON_TOK_STRING),
        JSON_OBJ_DESCR_PRIM(struct full_json_s, wifiPassword, JSON_TOK_STRING),
    };

    int expected_return_code = (1U << (size_t) ARRAY_SIZE(full_json_descr)) - 1;
    struct full_json_s parsed_json = { 0 };

    int64_t ret_parse = json_obj_parse(
        fcontent, strlen(fcontent) + 1, full_json_descr, ARRAY_SIZE(full_json_descr), &parsed_json);
    if (ret_parse != expected_return_code) {
        LOG_ERR("JSON Parse Error: %lld. Expected %d.", ret_parse, expected_return_code);
        return -1;
    }
    if (!parsed_json.deviceID) {
        LOG_ERR("Parsed JSON is missing the deviceID field.");
        return -1;
    }
    if (!parsed_json.credentialSecret) {
        LOG_ERR("Parsed JSON is missing the credentialSecret field.");
        return -1;
    }

    struct full_json_s updated_json = { .deviceID = parsed_json.deviceID,
        .credentialSecret = parsed_json.credentialSecret,
        .wifiSsid = new_ssid,
        .wifiPassword = new_pwd };
    int ret_encode = json_obj_encode_buf(
        full_json_descr, ARRAY_SIZE(full_json_descr), &updated_json, output_buffer, buffer_size);
    if (ret_encode < 0) {
        LOG_ERR("Failed to encode updated JSON.");
        return -1;
    }
    return 0;
}
#endif
