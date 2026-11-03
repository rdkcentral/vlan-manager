# VLAN Manager

## Overview

VLAN Manager (`vlan-manager`) is an RDK-B software component that creates, configures, and monitors VLAN interfaces on the device. It exposes the TR-181 data model subtrees `Device.X_RDK_Ethernet.Link.*` and `Device.X_RDK_Ethernet.VLANTermination.*` to the platform.

VLAN Manager runs as a background daemon process and is responsible for:

- Registering the TR-181 data model with the CCSP message bus.
- Creating and removing tagged VLAN interfaces (`VLANTermination`) on a base Ethernet interface.
- Creating and removing untagged VLAN links (`Link`) and applying priority markings.
- Reporting VLAN interface status to the WAN component.
- Optionally delegating interface and marking configuration to a JSON HAL server.

---

## Features

- TR-181 data model exposure (`Device.X_RDK_Ethernet.Link.*`, `Device.X_RDK_Ethernet.VLANTermination.*`).
- Tagged VLAN interface creation and deletion, with VLAN ID and TPID configuration.
- Untagged VLAN link handling, including priority tagging control.
- Egress priority marking table (`X_RDK_Marking`) per Ethernet link.
- Interface status polling and status notification to the WAN component.
- Optional JSON HAL client mode, with schema-defined requests.
- Optional systemd `sd_notify` support for supervised startup (when built with `ENABLE_SD_NOTIFY`).

---

## Build Instructions

### Prerequisites

- RDK-B build environment.
- CCSP message bus libraries (`ccsp_common`), `rdkloggers`, `libprivilege`, and `secure_wrapper`.
- `dbus-1` version 1.6.18 or later.
- `hal_platform`, `syscfg`, and `sysevent` libraries (middle layer).
- JSON HAL client (`json_hal_client.h` and its library), when `VLAN_MANAGER_HAL_ENABLED` is defined.

### Build Process

VLAN Manager uses Autotools (`configure.ac`, `Makefile.am`).

```bash
autoreconf -i
./configure --enable-notify
make
```

The `--enable-notify` option defines `ENABLE_SD_NOTIFY`. Without it, `sd_notify` support is not built.

---

## Documentation

### Architecture

- [architecture/component-diagram.md](architecture/component-diagram.md)

### Components

- [components/ssp.md](components/ssp.md)
- [components/data-model-layer.md](components/data-model-layer.md)
- [components/vlan-termination.md](components/vlan-termination.md)
- [components/eth-link.md](components/eth-link.md)
- [components/hal-abstraction.md](components/hal-abstraction.md)

### Configuration

- [configuration-guide.md](configuration-guide.md)

---

## Directory Structure

```text
vlan-manager/
├── cfg/                                    # Autotools auxiliary files
│   └── Makefile.am
│
├── config/                                 # Runtime configuration files
│   ├── vlan_manager_conf.json
│   └── RdkVlanManager.xml
│
├── docs/                                   # Documentation
│   ├── architecture/
│   │   └── component-diagram.md
│   ├── components/
│   │   ├── data-model-layer.md
│   │   ├── eth-link.md
│   │   ├── hal-abstraction.md
│   │   ├── ssp.md
│   │   └── vlan-termination.md
│   ├── configuration-guide.md
│   └── README.md
│
├── hal_schema/                             # HAL schema definition and examples
│   ├── ethlinkvlanterm_hal_schema.json
│   ├── example_getParameters.json
│   ├── example_getParametersResponse.json
│   ├── example_getSchema_msg.json
│   ├── example_getSchemaResponse_msg.json
│   ├── example_result_msg.json
│   └── example_setParameters.json
│
├── source/
│   ├── Makefile.am
│   │
│   ├── RdkVlanManager/                     # SSP process layer
│   │   ├── Makefile.am
│   │   ├── ssp_action.c
│   │   ├── ssp_global.h
│   │   ├── ssp_internal.h
│   │   ├── ssp_main.c
│   │   ├── ssp_messagebus_interface.c
│   │   └── ssp_messagebus_interface.h
│   │
│   └── TR-181/                             # TR-181 data model implementation
│       ├── Makefile.am
│       ├── include/
│       │   ├── ethernet_apis.h
│       │   └── vlan_apis.h
│       └── middle_layer_src/
│           ├── ethernet_apis.c
│           ├── ethernet_dml.c
│           ├── ethernet_dml.h
│           ├── ethernet_internal.c
│           ├── ethernet_internal.h
│           ├── plugin_main.c
│           ├── plugin_main.h
│           ├── plugin_main_apis.c
│           ├── plugin_main_apis.h
│           ├── vlan_apis.c
│           ├── vlan_dml.c
│           ├── vlan_dml.h
│           ├── vlan_eth_hal.c
│           ├── vlan_eth_hal.h
│           ├── vlan_internal.c
│           ├── vlan_internal.h
│           ├── vlan_mgr_apis.h
│           ├── vlan_mgr_apis_busutil.h
│           ├── vlan_mgr_dml_api_common.h
│           └── Makefile.am
│
├── .github/                                # CODEOWNERS and CI workflows
├── configure.ac
├── CONTRIBUTING.md
├── COPYING
├── LICENSE
├── Makefile.am
├── NOTICE
└── README.md
```

---

## Quick Start

### Building the Component

```bash
autoreconf -i
./configure --enable-notify
make
```

### Configuration

Place the HAL configuration file at the target path:

- `/etc/rdk/conf/vlan_manager_conf.json`

The configuration file references the HAL schema. The packaged value is `/etc/rdk/schemas/ethlinkvlanterm_hal_schema.json`. See [configuration-guide.md](configuration-guide.md) for the full list of settings.

### Key Interfaces

| Interface                | Purpose                                               | Notes                                                           |
| ------------------------ | ----------------------------------------------------- | --------------------------------------------------------------- |
| Message Bus (D-Bus)      | Component registration, data model access             | Component name `com.cisco.spvtg.ccsp.vlanmanager`               |
| WAN component (CCSP bus) | VLAN status propagation (`<path>.VlanStatus`)         | Component name is defined by `WAN_COMPONENT_NAME` in the source |
| JSON HAL RPC (TCP)       | VLAN interface, marking, and delete requests          | Used when the HAL mode is enabled; port set in JSON config      |
| Linux networking         | VLAN interface creation with `ip link` (non-HAL mode) | Used when the HAL mode is not enabled                           |

---

For system architecture, component internals, and data flow details, see [architecture/component-diagram.md](architecture/component-diagram.md).
