# VLAN Manager Configuration Guide

## Overview

VLAN Manager is configured through a JSON runtime configuration file, a TR-181 data model XML file, PSM keys for persistent state, and build-time preprocessor macros. The runtime configuration controls the HAL client connection. The XML file defines the data model object tree. Build macros select whether VLAN operations use the HAL or Linux networking commands.

---

## Platform Integration Guide

This section is a task-oriented guide for integrating VLAN Manager onto a platform. The reference tables that follow provide full parameter details.

### Step 1 — Choose Your Operating Mode

VLAN Manager selects its VLAN operations at compile time.

| Mode       | Macro                        | VLAN Interface Operations                                                    |
| ---------- | ---------------------------- | ---------------------------------------------------------------------------- |
| Standard   | _(not defined)_              | Linux networking commands (`ip link add/delete/set`)                         |
| HAL        | `VLAN_MANAGER_HAL_ENABLED`   | JSON HAL client (`vlan_eth_hal.c`) sends requests to a vendor HAL server     |

Choose HAL mode when the platform provides a JSON HAL server that implements `hal_schema/ethlinkvlanterm_hal_schema.json`.

---

### Step 2 — Configure Build Flags

Set these preprocessor macros in your platform's build configuration:

| Macro                        | When to Set                                                                     |
| ---------------------------- | ------------------------------------------------------------------------------- |
| `VLAN_MANAGER_HAL_ENABLED`   | Platform provides the JSON HAL server. This macro is not set by `configure.ac`. |
| `ENABLE_SD_NOTIFY`           | Process is managed by systemd with `Type=notify` (`--enable-notify`)            |
| `_HUB4_PRODUCT_REQ_`         | Platform uses the HUB4 product configuration path in `vlan_internal.c`          |

---

### Step 3 — Deploy Runtime Configuration File

Copy the configuration file to the target filesystem.

| Source File                        | Target Path                              |
| ---------------------------------- | ---------------------------------------- |
| `config/vlan_manager_conf.json`    | `/etc/rdk/conf/vlan_manager_conf.json`   |

Edit the file for your platform:

```json
{
    "hal_schema_path": "/etc/rdk/schemas/ethlinkvlanterm_hal_schema.json",
    "server_port": 4096
}
```

- **`server_port`** — must match the port your JSON HAL server listens on.
- **`hal_schema_path`** — must point to the schema file that matches your HAL server version.

---

### Step 4 — Deploy HAL Schema File

Copy the schema file. The `hal_schema_path` setting in `vlan_manager_conf.json` points to this location.

| Source File                                      | Target Path                                              |
| ------------------------------------------------ | -------------------------------------------------------- |
| `hal_schema/ethlinkvlanterm_hal_schema.json`     | `/etc/rdk/schemas/ethlinkvlanterm_hal_schema.json`       |

> The schema file `description` field states that its contents must not be modified. Changes are made by the RDK community team.

---

### Step 5 — Deploy the Data Model XML

`config/RdkVlanManager.xml` defines the `Device.X_RDK_Ethernet.*` object tree. The data model code that consumes it (`dm_pack_datamodel.c`) is not present in this repository, so this document does not describe how the file is deployed.

---

### Step 6 — Verify Integration

```bash
# Confirm the process started and wrote its PID file
cat /var/tmp/vlanmanager.pid
```

The sentinel file `/tmp/vlanmanager_initialized` is created by `ssp_main.c` after `Cdm_Init` returns successfully.

---

### Step 7 — Adding New Data Model Parameters

To expose a new parameter under `Device.X_RDK_Ethernet.*`:

1. **`config/RdkVlanManager.xml`** — Declare the parameter with its type and access permissions.
2. **`source/TR-181/middle_layer_src/vlan_dml.c`** or **`ethernet_dml.c`** — Add the `GetParam*Value` and `SetParam*Value` handlers for the object.
3. **`source/TR-181/middle_layer_src/vlan_apis.c`** or **`ethernet_apis.c`** — Add the logic that backs the parameter, when it needs system or HAL access.
4. **`source/TR-181/include/vlan_apis.h`** or **`ethernet_apis.h`** — Declare the data types and functions used by the new logic.

---

## Configuration Architecture

```text
/etc/rdk/conf/
└── vlan_manager_conf.json

/etc/rdk/schemas/
└── ethlinkvlanterm_hal_schema.json

/tmp/
└── vlanmanager_initialized          (sentinel, created at startup)

/var/tmp/
└── vlanmanager.pid                  (PID file)

Source tree:
config/
├── vlan_manager_conf.json
└── RdkVlanManager.xml

hal_schema/
├── ethlinkvlanterm_hal_schema.json
└── example_*.json
```

---

## Configuration Reference

### JSON Configuration File

#### `vlan_manager_conf.json`

```json
{
    "hal_schema_path": "/etc/rdk/schemas/ethlinkvlanterm_hal_schema.json",
    "server_port": 4096
}
```

| Parameter         | Type    | Default                                                | Description                                                                                       |
| ----------------- | ------- | ------------------------------------------------------ | ------------------------------------------------------------------------------------------------- |
| `hal_schema_path` | string  | `/etc/rdk/schemas/ethlinkvlanterm_hal_schema.json`     | Absolute path to the JSON HAL schema file. Used by the HAL client to validate JSON RPC messages.  |
| `server_port`     | integer | `4096`                                                 | TCP port on which the JSON HAL server listens. VLAN Manager connects to this port.                |

The client reads the file from `VLAN_JSON_CONF_PATH`, which is `/etc/rdk/conf/vlan_manager_conf.json`, defined in `source/TR-181/middle_layer_src/vlan_eth_hal.c`.

### Build-Time Configuration

| Macro                        | Type         | Default      | Description                                                                                                                           |
| ---------------------------- | ------------ | ------------ | ------------------------------------------------------------------------------------------------------------------------------------- |
| `VLAN_MANAGER_HAL_ENABLED`   | preprocessor | Not defined  | When defined, VLAN termination and link operations use the JSON HAL client. When not defined, they use `ip link` commands.            |
| `COMCAST_VLAN_HAL_ENABLED`   | preprocessor | Not defined  | When defined, the untagged link path calls `EthLink_CreateBridgeInterface` instead of the macvlan path.                               |
| `ENABLE_SD_NOTIFY`           | preprocessor | Not defined  | When defined, sends `sd_notify(READY=1)` to systemd. Set by `--enable-notify` in `configure.ac`.                                     |
| `_HUB4_PRODUCT_REQ_`         | preprocessor | Not defined  | When defined, `vlan_internal.c` loads VLAN configuration from the `dmsb.vlanmanager.cfg.*` PSM keys using the router region.       |
| `_DEBUG`                     | preprocessor | Not defined  | When defined, the SSP process stays in the foreground and does not redirect stdio to `/dev/null`.                                   |

`--enable-notify` defines `ENABLE_SD_NOTIFY` through `configure.ac`.

### Component Identity Constants

These are defined in `source/RdkVlanManager/ssp_internal.h` and are not runtime-configurable.

| Constant                       | Value                                  | Description                         |
| ------------------------------ | -------------------------------------- | ----------------------------------- |
| `COMPONENT_ID_VLAN_MANAGER`    | `com.cisco.spvtg.ccsp.vlanmanager`     | CCSP component identifier           |
| `COMPONENT_NAME_VLAN_MANAGER`  | `com.cisco.spvtg.ccsp.vlanmanager`     | CCSP component name                 |
| `COMPONENT_VERSION_VLAN_MANAGER` | `1`                                  | CCSP component version              |
| `COMPONENT_PATH_VLAN_MANAGER`  | `/com/cisco/spvtg/ccsp/vlanmanager`    | D-Bus object path                   |
| `MESSAGE_BUS_CONFIG_FILE`      | `msg_daemon.cfg`                       | CCSP message bus configuration file |

### Runtime Paths

| Path                              | Description                                                                 |
| --------------------------------- | --------------------------------------------------------------------------- |
| `/var/tmp/vlanmanager.pid`        | PID file written by the SSP process at startup                              |
| `/tmp/vlanmanager_initialized`    | Sentinel file created after startup                                         |
| `/etc/debug.ini`                  | Logger configuration read by `rdk_logger_init`                              |

### Constants

| Constant                    | Value                                           | Defined In                                             | Description                                                    |
| --------------------------- | ----------------------------------------------- | ------------------------------------------------------ | -------------------------------------------------------------- |
| `VLAN_JSON_CONF_PATH`       | `/etc/rdk/conf/vlan_manager_conf.json`          | `source/TR-181/middle_layer_src/vlan_eth_hal.c`        | Path of the HAL client configuration file                      |
| `HAL_CONNECTION_RETRY_MAX_COUNT` | `10`                                       | `source/TR-181/middle_layer_src/vlan_eth_hal.c`        | Maximum connection checks while waiting for the HAL client     |
| `DEFAULT_VLAN_ID`           | `-1`                                            | `source/TR-181/include/ethernet_apis.h`                | VLAN ID used for an untagged link                              |

---

## PSM Keys

VLAN Manager reads its saved configuration from PSM at startup. The keys are read in `vlan_internal.c` and `ethernet_internal.c`.

### VLAN Termination

| Key                                           | Description                              |
| --------------------------------------------- | ---------------------------------------- |
| `dmsb.vlanmanager.ifcount`                    | Number of saved VLAN termination entries |
| `dmsb.vlanmanager.<n>.VlanEnable`             | Enable state of entry `<n>`              |
| `dmsb.vlanmanager.<n>.alias`                  | Alias of entry `<n>`                     |
| `dmsb.vlanmanager.<n>.name`                   | Interface name of entry `<n>`            |
| `dmsb.vlanmanager.<n>.lowerlayers`            | Lower layer path of entry `<n>`          |
| `dmsb.vlanmanager.<n>.vlanid`                 | VLAN ID of entry `<n>`                   |
| `dmsb.vlanmanager.<n>.tpid`                   | TPID of entry `<n>`                      |
| `dmsb.vlanmanager.<n>.baseinterface`          | Base interface of entry `<n>`            |
| `dmsb.vlanmanager.<n>.path`                   | Data model path of entry `<n>`           |

### Ethernet Link

| Key                                           | Description                              |
| --------------------------------------------- | ---------------------------------------- |
| `dmsb.ethlink.ifcount`                        | Number of saved Link entries             |
| `dmsb.ethlink.<n>.Enable`                     | Enable state of entry `<n>`              |
| `dmsb.ethlink.<n>.alias`                      | Alias of entry `<n>`                     |
| `dmsb.ethlink.<n>.name`                       | Interface name of entry `<n>`            |
| `dmsb.ethlink.<n>.lowerlayers`                | Lower layer path of entry `<n>`          |
| `dmsb.ethlink.<n>.macoffset`                  | MAC address offset of entry `<n>`        |
| `dmsb.ethlink.<n>.baseiface`                  | Base interface of entry `<n>`            |
| `dmsb.ethlink.<n>.path`                       | Data model path of entry `<n>`           |

The `_HUB4_PRODUCT_REQ_` branch in `vlan_internal.c` uses a separate set of keys: `dmsb.vlanmanager.cfg.count` and `dmsb.vlanmanager.cfg.<n>.{region, vlanid, tpid}`.

---

## TR-181 Data Model Configuration

The data model is defined in `config/RdkVlanManager.xml`. The file is loaded at startup by the data model registration layer.

### Data Model Namespace

```text
Device.X_RDK_Ethernet.
├── Link.{i}.                              (staticTable, max 128)
│   ├── Enable
│   ├── Status
│   ├── Alias
│   ├── Name
│   ├── LowerLayers
│   ├── X_RDK_BaseInterface
│   ├── MACAddress
│   ├── MACAddrOffSet
│   ├── PriorityTagging
│   ├── X_RDK_Refresh
│   └── X_RDK_Marking.{i}.                 (dynamicTable, max 128)
│       ├── SKBPort
│       └── EthernetPriorityMark
└── VLANTermination.{i}.                   (staticTable, max 128)
    ├── Enable
    ├── Status
    ├── Alias
    ├── Name
    ├── LastChange
    ├── LowerLayers
    ├── VLANID
    ├── TPID
    └── X_RDK_BaseInterface
```

For parameter behaviour, see [data-model-layer.md](components/data-model-layer.md), [vlan-termination.md](components/vlan-termination.md), and [eth-link.md](components/eth-link.md).

---

## Configuration Notes

- **HAL schema module name.** The schema defines the module as `ethvlanhal`. The example file `hal_schema/example_setParameters.json` uses `vlanhal`. Confirm the name your HAL server expects before deployment.
- **HAL schema path in the example.** The example file `hal_schema/example_getSchemaResponse_msg.json` shows `/etc/sky/hal_schemas/...`. The configuration file expects `/etc/rdk/schemas/ethlinkvlanterm_hal_schema.json`. Use the configuration file path.
- **`deleteObject`.** The schema lists a `deleteObject` action and the source defines a matching constant. The current code does not send this request.
- **`dm_pack_datamodel.c`.** `source/RdkVlanManager/Makefile.am` and `ssp_action.c` reference this file. It is not present in the repository.
