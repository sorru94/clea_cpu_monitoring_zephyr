# Clea CPU metrics sample app for i.MX RT1064

This sample application is a simple demo app for the Clea platform on the i.MX RT1064 EVK board.
The sample application connects the board to Clea enabling IoT management and displays a simple
custom application for the Clea platform that collects CPU usage and temperature statistics
and displays them on a web based GUI.

## Setup the workspace

Start by creating a new workspace folder and a venv where `west` will reside.
```sh
mkdir ~/clea_cpu_monitor_zephyrproject && cd ~/clea_cpu_monitor_zephyrproject
python3 -m venv .venv
source .venv/bin/activate
pip install west
```

Initalize the workspace as a Zephyr project.
```sh
west init -m git@github.com:secomind/clea_cpu_monitoring_zephyr --mr master
west update
```

Install the required dependencies
```sh
west zephyr-export
west packages pip --install
west packages pip --install -- -r ./clea_cpu_monitoring_zephyr/scripts/requirements.txt
```

## Configure the application

The following entries should be modified in the `proj.conf` file.

```kconfig
CONFIG_ASTARTE_DEVICE_SDK_HOSTNAME="<HOSTNAME>"
CONFIG_ASTARTE_DEVICE_SDK_REALM_NAME="<REALM_NAME>"

CONFIG_ASTARTE_DEVICE_ID="<DEVICE_ID>"
CONFIG_ASTARTE_CREDENTIAL_SECRET="<CREDENTIAL_SECRET>"
```

Where `<DEVICE_ID>` is the device ID of the device you would like to use in the sample, `<HOSTNAME>`
is the hostname for your Astarte instance, `<REALM_NAME>` is the name of your testing realm and
`<CREDENTIAL_SECRET>` is the credential secret obtained through the manual registration

## Build and flash the sample

You can now build the sample with the following command:
```sh
west build --sysbuild -p -b mimxrt1064_evk clea_cpu_monitoring_zephyr/app/
```
Or if choosing the FRDM-RW612 board:
```sh
west build --sysbuild -p -b frdm_rw612 clea_cpu_monitoring_zephyr/app -DEXTRA_CONF_FILE="prj-wifi.conf"
```

Then flash it with the command:
```sh
west flash --runner=linkserver
```

### Configuration from flash

Astarte devices are authenticated to the cloud instance using a credential secret and device ID
combination. For various reason it might be inconvenient and unsafe to store such information
within the binary file of your application.
This sample provides the users with two methods to handle sensitive information:
1. The user can provide all sensitive data through the kconfig options. Such data will be embedded
in the final application binary.
2. The user can embed all sensitive data within a separate partition, which can be flashed
independently. The sample will then read the sensitive data from the partition and the application
binary will not contain sensitive or device-specific information. Meaning it will be possible
to flash the same binary file on multiple separate devices.

The first method is very straight forward and enabled by default. The user should only change
the `CONFIG_ASTARTE_DEVICE_ID` and `CONFIG_ASTARTE_CREDENTIAL_SECRET` options in the `prj.conf`.

However, to use the flash partition a couple of extra steps will be required.
First, install the `littlefs-python` tool.
```bash
pip install littlefs-python
```
Next, update the json file `config_lfs/configuration.json` with your desired options and
create the new partition.
```bash
littlefs-python create app/config_lfs/ app/lfs.bin --block-size 4096 --block-count 6
```
Finally, flash the partition on your device using your debugger of choice. The produced `lfs.bin`
should be flashed at address `0xBE40000`.
For example using J-Link for the FRDM RW612 bard the following command will do the trick.
```bash
JLinkExe -Device RW612 -if SWD -Speed 4000
connect
LoadBin app/lfs.bin 0xBE40000
reset
exit
```
Now your device has permanently been flashed with the credentials. You can enable the
`CONFIG_GET_CONFIG_FROM_FLASH` option in the kconfig and flash your application as you would normally
do. The standard `west flash` command will only flash the application partition and not the
custom partition used for the credentials.

The WiFi SSID and password can also be flashed on the devie in this manner.

### Update WiFi configuration through UART

When WiFi credentials are stored in flash they can be updated through an UART shell command.
Open a shell with minicom.
```bash
minicom -D /dev/ttyACM0 -b 115200
```
Run the command to update the wifi config
```bash
wifi update-credentials -s ssid -p pwd
```
