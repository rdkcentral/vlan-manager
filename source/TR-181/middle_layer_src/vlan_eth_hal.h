/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 Sky
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * Copyright [2014] [Cisco Systems, Inc.]
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/
/**********************************************************************

    module: vlan_eth_hal.h

        For CCSP Component:  VLAN Termination HAL Layer

    ---------------------------------------------------------------

    description:

        This header file gives the function call prototypes and
        structure definitions used for the RDK-Broadband
        VLAN abstraction layer

        NOTE:
        THIS VERSION IS AN EARLY DRAFT INTENDED TO GET COMMENTS FROM COMCAST.
        TESTING HAS NOT YET BEEN COMPLETED.

    ---------------------------------------------------------------

    environment:

        This HAL layer is intended to support VLAN drivers
        through the System Calls.

    ---------------------------------------------------------------

    author:

**********************************************************************/

#ifndef __VLAN_ETH_HAL_H__
#define __VLAN_ETH_HAL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(VLAN_MANAGER_HAL_ENABLED)
#include "json_hal_common.h"
#endif
#ifndef TRUE
#define TRUE     1
#endif

#ifndef FALSE
#define FALSE    0
#endif

#ifndef ENABLE
#define ENABLE   1
#endif

#ifndef RETURN_OK
#define RETURN_OK   0
#endif

#ifndef RETURN_ERR
#define RETURN_ERR   -1
#endif

/* * TR181 DML Params */
#define VLAN_ETH_LINK          "Device.Ethernet.Link.%d."
#define VLAN_ETH_LINK_ENABLE   "Device.Ethernet.Link.%d.Enable"
#define VLAN_ETH_LINK_NAME     "Device.Ethernet.Link.%d.Name"
#define VLAN_ETH_LINK_STATUS   "Device.Ethernet.Link.%d.Status"

#define VLAN_ETH_TERMINATION_ALIAS "Device.Ethernet.VLANTermination.%d.Alias"
#define VLAN_ETH_TERMINATION_NAME "Device.Ethernet.VLANTermination.%d.Name"
#define VLAN_ETH_TERMINATION_VLANID "Device.Ethernet.VLANTermination.%d.VLANID"
#define VLAN_ETH_TERMINATION_TPID "Device.Ethernet.VLANTermination.%d.TPID"
#if defined(VLAN_MANAGER_HAL_ENABLED)
#define WANIF_ETH_MARKING_SKBPORT "Device.Ethernet.Link.%d.X_RDK_Marking.%d.SKBPort"
#define WANIF_ETH_MARKING_PRIORITYMARK "Device.Ethernet.Link.%d.X_RDK_Marking.%d.EthernetPriorityMark"
#endif

/**********************************************************************
                STRUCTURE DEFINITIONS
**********************************************************************/

typedef struct _VLAN_SKB_CONFIG
{
     char alias[32]; /* Indicates DATA or VOICE */
     unsigned int skbMark;  /* SKB Marking Value. */
     unsigned int skbPort; /* SKB Marking Port */
     unsigned int skbEthPriorityMark; /* SKB Ethernet Priority. */
}vlan_skb_config_t;

// Structure for VLAN configuration
typedef struct _vlan_configuration
{
     unsigned int IfaceInstanceNumber; /* Instance number of interface */
     char BaseInterface[64]; /* BaseInterface like dsl0, fast0 */
     char L2Interface[64]; /* L2interface like eth3, ptm0 */
     char L3Interface[64]; /* L3 interface like erouter0. */
     int VLANId; /* Vlan Id */
     unsigned int TPId; /* Vlan tag protocol identifier. */
     unsigned int skbMarkingNumOfEntries; /* Number of SKB marking entries. */
     vlan_skb_config_t *skb_config; /* SKB Marking Data. */
} vlan_configuration_t;

/**********************************************************************
 *
 *  VLAN ETH HAL function prototypes
 *
***********************************************************************/

#if defined(VLAN_MANAGER_HAL_ENABLED)
/**
* @description This HAL is used to initialize the vlan hal client. Connected to rpc
* server to send/receive data.
* @param vlan_configuration_t config - vlanconfiguration data
*
* @return The status of the operation.
* @retval RETURN_OK if successful (or) RETURN_OK If interface is already exist with given config data
* @retval RETURN_ERR if any error is detected
*
* @execution Synchronous.
* @sideeffect None.
*
*/
int vlan_eth_hal_init();

/**
* @description This HAL is used to create an vlan interface and assign the vlanID. This
* HAL also used to reconfigure the vlan interface incase if any update in SKB marking configuration.
* This is being handled by `config->doReconfigure` flag in the vlan_configuration_t. If this flag TRUE,
* indicates, reconfigure required and it will update vlan rules on the existing interface. If it FALSE,
* create and configure the vlan interface.
* @param vlan_configuration_t config - vlanconfiguration data
*
* @return The status of the operation.
* @retval RETURN_OK if successful (or) RETURN_OK If interface is already exist with given config data
* @retval RETURN_ERR if any error is detected
*
* @execution Synchronous.
* @sideeffect None.
*
*/
int vlan_eth_hal_createInterface(vlan_configuration_t *config);

/**
* @description This HAL API is used to configure the vlan interface incase if any update in SKB marking configuration.

* @param vlan_configuration_t config - vlanconfiguration data
* @return The status of the operation.
* @retval RETURN_OK if successful (or) RETURN_OK If interface is already exist with given config data
* @retval RETURN_ERR if any error is detected
*
* @execution Synchronous.
* @sideeffect None.
*
*/
int vlan_eth_hal_setMarkings(vlan_configuration_t *config);
#endif // VLAN_MANAGER_HAL_ENABLED

/**
* @description This HAL is used to deassociate an existing vlan interface
* @param const char *vlan_ifname - interface name
*
* @return The status of the operation.
* @retval RETURN_OK if successful (or) RETURN_OK If interface is not exist
* @retval RETURN_ERR if any error is detected
*
* @execution Synchronous.
* @sideeffect None.
*
*/
int vlan_eth_hal_deleteInterface(char *ifname, int instanceNumber);

#endif /*__VLAN_ETH_HAL_H__*/
