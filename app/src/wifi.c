/*
 * (C) Copyright 2025, SECO Mind Srl
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi.h"

#include <zephyr/kernel.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/shell/shell.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi, CONFIG_APP_LOG_LEVEL); // NOLINT

#include "sample_config.h"

/************************************************
 *        Defines, constants and typedef        *
 ***********************************************/

#define WIFI_SHELL_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_shell_mgmt_cb;
static struct net_mgmt_event_callback ipv4_cb;

static K_SEM_DEFINE(wifi_connected, 0, 1);
static K_SEM_DEFINE(ipv4_address_obtained, 0, 1);

/************************************************
 *       Callbacks declaration/definition       *
 ***********************************************/

static void wifi_mgmt_event_handler(
    struct net_mgmt_event_callback *event_cb, uint64_t mgmt_event, struct net_if *iface)
{
    const struct wifi_status *status = (const struct wifi_status *) event_cb->info;
    switch (mgmt_event) {
        case NET_EVENT_WIFI_CONNECT_RESULT:
            if (status->status) {
                LOG_DBG("WiFi connection request failed (%d)", status->status);
                k_sem_take(&wifi_connected, K_NO_WAIT);
            } else {
                LOG_DBG("WiFi connected");
                k_sem_give(&wifi_connected);
            }
            break;
        case NET_EVENT_WIFI_DISCONNECT_RESULT:
            k_sem_take(&wifi_connected, K_NO_WAIT);
            if (status->status) {
                LOG_DBG("WiFi disconnection error, status: (%d)", status->status);
            } else {
                LOG_DBG("WiFi disconnected");
            }
            break;
        default:
            break;
    }
}

static void ipv4_mgmt_event_handler(
    struct net_mgmt_event_callback *event_cb, uint64_t mgmt_event, struct net_if *iface)
{
    (void) event_cb;
    (void) iface;

    switch (mgmt_event) {

        case NET_EVENT_IPV4_ADDR_ADD:
            LOG_DBG("Network event: NET_EVENT_IPV4_ADDR_ADD."); // NOLINT
            k_sem_give(&ipv4_address_obtained);
            break;

        case NET_EVENT_IPV4_ADDR_DEL:
            LOG_DBG("Network event: NET_EVENT_IPV4_ADDR_DEL."); // NOLINT
            k_sem_take(&ipv4_address_obtained, K_NO_WAIT);
            break;

        default:
            break;
    }
}

static int cmd_update_credentials(const struct shell *sh, size_t argc, char **argv)
{
    char ssid[SAMPLE_CONFIG_WIFI_MAX_STRINGS] = { 0 };
    char pwd[SAMPLE_CONFIG_WIFI_MAX_STRINGS] = { 0 };
    int snprintf_rc;
    int opt;
    int opt_index = 0;
    struct getopt_state *state;
    static const struct option long_options[] = { { "ssid", required_argument, 0, 's' },
        { "passphrase", required_argument, 0, 'p' }, { 0, 0, 0, 0 } };

    while ((opt = getopt_long(argc, argv, "s:p:", long_options, &opt_index)) != -1) {
        state = getopt_state_get();
        switch (opt) {
            case 's':
                snprintf_rc = snprintf(ssid, ARRAY_SIZE(ssid), "%s", state->optarg);
                if ((snprintf_rc < 0) || (snprintf_rc >= ARRAY_SIZE(ssid))) {
                    LOG_ERR("Error copying the SSID parameter.");
                    return -1;
                }
                break;
            case 'p':
                snprintf_rc = snprintf(pwd, ARRAY_SIZE(pwd), "%s", state->optarg);
                if ((snprintf_rc < 0) || (snprintf_rc >= ARRAY_SIZE(pwd))) {
                    LOG_ERR("Error copying the passkey parameter.");
                    return -1;
                }
                break;
            case '?':
            default:
                break;
        }
    }

    int rc = sample_config_update_wifi_creds(ssid, pwd);
    if (rc != 0) {
        shell_print(sh, "Failed updating WiFi credentials.");
    }
    shell_print(sh, "WiFi credentials updated.");
    shell_print(sh, "Please reboot the board for the changes to take effect.");
    return 0;
}

static int cmd_show_ssid(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    char ssid[SAMPLE_CONFIG_WIFI_MAX_STRINGS] = { 0 };
    int rc = sample_config_get_wifi_ssid(ssid);
    if (rc != 0) {
        shell_print(sh, "Failed getting WiFi SSID.");
    } else {
        shell_print(sh, "%s", ssid);
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(wifi_commands,
    SHELL_CMD_ARG(update - credentials, NULL,
        " Update WiFi credentials in Fash:\n"
        "-s --ssid=<SSID>\n"
        "-p --passphrase=<PSK> (valid only for secure SSIDs)\n",
        cmd_update_credentials, 5, 0),
    // SHELL_CMD(update-credentials, NULL, "Update WiFi credentials.", cmd_update_credentials),
    SHELL_CMD(show - ssid, NULL, "Show used WiFi SSID.", cmd_show_ssid), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(wifi, &wifi_commands, "WiFi commands", NULL);

/************************************************
 *         Global functions definitions         *
 ***********************************************/

void app_wifi_init(void)
{
    net_mgmt_init_event_callback(
        &wifi_shell_mgmt_cb, wifi_mgmt_event_handler, WIFI_SHELL_MGMT_EVENTS);
    net_mgmt_init_event_callback(
        &ipv4_cb, ipv4_mgmt_event_handler, NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IPV4_ADDR_DEL);

    net_mgmt_add_event_callback(&wifi_shell_mgmt_cb);
    net_mgmt_add_event_callback(&ipv4_cb);
}

int app_wifi_connect(const char *ssid, enum wifi_security_type sec, const char *psk)
{
    LOG_INF("Connecting through wifi..."); // NOLINT

    struct net_if *iface = net_if_get_wifi_sta();
    struct wifi_connect_req_params cnx_params = { 0 };
    int ret;

    cnx_params.band = WIFI_FREQ_BAND_UNKNOWN;
    cnx_params.channel = WIFI_CHANNEL_ANY;
    cnx_params.security = WIFI_SECURITY_TYPE_NONE;
    cnx_params.mfp = WIFI_MFP_OPTIONAL;

    cnx_params.ssid = ssid;
    cnx_params.ssid_length = strlen(cnx_params.ssid);
    cnx_params.security = sec;
    cnx_params.psk = psk;
    cnx_params.psk_length = strlen(cnx_params.psk);

    ret = net_mgmt(
        NET_REQUEST_WIFI_CONNECT, iface, &cnx_params, sizeof(struct wifi_connect_req_params));
    if (ret) {
        LOG_ERR("Connection request failed with error: %d", ret);
        return -ENOEXEC;
    }

    LOG_INF("Waiting for WiFi to be connected.");
    while (k_sem_count_get(&wifi_connected) == 0) {
        k_sleep(K_MSEC(200));
    }

    net_dhcpv4_start(iface);

    LOG_INF("Waiting for an IPv4 address (DHCP)."); // NOLINT
    while (k_sem_count_get(&ipv4_address_obtained) == 0) {
        k_sleep(K_MSEC(200));
    }

    LOG_INF("WiFi ready..."); // NOLINT

    return 0;
}
