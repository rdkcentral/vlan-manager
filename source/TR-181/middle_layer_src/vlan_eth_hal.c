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

    module: vlan_eth_hal.c

    ---------------------------------------------------------------

    description:

        This sample implementation file gives the function call prototypes and
        structure definitions used for the RDK-Broadband
        Ethernet VLAN hardware abstraction layer


    ---------------------------------------------------------------

    environment:

        This HAL layer is intended to support Ethernet VLAN drivers
        through an open API.

    ---------------------------------------------------------------

**********************************************************************/

/*****************************************************************************
* STANDARD INCLUDE FILES
*****************************************************************************/
#include <limits.h>
/*****************************************************************************
* PROJECT-SPECIFIC INCLUDE FILES
*****************************************************************************/
#include "vlan_eth_hal.h"
#include "json_hal_client.h"
#include <json-c/json.h>
#include "ansc_platform.h"
/***************************************************************************************
* GLOBAL SYMBOLS
****************************************************************************************/
/****************************************************************************************/

#define VLAN_JSON_CONF_PATH "/etc/rdk/conf/vlan_manager_conf.json"

#define RPC_GET_PARAMETERS_REQUEST "getParameters"
#define RPC_SET_PARAMETERS_REQUEST "setParameters"
#define RPC_DELETE_OBJECT_REQUEST "deleteObject"
#define SCHEMA_FILE_BUFFER_SIZE 200000

#define CHECK(expr)                                                \
    if (!(expr))                                                   \
    {                                                              \
        CcspTraceError(("%s - %d Invalid parameter error \n!!!", __FUNCTION__, __LINE__)); \
        return RETURN_ERR;                                         \
    }

#define FREE_JSON_OBJECT(expr) \
    if(expr)                   \
    {                          \
        json_object_put(expr); \
    }

#define HAL_CONNECTION_RETRY_MAX_COUNT 10

#if defined(VLAN_MANAGER_HAL_ENABLED)
/* Intializer. */
INT vlan_eth_hal_init()
{
    int rc = RETURN_OK;
    rc = json_hal_client_init(VLAN_JSON_CONF_PATH);
    if (rc != RETURN_OK) {
        CcspTraceError(("%s - %d Failed to initialise json hal client library \n", __FUNCTION__, __LINE__));
        return RETURN_ERR;
    }
    rc = json_hal_client_run();
    if (rc != RETURN_OK) {
        CcspTraceError(("%s - %d Failed to start json hal client \n", __FUNCTION__, __LINE__));
        return RETURN_ERR;
    }

    /**
     * Make sure HAL client connected to server.
     * Here waits for 10seconds time to check whether connection established or not.
     * If connection not established within this time, returned error.
     */
    int retry_count = 0;
    int is_client_connected = 0;
    while (retry_count < HAL_CONNECTION_RETRY_MAX_COUNT) {
        if (!json_hal_is_client_connected()) {
            sleep (1);
            retry_count++;
        }else {
            CcspTraceInfo(("%s-%d Hal-client connected to the hal server \n", __FUNCTION__, __LINE__));
            is_client_connected = TRUE;
            break;
        }
    }

    if (is_client_connected != TRUE) {
         CcspTraceError(("Failed to connect to the hal server. \n"));
         return RETURN_ERR;
    }

    return RETURN_OK;
}

/* vlan_eth_hal_createInterface() */
int vlan_eth_hal_createInterface(vlan_configuration_t *config)
{
    json_object *jmsg = NULL;
    json_object *jreply_msg = NULL;
    json_bool status = FALSE;
    hal_param_t param;

    if (NULL == config)
    {
        CcspTraceError(("Error: Invalid arguement \n"));
        return RETURN_ERR;
    }

    jmsg = json_hal_client_get_request_header(RPC_SET_PARAMETERS_REQUEST);
    CHECK(jmsg);

    memset(&param, 0, sizeof(param));
    snprintf(param.name, sizeof(param.name), VLAN_ETH_TERMINATION_ALIAS, config->IfaceInstanceNumber);
    snprintf(param.value, sizeof(param.value), "%s", config->L2Interface);
    param.type = PARAM_STRING;
    json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);

    memset(&param, 0, sizeof(param));
    snprintf(param.name, sizeof(param.name), VLAN_ETH_TERMINATION_NAME, config->IfaceInstanceNumber);
    snprintf(param.value, sizeof(param.value), "%s", config->L3Interface);
    param.type = PARAM_STRING;
    json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);

    memset(&param, 0, sizeof(param));
    snprintf(param.name, sizeof(param.name), VLAN_ETH_TERMINATION_VLANID, config->IfaceInstanceNumber);
    snprintf(param.value, sizeof(param.value), "%d", config->VLANId);
    param.type = PARAM_INTEGER;
    json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);

    memset(&param, 0, sizeof(param));
    snprintf(param.name, sizeof(param.name), VLAN_ETH_TERMINATION_TPID, config->IfaceInstanceNumber);
    snprintf(param.value, sizeof(param.value), "%d", config->TPId);
    param.type = PARAM_UNSIGNED_INTEGER;
    json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);
    CcspTraceInfo(("%s-%d: skbMarkingNumOfEntries=%d \n", __FUNCTION__, __LINE__, (*config).skbMarkingNumOfEntries ));
    for (int i = 0; i < (*config).skbMarkingNumOfEntries; ++i)
    {
        memset(&param, 0, sizeof(param));
        snprintf(param.name, sizeof(param.name), WANIF_ETH_MARKING_SKBPORT, config->IfaceInstanceNumber, i+1);
        snprintf(param.value, sizeof(param.value), "%d", config->skb_config[i].skbPort);
        param.type = PARAM_UNSIGNED_INTEGER;
        json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);

        memset(&param, 0, sizeof(param));
        snprintf(param.name, sizeof(param.name), WANIF_ETH_MARKING_PRIORITYMARK, config->IfaceInstanceNumber, i+1);
        snprintf(param.value, sizeof(param.value), "%d", config->skb_config[i].skbEthPriorityMark);
        param.type = PARAM_INTEGER;
        json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);
    }
    CcspTraceInfo(("JSON Request message = %s \n", json_object_to_json_string_ext(jmsg, JSON_C_TO_STRING_PRETTY)));
    if( json_hal_client_send_and_get_reply(jmsg, &jreply_msg) != RETURN_OK)
    {
        CcspTraceError(("[%s][%d] RPC message failed \n", __FUNCTION__, __LINE__));
        FREE_JSON_OBJECT(jmsg);
        FREE_JSON_OBJECT(jreply_msg);
        return RETURN_ERR;
    }
    CHECK(jreply_msg);

    if (json_hal_get_result_status(jreply_msg, &status) == RETURN_OK)
    {
        if (status)
        {
            CcspTraceInfo(("%s - %d Set request is successful ", __FUNCTION__, __LINE__));
        }
        else
        {
            CcspTraceError(("%s - %d - Set request is failed \n", __FUNCTION__, __LINE__));
        }
    }
    else
    {
        CcspTraceError(("%s - %d Failed to get result status from json response, something wrong happened!!! \n", __FUNCTION__, __LINE__));
    }

    // Free json objects.
    FREE_JSON_OBJECT(jmsg);
    FREE_JSON_OBJECT(jreply_msg);

    return RETURN_OK;
}

/* vlan_eth_hal_setMarkings() */
int vlan_eth_hal_setMarkings(vlan_configuration_t *config)
{
    json_object *jmsg = NULL;
    json_object *jreply_msg = NULL;
    json_bool status = FALSE;
    hal_param_t param;

    if (NULL == config)
    {
        CcspTraceError(("Error: Invalid arguement \n"));
        return RETURN_ERR;
    }

    jmsg = json_hal_client_get_request_header(RPC_SET_PARAMETERS_REQUEST);
    CHECK(jmsg);

    memset(&param, 0, sizeof(param));
    snprintf(param.name, sizeof(param.name), VLAN_ETH_TERMINATION_ALIAS, config->IfaceInstanceNumber);
    snprintf(param.value, sizeof(param.value), "%s", config->L2Interface);
    param.type = PARAM_STRING;
    json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);

    memset(&param, 0, sizeof(param));
    snprintf(param.name, sizeof(param.name), VLAN_ETH_TERMINATION_NAME, config->IfaceInstanceNumber);
    snprintf(param.value, sizeof(param.value), "%s", config->L3Interface);
    param.type = PARAM_STRING;
    json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);

    memset(&param, 0, sizeof(param));
    snprintf(param.name, sizeof(param.name), VLAN_ETH_TERMINATION_VLANID, config->IfaceInstanceNumber);
    snprintf(param.value, sizeof(param.value), "%d", config->VLANId);
    param.type = PARAM_INTEGER;
    json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);

    memset(&param, 0, sizeof(param));
    snprintf(param.name, sizeof(param.name), VLAN_ETH_TERMINATION_TPID, config->IfaceInstanceNumber);
    snprintf(param.value, sizeof(param.value), "%d", config->TPId);
    param.type = PARAM_UNSIGNED_INTEGER;
    json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);
    CcspTraceInfo(("%s-%d: skbMarkingNumOfEntries=%d \n", __FUNCTION__, __LINE__, (*config).skbMarkingNumOfEntries ));
    for (int i = 0; i < (*config).skbMarkingNumOfEntries; ++i)
    {
        memset(&param, 0, sizeof(param));
        snprintf(param.name, sizeof(param.name), WANIF_ETH_MARKING_SKBPORT, config->IfaceInstanceNumber, i+1);
        snprintf(param.value, sizeof(param.value), "%d", config->skb_config[i].skbPort);
        param.type = PARAM_UNSIGNED_INTEGER;
        json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);

        memset(&param, 0, sizeof(param));
        snprintf(param.name, sizeof(param.name), WANIF_ETH_MARKING_PRIORITYMARK, config->IfaceInstanceNumber, i+1);
        snprintf(param.value, sizeof(param.value), "%d", config->skb_config[i].skbEthPriorityMark);
        param.type = PARAM_INTEGER;
        json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);
    }
    CcspTraceInfo(("JSON Request message = %s \n", json_object_to_json_string_ext(jmsg, JSON_C_TO_STRING_PRETTY)));
    if( json_hal_client_send_and_get_reply(jmsg, &jreply_msg) != RETURN_OK)
    {
        CcspTraceError(("[%s][%d] RPC message failed \n", __FUNCTION__, __LINE__));
        FREE_JSON_OBJECT(jmsg);
        FREE_JSON_OBJECT(jreply_msg);
        return RETURN_ERR;
    }
    CHECK(jreply_msg);

    if (json_hal_get_result_status(jreply_msg, &status) == RETURN_OK)
    {
        if (status)
        {
            CcspTraceInfo(("%s - %d Set request is successful ", __FUNCTION__, __LINE__));
        }
        else
        {
            CcspTraceError(("%s - %d - Set request is failed \n", __FUNCTION__, __LINE__));
        }
    }
    else
    {
        CcspTraceError(("%s - %d Failed to get result status from json response, something wrong happened!!! \n", __FUNCTION__, __LINE__));
    }

    // Free json objects.
    FREE_JSON_OBJECT(jmsg);
    FREE_JSON_OBJECT(jreply_msg);

    return RETURN_OK;
}
#endif //VLAN_MANAGER_HAL_ENABLED

/* vlan_eth_hal_deleteInterface() */
int vlan_eth_hal_deleteInterface(char *ifname, int instanceNumber)
{
    json_object *jmsg = NULL;
    json_object *jreply_msg = NULL;
    json_bool status = FALSE;
    hal_param_t param;

    if (NULL == ifname)
    {
        CcspTraceError(("Error: Invalid arguement \n"));
        return RETURN_ERR;
    }

    jmsg = json_hal_client_get_request_header(RPC_SET_PARAMETERS_REQUEST);
    CHECK(jmsg);

    memset(&param, 0, sizeof(param));
    snprintf(param.name, sizeof(param.name), VLAN_ETH_LINK_ENABLE, instanceNumber);
    snprintf(param.value, sizeof(param.value), "false");
    param.type = PARAM_BOOLEAN;
    json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);

    memset(&param, 0, sizeof(param));
    snprintf(param.name, sizeof(param.name), VLAN_ETH_LINK_NAME, instanceNumber);
    snprintf(param.value, sizeof(param.value), "%s", ifname);
    param.type = PARAM_STRING;
    json_hal_add_param(jmsg, SET_REQUEST_MESSAGE, &param);

    CcspTraceInfo(("JSON Request message = %s \n", json_object_to_json_string_ext(jmsg, JSON_C_TO_STRING_PRETTY)));

    if( json_hal_client_send_and_get_reply(jmsg, &jreply_msg) != RETURN_OK)
    {
        CcspTraceError(("[%s][%d] RPC message failed \n", __FUNCTION__, __LINE__));
        FREE_JSON_OBJECT(jmsg);
        FREE_JSON_OBJECT(jreply_msg);
        return RETURN_ERR;
    }
    CHECK(jreply_msg);

    if (json_hal_get_result_status(jreply_msg, &status) == RETURN_OK)
    {
        if (status)
        {
            CcspTraceInfo(("%s - %d Delete request for [%s] is successful \n", __FUNCTION__, __LINE__, ifname));
        }
        else
        {
            CcspTraceError(("%s - %d - Delete request for [%s] is failed \n", __FUNCTION__, __LINE__, ifname));
        }
    }
    else
    {
        CcspTraceError(("%s - %d Failed to get result status from json response, something wrong happened!!! \n", __FUNCTION__, __LINE__));
    }

    // Free json objects.
    FREE_JSON_OBJECT(jmsg);
    FREE_JSON_OBJECT(jreply_msg);

    return RETURN_OK;
}
