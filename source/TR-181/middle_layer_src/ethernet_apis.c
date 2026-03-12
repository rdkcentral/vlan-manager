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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <syscfg/syscfg.h>

#include "vlan_mgr_apis.h"
#include "ethernet_apis.h"
#include "ethernet_internal.h"
#include <sysevent/sysevent.h>
#include "plugin_main_apis.h"
#include "vlan_internal.h"
#include "vlan_dml.h"
#include "vlan_eth_hal.h"
#include "secure_wrapper.h"
#include "ccsp_psm_helper.h"
#if defined(COMCAST_VLAN_HAL_ENABLED)
#include "ccsp_hal_ethsw.h"
#include <platform_hal.h>
#endif //COMCAST_VLAN_HAL_ENABLED

/* **************************************************************************************************** */
#define DATAMODEL_PARAM_LENGTH 256
#define PARAM_SIZE_10 10
#define PARAM_SIZE_32 32
#define PARAM_SIZE_64 64

/*TODO:
 * Need to be Reviewed After Unification Finalised.
 */
//ETH Agent.
#define ETH_DBUS_PATH                     "/com/cisco/spvtg/ccsp/ethagent"
#define ETH_COMPONENT_NAME                "eRT.com.cisco.spvtg.ccsp.ethagent"
#define ETH_STATUS_PARAM_NAME             "Device.Ethernet.X_RDK_Interface.%d.WanStatus"
#define ETH_NOE_PARAM_NAME                "Device.Ethernet.X_RDK_InterfaceNumberOfEntries"
#define ETH_IF_PARAM_NAME                 "Device.Ethernet.X_RDK_Interface.%d.Name"
//DSL Agent.
#define DSL_DBUS_PATH                     "/com/cisco/spvtg/ccsp/xdslmanager"
#define DSL_COMPONENT_NAME                "eRT.com.cisco.spvtg.ccsp.xdslmanager"
#define DSL_LINE_WAN_STATUS_PARAM_NAME    "Device.DSL.Line.%d.X_RDK_WanStatus"

/*TODO:
 * Need to Reviewed Marking Table Handling After Unification Finalised.
 */
//WAN Agent
#define WAN_DBUS_PATH                     "/com/cisco/spvtg/ccsp/wanmanager"
#define WAN_COMPONENT_NAME                "eRT.com.cisco.spvtg.ccsp.wanmanager"

#if defined(WAN_MANAGER_UNIFICATION_ENABLED)
#define WAN_IF_LINK_STATUS                "Device.X_RDK_WanManager.Interface.%d.VirtualInterface.1.VlanStatus"
#define WAN_MARKING_NOE_PARAM_NAME        "Device.X_RDK_WanManager.Interface.%d.MarkingNumberOfEntries"
#define WAN_MARKING_TABLE_NAME            "Device.X_RDK_WanManager.Interface.%d.Marking."
#else
#define WAN_IF_LINK_STATUS                "Device.X_RDK_WanManager.CPEInterface.%d.Wan.LinkStatus"
#define WAN_MARKING_NOE_PARAM_NAME        "Device.X_RDK_WanManager.CPEInterface.%d.MarkingNumberOfEntries"
#define WAN_MARKING_TABLE_NAME            "Device.X_RDK_WanManager.CPEInterface.%d.Marking."
#endif /* WAN_MANAGER_UNIFICATION_ENABLED */

extern ANSC_HANDLE                        g_MessageBusHandle;
        int                               sysevent_fd = -1;
        token_t                           sysevent_token;

static ANSC_STATUS DmlEthSetParamValues(const char *pComponent, char *pBus, char *pParamName, char *pParamVal, enum dataType_e type, unsigned int bCommitFlag);
static ANSC_STATUS DmlEthGetParamNames(char *pComponent, char *pBus, char *pParamName, char a2cReturnVal[][256], int *pReturnSize);
static int EthLink_SyseventInit( void );

static ANSC_STATUS EthLink_GetUnTaggedVlanInterfaceStatus(const char *iface, ethernet_link_status_e *status);

static ANSC_STATUS EthLink_GetVlanIdAndTPId(const PDML_ETHERNET pEntry, INT *pVlanId, ULONG *pTPId);
static int EthLink_GetActiveWanInterfaces(char *Alias);
static ANSC_STATUS EthLink_DeleteMarking(PDML_ETHERNET pEntry);
static ANSC_STATUS EthLink_CreateUnTaggedInterface(PDML_ETHERNET pEntry);
#if !defined(VLAN_MANAGER_HAL_ENABLED)
static ANSC_STATUS EthLink_DeleteUnTaggedInterface(PDML_ETHERNET pEntry);
#endif /* !VLAN_MANAGER_HAL_ENABLED */
/*TODO
* Need to be Reviewed after Unification finalised.
*/
static ANSC_STATUS EthLink_CreateMarkingTable(PDML_ETHERNET pEntry, vlan_configuration_t* pVlanCfg);
static ANSC_STATUS EthLink_AddMarking(PDML_ETHERNET pEntry);
static ANSC_STATUS EthLink_TriggerVlanRefresh(PDML_ETHERNET pEntry );
#if !defined(VLAN_MANAGER_HAL_ENABLED)
static ANSC_STATUS EthLink_SetEgressQoSMap( vlan_configuration_t *pVlanCfg );
#endif

#if defined(COMCAST_VLAN_HAL_ENABLED)
static ANSC_STATUS EthLink_CreateBridgeInterface(BOOL isAutoWanMode);
static INT EthLink_Hal_BridgeConfigIntelPuma7(WAN_MODE_BRIDGECFG *pCfg);
static INT EthLink_Hal_BridgeConfigBcm(WAN_MODE_BRIDGECFG *pCfg);
static BOOL EthLink_IsWanEnabled();
static void EthLink_GetInterfaceMacAddress(macaddr_t* macAddr,char *pIfname);
static INT EthLink_BridgeNfDisable( const char* bridgeName, bridge_nf_table_t table, BOOL disable );
#endif
/* *************************************************************************************************** */

/* * EthLink_SyseventInit() */
static int EthLink_SyseventInit( void )
{
    char sysevent_ip[] = "127.0.0.1";
    char sysevent_name[] = "vlanmgr";

    sysevent_fd =  sysevent_open( sysevent_ip, SE_SERVER_WELL_KNOWN_PORT, SE_VERSION, sysevent_name, &sysevent_token );

    if ( sysevent_fd < 0 )
        return -1;

    return 0;
}

/**********************************************************************

    caller:     self

    prototype:

        BOOL
        EthLink_Init
            (
                ANSC_HANDLE                 hDml,
                PANSC_HANDLE                phContext
            );

        Description:
            This is the initialization routine for ETHERNET backend.

        Arguments:
            hDml               Opaque handle from DM adapter. Backend saves this handle for calling pValueGenFn.
             phContext       Opaque handle passed back from backend, needed by CosaDmlETHERNETXyz() routines.

        Return:
            Status of operation.

**********************************************************************/
ANSC_STATUS
EthLink_Init
    (
        ANSC_HANDLE                 hDml,
        PANSC_HANDLE                phContext
    )
{
    ANSC_STATUS returnStatus = ANSC_STATUS_SUCCESS;

    // Initialize sysevent
    if ( EthLink_SyseventInit( ) < 0 )
    {
        CcspTraceError(("%s - %d : SysEvent Initialisation failed\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    return returnStatus;
}

/**********************************************************************

    caller:     self

    prototype:

        ANSC_STATUS
        EthLink_GetStatus
            (
                ANSC_HANDLE         hThisObject,
                PDML_ETHERNET      pEntry
            );

    Description:
        The API updated current state of a ETHERNET interface
    Arguments:
        pAlias      The entry is identified through Alias.
        pEntry      The new configuration is passed through this argument, even Alias field can be changed.

    Return:
        Status of the operation

**********************************************************************/
ANSC_STATUS
EthLink_GetStatus
    (
        PDML_ETHERNET      pEntry
    )
{
    ANSC_STATUS             returnStatus  = ANSC_STATUS_SUCCESS;
    ethernet_link_status_e status;

    if (pEntry != NULL) {
        if ( ANSC_STATUS_SUCCESS != EthLink_GetUnTaggedVlanInterfaceStatus(pEntry->Name, &status)) {
            pEntry->Status = ETH_IF_ERROR;
            CcspTraceError(("%s %d - %s: Failed to get interface status for this\n", __FUNCTION__,__LINE__, pEntry->Name));
        }
        else {
            pEntry->Status = status;
        }
    }
    return returnStatus;
}

/* Set Wan Virtual Interface Vlan Status */
ANSC_STATUS EthLink_SendVirtualIfaceVlanStatus(char *path, char *vlanStatus)
{
    char acSetParamName[DATAMODEL_PARAM_LENGTH] = {0};

    if ((NULL == path) || (strlen(path) <= 0) || (NULL == vlanStatus))
    {
        CcspTraceError(("%s Path Or vlanStatus Null\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    //Set WAN Virtual Iface VlanStatus
    snprintf(acSetParamName, DATAMODEL_PARAM_LENGTH, "%s.%s", path, "VlanStatus");
    DmlEthSetParamValues(WAN_COMPONENT_NAME, WAN_DBUS_PATH, acSetParamName, vlanStatus, ccsp_string, TRUE);
    CcspTraceInfo(("%s-%d:Successfully set the Virtual Interface(%s) VLAN Status(%s) \n", __FUNCTION__, __LINE__, path, vlanStatus));
    return ANSC_STATUS_SUCCESS;
}

/**********************************************************************

    caller:     self

    prototype:

        ANSC_STATUS
        EthLink_Enable
            (
                PDML_ETHERNET      pEntry
            );

    Description:
        The API enable the designated ETHERNET interface
    Arguments:
        pEntry      The new configuration is passed through this argument, even Alias field can be changed.

    Return:
        Status of the operation

**********************************************************************/
ANSC_STATUS EthLink_Enable(PDML_ETHERNET  pEntry)
{
    ANSC_STATUS returnStatus = ANSC_STATUS_SUCCESS;
    ethernet_link_status_e status = ETH_IF_DOWN;
    int iIterator = 0;

    if (NULL == pEntry)
    {
        CcspTraceError(("%s : Failed to Enable EthLink \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    //create marking table in ethlink here. no need to delete if ethlink is disabled.
    /* TODO: Retry add making if DM gert fails. 
     * Currently we continue to create VLAN link, if Marking get fails to avoid WAN failure.
     */
    if (EthLink_AddMarking(pEntry) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("[%s-%d] Failed to Add Marking. Creating VLAN without Marking \n", __FUNCTION__, __LINE__));
    }

    if (pEntry->PriorityTagging == FALSE)
    {

        //Delete UnTagged Vlan Interface
        returnStatus = EthLink_GetUnTaggedVlanInterfaceStatus(pEntry->Name, &status);
        if (returnStatus != ANSC_STATUS_SUCCESS )
        {
            CcspTraceError(("[%s-%d] - %s: Failed to get VLAN interface status\n", __FUNCTION__, __LINE__, pEntry->Name));
            return returnStatus;
        }
#if defined(VLAN_MANAGER_HAL_ENABLED)
        if ( ( status != ETH_IF_NOTPRESENT ) && ( status != ETH_IF_ERROR ) )
        {
            returnStatus = vlan_eth_hal_deleteInterface(pEntry->Name, pEntry->InstanceNumber);
            if (ANSC_STATUS_SUCCESS != returnStatus)
            {
                CcspTraceError(("[%s-%d] Failed to delete UnTagged VLAN interface(%s)\n", __FUNCTION__, __LINE__, pEntry->Name));
            }
        }
#endif
        //Create UnTagged Vlan Interface
        returnStatus = EthLink_CreateUnTaggedInterface(pEntry);
        if (ANSC_STATUS_SUCCESS != returnStatus)
        {
            pEntry->Status = ETH_IF_ERROR;
            CcspTraceError(("[%s][%d]Failed to create UnTagged VLAN interface \n", __FUNCTION__, __LINE__));
            return returnStatus;
        }

        while(iIterator < 10)
        {
            if (ANSC_STATUS_FAILURE == EthLink_GetUnTaggedVlanInterfaceStatus(pEntry->Name, &status))
            {
                CcspTraceError(("%s-%d: Failed to Get UnTagged Vlan Interface=%s Status \n", __FUNCTION__, __LINE__, pEntry->Name));
                return ANSC_STATUS_FAILURE;
            }

            if (status == ETH_IF_UP)
            {
                EthLink_SendVirtualIfaceVlanStatus(pEntry->Path, "Up");
                break;
            }

            iIterator++;
            sleep(2);
            CcspTraceInfo(("%s-%d: Interface Status(%d), retry-count=%d \n", __FUNCTION__, __LINE__, status, iIterator));
        }
        pEntry->Status = ETH_IF_UP;
        CcspTraceInfo(("%s - %s:Successfully created UnTagged VLAN Interface(%s)\n",__FUNCTION__, ETH_MARKER_VLAN_IF_CREATE, pEntry->Name));
    }

    return ANSC_STATUS_SUCCESS;
}

/**********************************************************************

    caller:     self

    prototype:

        ANSC_STATUS
        EthLink_Disable
            (
                PDML_ETHERNET      pEntry
            );

    Description:
        The API delete the designated ETHERNET interface from the system
    Arguments:
        pEntry      The new configuration is passed through this argument, even Alias field can be changed.

    Return:
        Status of the operation

**********************************************************************/

ANSC_STATUS EthLink_Disable(PDML_ETHERNET  pEntry)
{
    ANSC_STATUS returnStatus = ANSC_STATUS_SUCCESS;
    ethernet_link_status_e status;

    if (pEntry == NULL )
    {
        CcspTraceError(("[%s-%d] Invalid parameter error! \n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_BAD_PARAMETER;
    }

    //create makring table in ethlink here. no need to delete if ethlink is disabled.
    if (EthLink_DeleteMarking(pEntry) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("[%s-%d] Failed to Delete Marking \n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    if (pEntry->PriorityTagging == FALSE)
    {
        returnStatus = EthLink_GetUnTaggedVlanInterfaceStatus(pEntry->Name, &status);
        if (returnStatus != ANSC_STATUS_SUCCESS )
        {
            CcspTraceError(("[%s-%d] - %s: Failed to get VLAN interface status\n", __FUNCTION__, __LINE__, pEntry->Name));
            return returnStatus;
        }
#if defined(VLAN_MANAGER_HAL_ENABLED)
        //Delete Untagged VLAN interface
        if ( ( status != ETH_IF_NOTPRESENT ) && ( status != ETH_IF_ERROR ) )
        {
            returnStatus = vlan_eth_hal_deleteInterface(pEntry->Name, pEntry->InstanceNumber);
            if (ANSC_STATUS_SUCCESS != returnStatus)
            {
                CcspTraceError(("[%s-%d] Failed to delete UnTagged VLAN interface(%s)\n", __FUNCTION__, __LINE__, pEntry->Name));
            }
        }
#elif !defined(COMCAST_VLAN_HAL_ENABLED)
        // Delete untagged VLAN interface using ip link commands when both HALs are disabled
        if ( ( status != ETH_IF_NOTPRESENT ) && ( status != ETH_IF_ERROR ) )
        {
            returnStatus = EthLink_DeleteUnTaggedInterface(pEntry);
            if (ANSC_STATUS_SUCCESS != returnStatus)
            {
                CcspTraceError(("[%s-%d] Failed to delete untagged VLAN interface using ip link commands\n", __FUNCTION__, __LINE__));
            }
        }
#endif
        pEntry->Status = ETH_IF_DOWN;
        //Notify Vlan Status to WAN Manager.
        EthLink_SendVirtualIfaceVlanStatus(pEntry->Path, "Down");
        CcspTraceInfo(("[%s-%d] Successfully Updated Vlan Status to WanManager \n", __FUNCTION__, __LINE__));

        CcspTraceInfo(("[%s-%d]  %s:Successfully deleted %s VLAN interface \n", __FUNCTION__, __LINE__, ETH_MARKER_VLAN_IF_DELETE, pEntry->Name)); 
    }

    return returnStatus;
}

/* * DmlEthGetParamValues() */
ANSC_STATUS DmlEthGetParamValues(
    char *pComponent,
    char *pBus,
    char *pParamName,
    char *pReturnVal)
{
    if((NULL == pComponent) || (NULL == pBus) || (NULL == pParamName) || (NULL == pReturnVal))
    {
        CcspTraceError(("%s - %d : Null valued input parameters\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    parameterValStruct_t **retVal = NULL;
    char *ParamName[1];
    int ret = 0,
        nval;

    //Assign address for get parameter name
    ParamName[0] = pParamName;

    ret = CcspBaseIf_getParameterValues(
        bus_handle,
        pComponent,
        pBus,
        ParamName,
        1,
        &nval,
        &retVal);

    //Copy the value
    if (CCSP_SUCCESS == ret)
    {
        if (NULL != retVal[0]->parameterValue)
        {
            memcpy(pReturnVal, retVal[0]->parameterValue, strlen(retVal[0]->parameterValue) + 1);
        }

        if (retVal)
        {
            free_parameterValStruct_t(bus_handle, nval, retVal);
        }
        CcspTraceInfo(("%s %d: GET : %s = %s \n", __FUNCTION__, __LINE__, pParamName, pReturnVal));
        return ANSC_STATUS_SUCCESS;
    }

    if (retVal)
    {
        free_parameterValStruct_t(bus_handle, nval, retVal);
    }

    CcspTraceError(("%s %d - Failed to get Eth Parameter values\n", __FUNCTION__, __LINE__));
    return ANSC_STATUS_FAILURE;
}

/* * DmlEthSetParamValues() */
static ANSC_STATUS DmlEthSetParamValues(
    const char *pComponent,
    char *pBus,
    char *pParamName,
    char *pParamVal,
    enum dataType_e type,
    unsigned int bCommitFlag)
{
    if((NULL == pComponent) || (NULL == pBus) || (NULL == pParamName) || (NULL == pParamVal))
    {
        CcspTraceError(("%s - %d : Null valued input parameter\n", __FUNCTION__, __LINE__));
	return ANSC_STATUS_FAILURE;
    }

    CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)g_MessageBusHandle;
    parameterValStruct_t param_val[1] = {0};
    char *faultParam = NULL;
    int ret = 0;

    param_val[0].parameterName = pParamName;
    param_val[0].parameterValue = pParamVal;
    param_val[0].type = type;

    ret = CcspBaseIf_setParameterValues(
        bus_handle,
        pComponent,
        pBus,
        0,
        0,
        param_val,
        1,
        bCommitFlag,
        &faultParam);

    if ((ret != CCSP_SUCCESS) && (faultParam != NULL))
    {
        CcspTraceError(("[%s][%d] Failed to set %s\n", __FUNCTION__, __LINE__, pParamName));
        bus_info->freefunc(faultParam);
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

/* *DmlEthGetParamNames() */
static ANSC_STATUS DmlEthGetParamNames(
    char *pComponent,
    char *pBus,
    char *pParamName,
    char a2cReturnVal[][256],
    int *pReturnSize)
{
    if((NULL == pComponent) || (NULL == pBus) || (NULL == pParamName) || (NULL == pReturnSize))
    {
        CcspTraceError(("%s - %d : Null valued input parameters\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    parameterInfoStruct_t **retInfo = NULL;
    int ret = 0,
        nval;

    ret = CcspBaseIf_getParameterNames(
        bus_handle,
        pComponent,
        pBus,
        pParamName,
        1,
        &nval,
        &retInfo);

    if (CCSP_SUCCESS == ret)
    {
        int iLoopCount;

        *pReturnSize = nval;

        for (iLoopCount = 0; iLoopCount < nval; iLoopCount++)
        {
            if (NULL != retInfo[iLoopCount]->parameterName)
            {
                snprintf(a2cReturnVal[iLoopCount], strlen(retInfo[iLoopCount]->parameterName) + 1, "%s", retInfo[iLoopCount]->parameterName);
            }
        }

        if (retInfo)
        {
            free_parameterInfoStruct_t(bus_handle, nval, retInfo);
        }

        return ANSC_STATUS_SUCCESS;
    }

    if (retInfo)
    {
        free_parameterInfoStruct_t(bus_handle, nval, retInfo);
    }

    CcspTraceError(("%s %d - Failed to get Eth Parameter Names\n", __FUNCTION__, __LINE__));
    return ANSC_STATUS_FAILURE;
}

/*TODO
 * Below Lists of APIs Need to be Reviewed after Unification if finalised:
 * EthLink_GetActiveWanInterfaces
 * EthLink_DeleteMarking
 * EthLink_AddMarking
 * EthLink_GetMarking
 */
static int EthLink_GetActiveWanInterfaces(char *Alias)
{
    char paramName[PARAM_SIZE_64] = {0};
    char *strValue                = NULL;
    char *endptr                  = NULL;
    int wanIfCount                = 0;
    int activeIface               = -1;
    int retPsmGet                 = CCSP_SUCCESS;

    if (Alias[0] == '\0')
    {
	CcspTraceError(("%s-%d: Alias is Null \n",__FUNCTION__, __LINE__));
        return activeIface;
    }

    strncpy(paramName, "dmsb.wanmanager.wanifcount", sizeof(paramName) - 1);
    retPsmGet = PSM_VALUE_GET_VALUE(paramName, strValue);
    if((retPsmGet == CCSP_SUCCESS) && (strValue != NULL))
    {
        wanIfCount = strtol(strValue, &endptr, 10);
        Ansc_FreeMemory_Callback(strValue);
        strValue = NULL;
    }

    for(int i = 1; i <= wanIfCount; i++)
    {
       memset(paramName, 0, sizeof(paramName));
       sprintf(paramName, "dmsb.wanmanager.if.%d.Name", i);
       retPsmGet = PSM_VALUE_GET_VALUE(paramName, strValue);
       if((retPsmGet == CCSP_SUCCESS) && (strValue != NULL))
       {
           if(0 == strcmp(strValue, Alias))
           {
              activeIface = i;

              Ansc_FreeMemory_Callback(strValue);
              strValue = NULL;

              break;
           }
           Ansc_FreeMemory_Callback(strValue);
           strValue = NULL;
       }
    }

    if(-1 != activeIface)
    {
        return(activeIface);
    }
    
    return -1;
}

static ANSC_STATUS EthLink_DeleteMarking(PDML_ETHERNET pEntry)
{
    if (pEntry == NULL )
    {
        CcspTraceError(("%s Invalid Memory\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    if (pEntry->NumberofMarkingEntries > 0)
    {
        if(pEntry->pstDataModelMarking != NULL)
        {
            free(pEntry->pstDataModelMarking);
            pEntry->pstDataModelMarking = NULL;
        }
	pEntry->NumberofMarkingEntries = 0;
    }

    return ANSC_STATUS_SUCCESS;
}

static ANSC_STATUS EthLink_AddMarking(PDML_ETHERNET pEntry)
{
    char acGetParamName[256] = {0};
    char acTmpReturnValue[256] = {0};
    char a2cTmpTableParams[16][256] = {0};
    INT iLoopCount = 0;
    INT iTotalNoofEntries = 0;
    INT iWANInstance   = -1;
    vlan_configuration_t VlanCfg = {0};

    if (pEntry == NULL )
    {
        CcspTraceError(("%s Invalid Memory\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    iWANInstance = EthLink_GetActiveWanInterfaces(pEntry->Alias);
    if (-1 == iWANInstance)
    {
        CcspTraceError(("%s %d Eth instance not present\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }
    CcspTraceInfo(("%s %d Wan Interface Instance:%d\n", __FUNCTION__, __LINE__, iWANInstance));

    memset(acGetParamName, 0, sizeof(acGetParamName));
    snprintf(acGetParamName, sizeof(acGetParamName), WAN_MARKING_TABLE_NAME, iWANInstance);

    if ( ANSC_STATUS_FAILURE == DmlEthGetParamNames(WAN_COMPONENT_NAME, WAN_DBUS_PATH, acGetParamName, a2cTmpTableParams, &iTotalNoofEntries))
    {
        CcspTraceError(("[%s][%d] Failed to get param value\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    // Intialise VlanCfg object.
    memset(&VlanCfg, 0, sizeof(VlanCfg));

    if( iTotalNoofEntries > 0 )
    {
        CcspTraceInfo(("%s %d: iTotalNoofEntries = %d\n", __FUNCTION__, __LINE__, iTotalNoofEntries));

        VlanCfg.skb_config = (vlan_skb_config_t*)malloc( iTotalNoofEntries * sizeof(vlan_skb_config_t) );
        VlanCfg.skbMarkingNumOfEntries = iTotalNoofEntries;

        if( NULL == VlanCfg.skb_config )
        {
            CcspTraceError(("%s - %d : Invalid SKB priority\n", __FUNCTION__, __LINE__));
            return ANSC_STATUS_FAILURE;
        }

        for (iLoopCount = 0; iLoopCount < iTotalNoofEntries; iLoopCount++)
        {
            char acTmpQueryParam[256];

            //Alias
            memset(acTmpQueryParam, 0, sizeof(acTmpQueryParam));
            snprintf(acTmpQueryParam, sizeof(acTmpQueryParam), "%sAlias", a2cTmpTableParams[iLoopCount]);
            memset(acTmpReturnValue, 0, sizeof(acTmpReturnValue));
            DmlEthGetParamValues(WAN_COMPONENT_NAME, WAN_DBUS_PATH, acTmpQueryParam, acTmpReturnValue);
            snprintf(VlanCfg.skb_config[iLoopCount].alias, sizeof(VlanCfg.skb_config[iLoopCount].alias), "%s", acTmpReturnValue);

            //SKBPort
            memset(acTmpQueryParam, 0, sizeof(acTmpQueryParam));
            snprintf(acTmpQueryParam, sizeof(acTmpQueryParam), "%sSKBPort", a2cTmpTableParams[iLoopCount]);
            memset(acTmpReturnValue, 0, sizeof(acTmpReturnValue));
            DmlEthGetParamValues(WAN_COMPONENT_NAME, WAN_DBUS_PATH, acTmpQueryParam, acTmpReturnValue);
            VlanCfg.skb_config[iLoopCount].skbPort = atoi(acTmpReturnValue);

            //SKBMark
            memset(acTmpQueryParam, 0, sizeof(acTmpQueryParam));
            snprintf(acTmpQueryParam, sizeof(acTmpQueryParam), "%sSKBMark", a2cTmpTableParams[iLoopCount]);
            memset(acTmpReturnValue, 0, sizeof(acTmpReturnValue));
            DmlEthGetParamValues(WAN_COMPONENT_NAME, WAN_DBUS_PATH, acTmpQueryParam, acTmpReturnValue);
            VlanCfg.skb_config[iLoopCount].skbMark = atoi(acTmpReturnValue);

            //EthernetPriorityMark
            memset(acTmpQueryParam, 0, sizeof(acTmpQueryParam));
            snprintf(acTmpQueryParam, sizeof(acTmpQueryParam), "%sEthernetPriorityMark", a2cTmpTableParams[iLoopCount]);
            memset(acTmpReturnValue, 0, sizeof(acTmpReturnValue));
            DmlEthGetParamValues(WAN_COMPONENT_NAME, WAN_DBUS_PATH, acTmpQueryParam, acTmpReturnValue);
            VlanCfg.skb_config[iLoopCount].skbEthPriorityMark = atoi(acTmpReturnValue);

            CcspTraceInfo(("WAN Marking - Ins[%d] Alias[%s] SKBPort[%u] SKBMark[%u] EthernetPriorityMark[%d]\n",
                        iLoopCount + 1,
                        VlanCfg.skb_config[iLoopCount].alias,
                        VlanCfg.skb_config[iLoopCount].skbPort,
                        VlanCfg.skb_config[iLoopCount].skbMark,
                        VlanCfg.skb_config[iLoopCount].skbEthPriorityMark ));
        }
        //Create and initialise Marking data models
        EthLink_CreateMarkingTable(pEntry, &VlanCfg);
    }

    //Free VlanCfg skb_config memory
    if (VlanCfg.skb_config != NULL)
    {
        free(VlanCfg.skb_config);
        VlanCfg.skb_config = NULL;
    }

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS EthLink_GetMarking(char *ifname, vlan_configuration_t *pVlanCfg)
{
    INT iLoopCount = 0;
    BOOL Found = FALSE;
    ANSC_STATUS returnStatus = ANSC_STATUS_FAILURE;

    if ((ifname == NULL) || (pVlanCfg == NULL))
    {
        CcspTraceError(("%s Invalid Memory\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    PDATAMODEL_ETHERNET    pMyObject    = (PDATAMODEL_ETHERNET)g_pBEManager->hEth;
    PDML_ETHERNET          p_EthLink    = NULL;

    if (pMyObject->ulEthlinkInstanceNumber > 0)
    {
        for(iLoopCount = 0; iLoopCount < pMyObject->ulEthlinkInstanceNumber; iLoopCount++)
        {
            p_EthLink = (PDML_ETHERNET)&(pMyObject->EthLink[iLoopCount]);
            if (p_EthLink != NULL)
            {
                if (strncmp(p_EthLink->Alias, ifname, strlen(ifname)) == 0)
                {
                    Found = TRUE;
                    break;
                }
            }
        }
        if (Found && (p_EthLink != NULL))
        {
            //Vlan Marking Info
            CcspTraceInfo(("%s-%d: NumberofMarkingEntries=%d \n", __FUNCTION__, __LINE__, p_EthLink->NumberofMarkingEntries));
            if (p_EthLink->NumberofMarkingEntries > 0)
            {
                //allocate memory to vlan_skb_config_t, free it once used.
                pVlanCfg->skbMarkingNumOfEntries = p_EthLink->NumberofMarkingEntries;
                pVlanCfg->skb_config = (vlan_skb_config_t*)malloc( p_EthLink->NumberofMarkingEntries * sizeof(vlan_skb_config_t) );

                if( NULL == pVlanCfg->skb_config )
                {
                    CcspTraceError(("%s - %d : Invalid SKB priority\n", __FUNCTION__, __LINE__));
                    return ANSC_STATUS_FAILURE;
                }

                for(int i = 0; i < p_EthLink->NumberofMarkingEntries; i++)
                {
                    PCOSA_DML_MARKING pDataModelMarking = (PCOSA_DML_MARKING)&(p_EthLink->pstDataModelMarking[i]);
                    if ((pDataModelMarking != NULL) && (pVlanCfg->skb_config != NULL))
                    {
                        strncpy(pVlanCfg->skb_config[i].alias, pDataModelMarking->Alias, sizeof(pVlanCfg->skb_config[i].alias) - 1);
                        pVlanCfg->skb_config[i].skbPort = pDataModelMarking->SKBPort;
                        pVlanCfg->skb_config[i].skbMark = pDataModelMarking->SKBMark;
                        pVlanCfg->skb_config[i].skbEthPriorityMark = pDataModelMarking->EthernetPriorityMark;
                        CcspTraceInfo(("%s-%d: Ins[%d] Alias[%s] SKBPort[%u] SKBMark[%u] EthernetPriorityMark[%d]\n", __FUNCTION__,
                                    __LINE__, (i + 1), pVlanCfg->skb_config[i].alias, pVlanCfg->skb_config[i].skbPort,
                                    pVlanCfg->skb_config[i].skbMark, pVlanCfg->skb_config[i].skbEthPriorityMark ));
                    }
                    else
                    {
                        CcspTraceError(("%s-%d: pDataModelMarking Or pVlanCfg->skb_config are Null \n", __FUNCTION__, __LINE__));
                        free(pVlanCfg->skb_config);
                        pVlanCfg->skb_config = NULL;
                        return ANSC_STATUS_FAILURE;
                    }
                }
            }
            returnStatus = ANSC_STATUS_SUCCESS;
        }
    }

    return returnStatus;
}

static ANSC_STATUS EthLink_CreateUnTaggedInterface(PDML_ETHERNET pEntry)
{
    ANSC_STATUS returnStatus = ANSC_STATUS_SUCCESS;
    vlan_configuration_t VlanCfg = {0};

    if (pEntry == NULL)
    {
        CcspTraceError(("%s-%d: Failed to Create Tagged Vlan Interface\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    memset (&VlanCfg, 0, sizeof(vlan_configuration_t));

    strncpy(VlanCfg.BaseInterface, pEntry->BaseInterface, sizeof(VlanCfg.BaseInterface) - 1);
    strncpy(VlanCfg.L3Interface, pEntry->Name, sizeof(VlanCfg.L3Interface) - 1);
    strncpy(VlanCfg.L2Interface, pEntry->BaseInterface, sizeof(VlanCfg.L2Interface) - 1);
    VlanCfg.VLANId = DEFAULT_VLAN_ID;
    VlanCfg.TPId   = 0;

    if (EthLink_GetMarking(pEntry->Alias, &VlanCfg) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("%s Failed to Get Marking, so Can't Create Vlan Interface(%s) \n", __FUNCTION__, pEntry->Alias));
        return ANSC_STATUS_FAILURE;
    }
#if defined(VLAN_MANAGER_HAL_ENABLED)
    vlan_eth_hal_createInterface(&VlanCfg);
#elif defined(COMCAST_VLAN_HAL_ENABLED)
/*TODO
 * Need to Add Code for HAL Independent Untagged Vlan/Bridge Interface creation.
 */
    EthLink_CreateBridgeInterface(TRUE);
#else
    // Create untagged interface using MACVLAN for truly untagged traffic
    CcspTraceInfo(("%s-%d: Creating MACVLAN untagged interface %s on base interface %s with MAC offset %ld\n", 
                   __FUNCTION__, __LINE__, pEntry->Name, pEntry->BaseInterface, pEntry->MACAddrOffSet));
    
    // Check if interface already exists (as MACVLAN or bridge) and delete it
    CcspTraceInfo(("%s-%d: Checking if interface %s already exists\n", __FUNCTION__, __LINE__, pEntry->Name));
    v_secure_system("ip link show %s > /dev/null 2>&1 && (ip link set %s down; ip link delete %s)", 
                    pEntry->Name, pEntry->Name, pEntry->Name);
    
    // Get MAC address with offset applied
    if (ANSC_STATUS_SUCCESS != EthLink_GetMacAddr(pEntry))
    {
        CcspTraceError(("%s-%d: Failed to get MAC address, creating MACVLAN without custom MAC\n", __FUNCTION__, __LINE__));
        // Create MACVLAN without setting custom MAC - kernel will assign one
        v_secure_system("ip link add link %s name %s type macvlan mode bridge", 
                        pEntry->BaseInterface, pEntry->Name);
    }
    else
    {
        CcspTraceInfo(("%s-%d: Using MAC address: %s (offset: %ld)\n", 
                       __FUNCTION__, __LINE__, pEntry->MACAddress, pEntry->MACAddrOffSet));
        
        // Create MACVLAN interface with custom MAC
        v_secure_system("ip link add link %s name %s address %s type macvlan mode bridge", 
                        pEntry->BaseInterface, pEntry->Name, pEntry->MACAddress);
    }
    
    // Set MTU to default 1500
    CcspTraceInfo(("%s-%d: Setting MTU to 1500 for MACVLAN interface %s\n", 
                   __FUNCTION__, __LINE__, pEntry->Name));
    v_secure_system("ip link set %s mtu 1500", pEntry->Name);
    
    v_secure_system("ip link set %s up", pEntry->Name);
    
    CcspTraceInfo(("%s-%d: Successfully created MACVLAN untagged interface %s\n", 
                   __FUNCTION__, __LINE__, pEntry->Name));
#endif
    //Free VlanCfg skb_config memory
    if (VlanCfg.skb_config != NULL)
    {
        free(VlanCfg.skb_config);
        VlanCfg.skb_config = NULL;
    }

    return returnStatus;
}

#if !defined(VLAN_MANAGER_HAL_ENABLED)
static ANSC_STATUS EthLink_DeleteUnTaggedInterface(PDML_ETHERNET pEntry)
{
    ANSC_STATUS returnStatus = ANSC_STATUS_SUCCESS;

    if (pEntry == NULL)
    {
        CcspTraceError(("%s-%d: Invalid parameter error\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    CcspTraceInfo(("%s-%d: Deleting untagged VLAN interface %s\n", 
                   __FUNCTION__, __LINE__, pEntry->Name));
    
    v_secure_system("ip link set %s down", pEntry->Name);
    v_secure_system("ip link delete %s", pEntry->Name);
    
    CcspTraceInfo(("%s-%d: Successfully deleted untagged VLAN interface %s\n", 
                   __FUNCTION__, __LINE__, pEntry->Name));

    return returnStatus;
}
#endif /* !VLAN_MANAGER_HAL_ENABLED */

/* Start Vlan Refresh Handle Thread */
void* EthLink_RefreshHandleThread(void *Arg)
{
    PDML_ETHERNET pEntry = (PDML_ETHERNET)Arg;

    if ( NULL == pEntry )
    {
        CcspTraceError(("%s-%d: Failed to Start Refresh Handle Thread, Arg pEntry Null \n", __FUNCTION__, __LINE__));
        pthread_exit(NULL);
    }

    pthread_detach(pthread_self());

    /* TODO: Retry add making if DM gert fails. 
     * Currently we continue to create VLAN link, if Marking get fails to avoid WAN failure.
     */
    if (EthLink_AddMarking(pEntry) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("[%s-%d] Failed to Update Refreshed Marking. Creating VLAN without Marking. \n", __FUNCTION__, __LINE__));
    }
    if (EthLink_TriggerVlanRefresh(pEntry) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("[%s-%d] Failed to Trigger Vlan Refresh \n", __FUNCTION__, __LINE__));
    }

    pthread_exit(NULL);
}

/* Trigger VLAN Refresh */
static ANSC_STATUS EthLink_TriggerVlanRefresh(PDML_ETHERNET pEntry )
{
    ANSC_STATUS returnStatus = ANSC_STATUS_SUCCESS;
    ethernet_link_status_e status = ETH_IF_DOWN;
    vlan_configuration_t VlanCfg = {0};
    INT iIterator = 0;

    if (pEntry == NULL)
    {
        CcspTraceError(("%s-%d: Failed to Trigger Vlan Refresh for Interface=%s \n", __FUNCTION__, __LINE__, pEntry->Alias));
        return ANSC_STATUS_FAILURE;
    }

    memset (&VlanCfg, 0, sizeof(vlan_configuration_t));

    strncpy(VlanCfg.BaseInterface, pEntry->BaseInterface, sizeof(VlanCfg.BaseInterface) - 1);
    strncpy(VlanCfg.L3Interface, pEntry->Name, sizeof(VlanCfg.L3Interface) - 1);
    strncpy(VlanCfg.L2Interface, pEntry->BaseInterface, sizeof(VlanCfg.L2Interface) - 1);
    VlanCfg.VLANId = 0;
    VlanCfg.TPId   = 0;
    if (EthLink_GetVlanIdAndTPId(pEntry, &VlanCfg.VLANId, (PULONG)&VlanCfg.TPId) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("%s Failed to Get VLANId and TPId for Interface(%s) \n", __FUNCTION__, pEntry->Alias));
    }

    /* TODO: Retry add making if DM gert fails. 
     * Currently we continue to create VLAN link, if Marking get fails to avoid WAN failure.
     */
    if (EthLink_GetMarking(pEntry->Alias, &VlanCfg) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("%s Failed to Get Marking, Creating Vlan Interface(%s) without marking \n", __FUNCTION__, pEntry->Alias));
    }
#if defined(VLAN_MANAGER_HAL_ENABLED)
    vlan_eth_hal_setMarkings(&VlanCfg);
#else
    if ( EthLink_SetEgressQoSMap(&VlanCfg) != ANSC_STATUS_FAILURE)
    {
        CcspTraceInfo(("%s - Successfully Set QoS Marking \n",__FUNCTION__));
    }
#endif
    //Free VlanCfg skb_config memory
    if (VlanCfg.skb_config != NULL)
    {
        free(VlanCfg.skb_config);
        VlanCfg.skb_config = NULL;
    }

    /*TODO
     * Need to be Reviewed the below code after Unification is finalised..
     */
    //Get status of VLAN link
    while(iIterator < 10)
    {
        char interface_name[IF_NAMESIZE] = {0};

        snprintf(interface_name, sizeof(interface_name), "%s", pEntry->Name);

        if (ANSC_STATUS_FAILURE == EthLink_GetUnTaggedVlanInterfaceStatus(interface_name, &status))
        {
            CcspTraceError(("%s-%d: Failed to Get Vlan Interface=%s Status \n", __FUNCTION__, __LINE__, pEntry->Name));
            return ANSC_STATUS_FAILURE;
        }

        if (status == ETH_IF_UP)
        {
            //Notify Vlan Status to WAN Manager.
            EthLink_SendVirtualIfaceVlanStatus(pEntry->Path, "Up");
            CcspTraceInfo(("[%s-%d] Successfully Updated Vlan Status to WanManager \n", __FUNCTION__, __LINE__));
            break;
        }

        iIterator++;
        sleep(2);
        CcspTraceInfo(("%s-%d: Interface Status(%d), retry-count=%d \n", __FUNCTION__, __LINE__, status, iIterator));
    }

    if(!strncmp(pEntry->Alias, "veip", 4))
    {
        v_secure_system("/etc/gpon_vlan_init.sh");
    }

    CcspTraceInfo(("%s - %s:Successfully Triggered VLAN Refresh for Interface(%s)\n", __FUNCTION__, ETH_MARKER_VLAN_REFRESH, pEntry->Alias));

    return returnStatus;
}

/*TODO
 *Need to be Reviewed and Write one Generic API for Marking Table Creation.
 */

static ANSC_STATUS EthLink_CreateMarkingTable( PDML_ETHERNET pEntry, vlan_configuration_t* pVlanCfg)
{
    if (NULL == pVlanCfg)
    {
        CcspTraceError(("%s Invalid Memory\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    PDML_ETHERNET          p_EthLink    = NULL;
    int                    iLoopCount   = 0;

    p_EthLink = (PDML_ETHERNET) pEntry;
    if(p_EthLink == NULL)
    {
        CcspTraceError(("%s : Failed, EthLink Null\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    if(p_EthLink->pstDataModelMarking != NULL)
    {
        free(p_EthLink->pstDataModelMarking);
        p_EthLink->pstDataModelMarking = NULL;
    }

    p_EthLink->NumberofMarkingEntries = pVlanCfg->skbMarkingNumOfEntries;
    p_EthLink->pstDataModelMarking = (PCOSA_DML_MARKING) malloc(sizeof(COSA_DML_MARKING)*(p_EthLink->NumberofMarkingEntries));
    if(p_EthLink->pstDataModelMarking == NULL)
    {
        CcspTraceError(("%s Failed to allocate Memory\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    memset(p_EthLink->pstDataModelMarking, 0, (sizeof(COSA_DML_MARKING)*(p_EthLink->NumberofMarkingEntries)));
    for(iLoopCount = 0; iLoopCount < p_EthLink->NumberofMarkingEntries; iLoopCount++)
    {
        PCOSA_DML_MARKING pDataModelMarking = (PCOSA_DML_MARKING)&(p_EthLink->pstDataModelMarking[iLoopCount]);
        if (pDataModelMarking != NULL)
        {
            strncpy( pDataModelMarking->Alias, pVlanCfg->skb_config[iLoopCount].alias, sizeof(pDataModelMarking->Alias) - 1);
            pDataModelMarking->SKBPort = pVlanCfg->skb_config[iLoopCount].skbPort;
            pDataModelMarking->SKBMark = pVlanCfg->skb_config[iLoopCount].skbMark;
            pDataModelMarking->EthernetPriorityMark = pVlanCfg->skb_config[iLoopCount].skbEthPriorityMark;
            CcspTraceInfo(("%s %d: %d) Alias = %s SKBPort = %d SKBMark = %d EthernetPriorityMark = %d\n", __FUNCTION__, __LINE__, iLoopCount, pDataModelMarking->Alias, pDataModelMarking->SKBPort, pDataModelMarking->SKBMark, pDataModelMarking->EthernetPriorityMark));
        }
    }

    CcspTraceInfo(("%s : Successfully Created EthLinkTable\n", __FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

#if !defined(VLAN_MANAGER_HAL_ENABLED)
static ANSC_STATUS EthLink_SetEgressQoSMap( vlan_configuration_t *pVlanCfg )
{
    INT SKBMark = 0;
    INT EthPriority = 0;

    if ((pVlanCfg == NULL) || (pVlanCfg->skb_config == NULL))
    {
        CcspTraceError(("%s-%d: Failed to Set Egress QoS\n",__FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    CcspTraceInfo(("%s-%d: skbMarkingNumOfEntries=%d \n",__FUNCTION__, __LINE__, pVlanCfg->skbMarkingNumOfEntries));
    if (pVlanCfg->skbMarkingNumOfEntries > 0)
    {
        for(int i = 0; i < pVlanCfg->skbMarkingNumOfEntries; i++)
        {
            SKBMark = pVlanCfg->skb_config[i].skbMark;
            EthPriority = pVlanCfg->skb_config[i].skbEthPriorityMark;
            v_secure_system("ip link set %s.%d type vlan egress-qos-map %d:%d", pVlanCfg->L3Interface, pVlanCfg->VLANId, SKBMark, EthPriority);
        }
    }

    return ANSC_STATUS_SUCCESS;
}
#endif //VLAN_MANAGER_HAL_ENABLED

static ANSC_STATUS EthLink_GetUnTaggedVlanInterfaceStatus(const char *iface, ethernet_link_status_e *status)
{
    int sfd;
    int flag = FALSE;
    struct ifreq intf;

    if(iface == NULL)
    {
        *status = ETH_IF_NOTPRESENT;
        CcspTraceError(("%s - %d : Invalid SKB priority\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    if ((sfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        *status = ETH_IF_ERROR;
        CcspTraceError(("%s - %d : Invalid SKB priority\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    memset (&intf, 0, sizeof(struct ifreq));
    strncpy(intf.ifr_name, iface, sizeof(intf.ifr_name) - 1);

    if (ioctl(sfd, SIOCGIFFLAGS, &intf) == -1) {
        *status = ETH_IF_ERROR;
    } else {
        flag = (intf.ifr_flags & IFF_RUNNING) ? TRUE : FALSE;
    }

    if(flag == TRUE)
        *status = ETH_IF_UP;
    else
        *status = ETH_IF_DOWN;

    close(sfd);

    return ANSC_STATUS_SUCCESS;
}

static ANSC_STATUS EthLink_GetVlanIdAndTPId(const PDML_ETHERNET pEntry, INT *pVlanId, ULONG *pTPId)
{
    INT VLANInstance = -1;
    ULONG VlanCount = 0;
    ANSC_HANDLE pNewEntry = NULL;
    CHAR BaseInterface[64] = {0};

    if (pVlanId == NULL || pEntry == NULL)
    {
        CcspTraceError(("[%s-%d] Invalid argument \n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_BAD_PARAMETER;
    }

    VlanCount = Vlan_GetEntryCount(NULL);
    for (int i = 0; i < VlanCount; i++)
    {
        pNewEntry = Vlan_GetEntry(NULL, i, (PULONG)&VLANInstance);
        if (pNewEntry != NULL)
        {
            memset(BaseInterface, 0, sizeof(BaseInterface));
            if (Vlan_GetParamStringValue(pNewEntry, "X_RDK_BaseInterface", BaseInterface, (PULONG)sizeof(BaseInterface)) == 0)
            {
                if( 0 == strncmp(pEntry->BaseInterface, BaseInterface, 3) )
                    break;
            }
        }
        pNewEntry = NULL;
    }

    //Get VLAN Term.
    CcspTraceInfo(("%s VLANInstance=%d \n", __FUNCTION__, VLANInstance));
    if ((-1 == VLANInstance) || (pNewEntry == NULL))
    {
        CcspTraceError(("%s Failed to get VLAN Instance \n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    //Get VLANID
    if (Vlan_GetParamIntValue(pNewEntry, "VLANID", pVlanId) != TRUE)
    {
        CcspTraceError(("%s - Failed to set VLANID data model\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    //Get TPID
    if (Vlan_GetParamUlongValue(pNewEntry, "TPID", pTPId) != TRUE)
    {
        CcspTraceError(("%s - Failed to set TPID data model\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS EthLink_GetMacAddr( PDML_ETHERNET pEntry )
{
    unsigned long long int number, new_mac;
    char acTmpReturnValue[256] = {0};
    char hex[32];
    char macStr[32];
    int i, j = 0;

    if (pEntry->InstanceNumber <= 0)
    {
        CcspTraceError(("%s-%d: Failed to get Mac Address, EthLinkInstance=%ld ", __FUNCTION__, __LINE__, pEntry->InstanceNumber));
        return ANSC_STATUS_FAILURE;
    }

    if(ANSC_STATUS_FAILURE == DmlEthGetParamValues(RDKB_PAM_COMPONENT_NAME, RDKB_PAM_DBUS_PATH, PAM_BASE_MAC_ADDRESS, acTmpReturnValue))
    {
        CcspTraceError(("[%s][%d]Failed to get param value\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    for(i = 0; acTmpReturnValue[i] != '\0'; i++)
    {
        if(acTmpReturnValue[i] != ':')
        {
            acTmpReturnValue[j++] = acTmpReturnValue[i];
        }
    }

    acTmpReturnValue[j] = '\0';
    sscanf(acTmpReturnValue, "%64llx", &number);

    new_mac = number + pEntry->MACAddrOffSet;
    snprintf(hex, sizeof(hex), "%08llx", new_mac);

    snprintf(macStr, sizeof(macStr), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
            hex[0], hex[1], hex[2], hex[3], hex[4], hex[5], hex[6], hex[7], hex[8], hex[9], hex[10], hex[11]);

    strncpy(pEntry->MACAddress, macStr, sizeof(pEntry->MACAddress) - 1);

    return ANSC_STATUS_SUCCESS;
}

#if defined(COMCAST_VLAN_HAL_ENABLED)
static ANSC_STATUS EthLink_CreateBridgeInterface(BOOL isAutoWanMode)
{
    char wanPhyName[64] = {0};
    char buf[64] = {0};
    char ethwan_ifname[64] = {0};
    BOOL ovsEnabled = FALSE;
    BOOL ethwanEnabled = FALSE;
    BOOL meshEbEnabled = FALSE;
    INT lastKnownWanMode = -1;
    BOOL configureBridge = FALSE;
    INT bridgemode = 0;
    WanBridgeCfgHandler pCfgHandler = NULL;
    WAN_MODE_BRIDGECFG wanModeCfg = {0};

    CcspTraceInfo(("Func %s Entered\n",__FUNCTION__));
#if defined(INTEL_PUMA7)
    pCfgHandler = EthLink_Hal_BridgeConfigIntelPuma7;
#elif defined (_COSA_BCM_ARM_)
    pCfgHandler = EthLink_Hal_BridgeConfigBcm;
#endif

    ethwanEnabled = EthLink_IsWanEnabled();

#if defined (_BRIDGE_UTILS_BIN_)
    if( 0 == syscfg_get( NULL, "mesh_ovs_enable", buf, sizeof( buf ) ) )
    {
          if ( strcmp (buf,"true") == 0 )
            ovsEnabled = TRUE;
          else
            ovsEnabled = FALSE;

    }
    else
    {
          CcspTraceError(("syscfg_get failed to retrieve ovs_enable\n"));

    }
    if( (0 == access( ONEWIFI_ENABLED , F_OK )) || (0 == access( OPENVSWITCH_LOADED, F_OK ))
                                                || (access(WFO_ENABLED, F_OK) == 0 ) )
    {
        CcspTraceInfo(("%s Setting ovsEnabled to TRUE [OneWifi/WFO]\n",__FUNCTION__));
        ovsEnabled = TRUE;
    }
#endif

    memset(buf,0,sizeof(buf));
    if (syscfg_get(NULL, "bridge_mode", buf, sizeof(buf)) == 0)
    {
        bridgemode = atoi(buf);
    }

    memset(buf,0,sizeof(buf));
    if (syscfg_get(NULL, "eb_enable", buf, sizeof(buf)) == 0)
    {
        if (strcmp(buf,"true") == 0)
        {
            meshEbEnabled = TRUE;
        }
        else
        {
            meshEbEnabled = FALSE;
        }
    }


    memset(buf,0,sizeof(buf));
    if (!syscfg_get(NULL, "wan_physical_ifname", buf, sizeof(buf)))
    {
        strcpy(wanPhyName, buf);
        printf("wanPhyName = %s\n", wanPhyName);
    }
    else
    {
        strcpy(wanPhyName, "erouter0");

    }

    // Do wan interface bridge creation/deletion only if last detected wan and current detected wan interface are different.
    // Hence deciding here whether wan bridge creation/deletion (configureBridge) is
    // needed or not based on "lastKnownWanMode and ethwanEnabled" value.
    memset(buf,0,sizeof(buf));
    if (syscfg_get(NULL, "last_wan_mode", buf, sizeof(buf)) == 0)
    {
        lastKnownWanMode = atoi(buf);
    }

    // last  known mode will be updated by wan manager after
    // this finalize api operation done. Till that lastknownmode will be previous detected wan.
    switch (lastKnownWanMode)
    {
        case WAN_MODE_DOCSIS:
            {
                if (ethwanEnabled == TRUE)
                {
                    configureBridge = TRUE;
                }
            }
            break;
        case WAN_MODE_ETH:
            {
                if (ethwanEnabled == FALSE)
                {
                    configureBridge = TRUE;
                }
            }
            break;
        default:
            {
                // if last known mode is unknown, then default last wan mode is primary wan.
                // Hence if ethwan is enabled when last known mode is primary, then configure bridge.
                // Note: Configure bridge will need to do when prev wan mode and new wan mode is different.
                if (ethwanEnabled == TRUE)
                {
                    configureBridge = TRUE;
                }
            }
            break;
    }

    CcspTraceInfo((" %s isAutoWanMode: %d lastknownmode: %d ethwanEnabled: %d ConfigureBridge: %d bridgemode: %d\n",
                __FUNCTION__,
                isAutoWanMode,
                lastKnownWanMode,
                ethwanEnabled,
                configureBridge,
                bridgemode));

        //Get the ethwan interface name from HAL
    memset( ethwan_ifname , 0, sizeof( ethwan_ifname ) );
    if ((0 != GWP_GetEthWanInterfaceName((unsigned char*) ethwan_ifname, sizeof(ethwan_ifname)))
            || (0 == strnlen(ethwan_ifname,sizeof(ethwan_ifname)))
            || (0 == strncmp(ethwan_ifname,"disable",sizeof(ethwan_ifname))))

    {
        //Fallback case needs to set it default
        memset( ethwan_ifname , 0, sizeof( ethwan_ifname ) );
        sprintf( ethwan_ifname , "%s", ETHWAN_DEF_INTF_NAME );
    }
    wanModeCfg.bridgemode = bridgemode;
    wanModeCfg.ovsEnabled = ovsEnabled;
    wanModeCfg.ethWanEnabled = ethwanEnabled;
    wanModeCfg.meshEbEnabled = meshEbEnabled;
    wanModeCfg.configureBridge = configureBridge;
    snprintf(wanModeCfg.wanPhyName,sizeof(wanModeCfg.wanPhyName),"%s",wanPhyName);
    snprintf(wanModeCfg.ethwan_ifname,sizeof(wanModeCfg.ethwan_ifname),"%s",ethwan_ifname);

    if (pCfgHandler)
    {
        pCfgHandler(&wanModeCfg);
    }


   if (TRUE == configureBridge)
   {
       CcspTraceInfo(("Wanmode is Changing restarting Mta agent"));
       v_secure_system ("killall CcspMtaAgentSsp");

   }

    if (TRUE == isAutoWanMode)
    {
        v_secure_system("sysevent set phylink_wan_state up");
        if ( 0 != access( "/tmp/autowan_iface_finalized" , F_OK ) )
        {
            v_secure_system("touch /tmp/autowan_iface_finalized");
        }
	//Update Wan.Name here.
        //UpdateInformMsgToWanMgr();

    }
    return ANSC_STATUS_SUCCESS;
}

static INT EthLink_Hal_BridgeConfigIntelPuma7(WAN_MODE_BRIDGECFG *pCfg)
{
    if (!pCfg)
    {
        CcspTraceError(("%s - %d : Invalid input argument \n", __FUNCTION__, __LINE__));
        return -1;
    }

    if (pCfg->ethWanEnabled == TRUE)
    {
        if (pCfg->ovsEnabled == TRUE)
        {
            v_secure_system("/usr/bin/bridgeUtils del-port brlan0 %s",pCfg->ethwan_ifname);
        }
        else
        {
            v_secure_system("brctl delif brlan0 %s",pCfg->ethwan_ifname);

        }


        if (TRUE == pCfg->configureBridge)
        {
            macaddr_t macAddr;
            char wan_mac[18];

            if (TRUE == pCfg->meshEbEnabled)
            {
                v_secure_system("/bin/sh /etc/utopia/service.d/vlan_util_xb7.sh meshethbhaul-removeEthwan 0");
            }
            v_secure_system("ifconfig %s down",pCfg->ethwan_ifname);
            v_secure_system("ip addr flush dev %s",pCfg->ethwan_ifname);
            v_secure_system("ip -6 addr flush dev %s",pCfg->ethwan_ifname);
            v_secure_system("sysctl -w net.ipv6.conf.%s.accept_ra=0",pCfg->ethwan_ifname);
            v_secure_system("ifconfig %s down; ip link set %s name dummy-rf", pCfg->wanPhyName,pCfg->wanPhyName);

            v_secure_system("brctl addbr %s", pCfg->wanPhyName);
            v_secure_system("brctl addif %s %s", pCfg->wanPhyName,pCfg->ethwan_ifname);
            v_secure_system("sysctl -w net.ipv6.conf.%s.autoconf=0", pCfg->ethwan_ifname);
            v_secure_system("sysctl -w net.ipv6.conf.%s.disable_ipv6=1", pCfg->ethwan_ifname);
            v_secure_system("ip6tables -I OUTPUT -o %s -p icmpv6 -j DROP", pCfg->ethwan_ifname);
            if (0 != pCfg->bridgemode)
            {
                v_secure_system("/bin/sh /etc/utopia/service.d/service_bridge_puma7.sh bridge-restart");
            }

            EthLink_BridgeNfDisable(pCfg->wanPhyName, NF_ARPTABLE, TRUE);
            EthLink_BridgeNfDisable(pCfg->wanPhyName, NF_IPTABLE, TRUE);
            EthLink_BridgeNfDisable(pCfg->wanPhyName, NF_IP6TABLE, TRUE);

            v_secure_system("ip link set %s up",ETHWAN_DOCSIS_INF_NAME);
            v_secure_system("brctl addif %s %s", pCfg->wanPhyName,ETHWAN_DOCSIS_INF_NAME);
            v_secure_system("sysctl -w net.ipv6.conf.%s.disable_ipv6=1",ETHWAN_DOCSIS_INF_NAME);

            memset(&macAddr,0,sizeof(macaddr_t));
            EthLink_GetInterfaceMacAddress(&macAddr,"dummy-rf"); //dummy-rf is renamed from erouter0
            memset(wan_mac,0,sizeof(wan_mac));
            snprintf(wan_mac, sizeof(wan_mac), "%02x:%02x:%02x:%02x:%02x:%02x", macAddr.hw[0], macAddr.hw[1], macAddr.hw[2],
                    macAddr.hw[3], macAddr.hw[4], macAddr.hw[5]);
            v_secure_system("ifconfig %s down", pCfg->wanPhyName);
            v_secure_system("ifconfig %s hw ether %s",  pCfg->wanPhyName,wan_mac);
            v_secure_system("echo %s > /sys/bus/platform/devices/toe/in_if", pCfg->wanPhyName);
            v_secure_system("ifconfig %s up", pCfg->wanPhyName);
        }
        else
        {
            v_secure_system("brctl addif %s %s", pCfg->wanPhyName,pCfg->ethwan_ifname);
        }

        v_secure_system("ifconfig %s up",pCfg->ethwan_ifname);
        v_secure_system("cmctl down");

    }
    else
    {
        v_secure_system("brctl delif %s %s", pCfg->wanPhyName,pCfg->ethwan_ifname);

        if (pCfg->ovsEnabled)
        {
            v_secure_system("/usr/bin/bridgeUtils add-port brlan0 %s",pCfg->ethwan_ifname);
        }
        else
        {
            v_secure_system("brctl addif brlan0 %s",pCfg->ethwan_ifname);
        }

        if (TRUE == pCfg->configureBridge)
        {
            if (TRUE == pCfg->meshEbEnabled)
            {
                v_secure_system("/bin/sh /etc/utopia/service.d/vlan_util_xb7.sh meshethbhaul-up 0");
            }
            if (0 != pCfg->bridgemode)
            {
                v_secure_system("/bin/sh /etc/utopia/service.d/service_bridge_puma7.sh bridge-restart");
            }

            v_secure_system("brctl delif %s %s", pCfg->wanPhyName,ETHWAN_DOCSIS_INF_NAME);
            v_secure_system("ifconfig %s down", pCfg->wanPhyName);
            v_secure_system("brctl delbr %s", pCfg->wanPhyName);
            v_secure_system("ip link set dummy-rf name %s", pCfg->wanPhyName);
            v_secure_system("echo %s > /sys/bus/platform/devices/toe/in_if", pCfg->wanPhyName);
            v_secure_system("ifconfig %s up", pCfg->wanPhyName);
       }
    }

    return 0;

}

static INT EthLink_Hal_BridgeConfigBcm(WAN_MODE_BRIDGECFG *pCfg)
{
    if (!pCfg)
    {
        CcspTraceError(("%s - %d : Invalid input arguement\n", __FUNCTION__, __LINE__));
        return -1;
    }

    if (pCfg->ethWanEnabled == TRUE)
    {
        if (pCfg->ovsEnabled == TRUE)
        {
            v_secure_system("/usr/bin/bridgeUtils del-port brlan0 %s",pCfg->ethwan_ifname);
        }
        else
        {
            v_secure_system("brctl delif brlan0 %s",pCfg->ethwan_ifname);

        }

        if (TRUE == pCfg->configureBridge)
        {
            v_secure_system("ifconfig %s down",pCfg->ethwan_ifname);
            v_secure_system("ip addr flush dev %s",pCfg->ethwan_ifname);
            v_secure_system("ip -6 addr flush dev %s",pCfg->ethwan_ifname);
            v_secure_system("sysctl -w net.ipv6.conf.%s.accept_ra=0",pCfg->ethwan_ifname);

            if (0 == pCfg->bridgemode)
            {
                CcspTraceInfo(("set CM0 %s wanif %s\n",ETHWAN_DOCSIS_INF_NAME,pCfg->wanPhyName));
                v_secure_system("ifconfig %s down", pCfg->wanPhyName);
                v_secure_system("ip link set %s name %s", pCfg->wanPhyName,ETHWAN_DOCSIS_INF_NAME);
                v_secure_system("brctl addbr %s", pCfg->wanPhyName);
                v_secure_system("brctl addif %s %s", pCfg->wanPhyName,pCfg->ethwan_ifname);

                v_secure_system("sysctl -w net.ipv6.conf.%s.autoconf=0", pCfg->ethwan_ifname);
                v_secure_system("sysctl -w net.ipv6.conf.%s.disable_ipv6=1", pCfg->ethwan_ifname);
                v_secure_system("ip6tables -I OUTPUT -o %s -p icmpv6 -j DROP", pCfg->ethwan_ifname);
                v_secure_system("ip link set %s up",ETHWAN_DOCSIS_INF_NAME);
                v_secure_system("brctl addif %s %s", pCfg->wanPhyName,ETHWAN_DOCSIS_INF_NAME);
                v_secure_system("sysctl -w net.ipv6.conf.%s.disable_ipv6=1",ETHWAN_DOCSIS_INF_NAME);
            }
            else
            {
#if defined (ENABLE_WANMODECHANGE_NOREBOOT)
                v_secure_system("touch /tmp/wanmodechange");
                if (pCfg->ovsEnabled)
                {
                    v_secure_system("/usr/bin/bridgeUtils multinet-syncMembers 1");
                    v_secure_system("/usr/bin/bridgeUtils del-port brlan0 %s",pCfg->ethwan_ifname);
                }
                else
                {

#if defined (_CBR2_PRODUCT_REQ_)
                    v_secure_system("/bin/sh /etc/utopia/service.d/vlan_util_tchcbr.sh multinet-syncMembers 1");
#else
                    v_secure_system("/bin/sh /etc/utopia/service.d/vlan_util_tchxb6.sh multinet-syncMembers 1");
#endif
                    v_secure_system("brctl delif brlan0 %s",pCfg->ethwan_ifname);
                }
                v_secure_system("rm /tmp/wanmodechange");
#endif

                v_secure_system("brctl addif %s %s", pCfg->wanPhyName,pCfg->ethwan_ifname);
            }
        }
        else
        {
            v_secure_system("brctl addif %s %s", pCfg->wanPhyName,pCfg->ethwan_ifname);
        }

        v_secure_system("ifconfig %s up",pCfg->ethwan_ifname);

    }
    else
    {
        v_secure_system("brctl delif %s %s", pCfg->wanPhyName,pCfg->ethwan_ifname);

        if (pCfg->ovsEnabled)
        {
            v_secure_system("/usr/bin/bridgeUtils add-port brlan0 %s",pCfg->ethwan_ifname);
        }
        else
        {
            v_secure_system("brctl addif brlan0 %s",pCfg->ethwan_ifname);
        }
        if (TRUE == pCfg->configureBridge)
        {
            if (0 == pCfg->bridgemode)
            {
                v_secure_system("brctl delif %s %s", pCfg->wanPhyName,ETHWAN_DOCSIS_INF_NAME);
                v_secure_system("ifconfig %s down", pCfg->wanPhyName);
                v_secure_system("brctl delbr %s", pCfg->wanPhyName);
                v_secure_system("ifconfig %s down", ETHWAN_DOCSIS_INF_NAME);
                v_secure_system("ip link set %s name %s", ETHWAN_DOCSIS_INF_NAME,pCfg->wanPhyName);
                v_secure_system("ifconfig %s up", pCfg->wanPhyName);
                // BCOMB-1508 - update 2 into /sys/class/net/erouter0/netdev_group
                v_secure_system("ip link set dev %s group 2", pCfg->wanPhyName);
                v_secure_system("echo addif %s > /proc/driver/flowmgr/cmd", pCfg->wanPhyName);
                v_secure_system("echo delif %s > /proc/driver/flowmgr/cmd", ETHWAN_DOCSIS_INF_NAME);
            }
            else
            {
#if defined (ENABLE_WANMODECHANGE_NOREBOOT)
                v_secure_system("touch /tmp/wanmodechange");
                if (pCfg->ovsEnabled)
                {
                    v_secure_system("/usr/bin/bridgeUtils multinet-syncMembers 1");
                }
                else
                {
#if defined (_CBR2_PRODUCT_REQ_)
                    v_secure_system("/bin/sh /etc/utopia/service.d/vlan_util_tchcbr.sh multinet-syncMembers 1");
#else
                    v_secure_system("/bin/sh /etc/utopia/service.d/vlan_util_tchxb6.sh multinet-syncMembers 1");
#endif
                }
                v_secure_system("rm /tmp/wanmodechange");
#endif
            }

        }
    }

    return 0;
}

static BOOL EthLink_IsWanEnabled()
{
    char buf[64];
    memset(buf,0,sizeof(buf));
    if (syscfg_get(NULL, "eth_wan_enabled", buf, sizeof(buf)) == 0)
    {
        if ( 0 == strcmp(buf,"true"))
        {

            if ( 0 == access( "/nvram/ETHWAN_ENABLE" , F_OK ) )
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}


static void EthLink_GetInterfaceMacAddress(macaddr_t* macAddr,char *pIfname)
{
    FILE *f = NULL;
    char line[256], *lineptr = line, fname[128];
    size_t size;

    if (pIfname)
    {
        snprintf(fname,sizeof(fname), "/sys/class/net/%s/address", pIfname);
        size = sizeof(line);
        if ((f = fopen(fname, "r")))
        {
            if ((getline(&lineptr, &size, f) >= 0))
            {
                sscanf(lineptr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &macAddr->hw[0], &macAddr->hw[1], &macAddr->hw[2], &macAddr->hw[3], &macAddr->hw[4], &macAddr->hw[5]);
            }
            fclose(f);
        }
    }

    return;
}

static INT EthLink_BridgeNfDisable( const char* bridgeName, bridge_nf_table_t table, BOOL disable )
{
    INT ret = 0;
    char disableFile[100] = {0};
    char* pTableName = NULL;

    if (bridgeName == NULL)
    {
        CcspTraceError(("bridgeName is NULLn"));
        return -1;
    }

    switch (table)
    {
        case NF_ARPTABLE:
            pTableName = "nf_disable_arptables";
            break;
        case NF_IPTABLE:
            pTableName = "nf_disable_iptables";
            break;
        case NF_IP6TABLE:
            pTableName = "nf_disable_ip6tables";
            break;
        default:
            // invalid table
            CcspTraceError(("Invalid table: %d\n", table));
            return -1;
    }

    snprintf(disableFile, sizeof(disableFile), "/sys/devices/virtual/net/%s/bridge/%s",bridgeName, pTableName);
    int fd = open(disableFile, O_WRONLY);
    if (fd != -1)
    {
        ret = 0;
        char val = disable ? '1' : '0';
        int num = write(fd, &val, 1);
        if (num != 1)
        {
            CcspTraceError(("Failed to write: %d\n", num));
            ret = -1;
        }

        close(fd);
    }
    else
    {
        CcspTraceError(("Failed to open %s\n", disableFile));
        ret = -1;
    }

    return ret;
}
#endif //COMCAST_VLAN_HAL_ENABLED
