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
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "vlan_mgr_apis.h"
#include "vlan_apis.h"
#include "vlan_internal.h"
#include "plugin_main_apis.h"
#include "ethernet_apis.h"
#include "ethernet_dml.h"
#include "vlan_eth_hal.h"

#ifdef FEATURE_MAPT
#include "sysevent/sysevent.h"

#define IS_EMPTY_STRING(s)    ((s == NULL) || (*s == '\0'))
#define SYSEVENT_WAN_IFACE_NAME "wan_ifname"
#define BUFLEN_64 64

#define PARAM_SIZE_32 32
#define PARAM_SIZE_64 64

extern int sysevent_fd;
extern token_t sysevent_token;
#endif
static pthread_mutex_t vlan_access_mutex;

static ANSC_STATUS Vlan_CreateTaggedInterface(PDML_VLAN pEntry);
static ANSC_STATUS Vlan_SetEthLink(PDML_VLAN pEntry, BOOL enable, BOOL PriTag);
extern ANSC_STATUS EthLink_SendVirtualIfaceVlanStatus(char *path, char *vlanStatus);
#if !defined(VLAN_MANAGER_HAL_ENABLED)
static ANSC_STATUS Vlan_DeleteInterface(PDML_VLAN p_Vlan);
static ANSC_STATUS Vlan_SetMacAddr(PDML_VLAN pEntry);
#endif

/*****************************************************************/
//VLAN APIs

static ANSC_STATUS Vlan_GetTaggedVlanInterfaceStatus(const char *iface, vlan_link_status_e *status)
{
    int sfd;
    int flag = FALSE;
    struct ifreq intf;

    if(iface == NULL)
    {
       *status = VLAN_IF_NOTPRESENT;
       CcspTraceError(("%s - %d : Invalid Interface\n", __FUNCTION__, __LINE__));
       return ANSC_STATUS_FAILURE;
    }

    if ((sfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        *status = VLAN_IF_ERROR;
        CcspTraceError(("%s - %d : Socket creation Failed\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    memset (&intf, 0, sizeof(struct ifreq));
    strncpy(intf.ifr_name, iface, sizeof(intf.ifr_name) - 1);

    if (ioctl(sfd, SIOCGIFFLAGS, &intf) == -1) {
        *status = VLAN_IF_ERROR;
    } else {
        flag = (intf.ifr_flags & IFF_RUNNING) ? TRUE : FALSE;
    }

    if(flag == TRUE)
        *status = VLAN_IF_UP;
    else
        *status = VLAN_IF_DOWN;

    close(sfd);

    return ANSC_STATUS_SUCCESS;
}

void get_uptime(long *uptime)
{
    struct sysinfo info;
    sysinfo( &info );
    *uptime= info.uptime;
}

#if !defined(VLAN_MANAGER_HAL_ENABLED)
static ANSC_STATUS Vlan_DeleteInterface(PDML_VLAN p_Vlan)
{
     if (NULL == p_Vlan)
     {
          CcspTraceError(("Error: Invalid arguement \n"));
          return ANSC_STATUS_FAILURE;
     }

     v_secure_system("ip link delete %s", p_Vlan->Name);

     return ANSC_STATUS_SUCCESS;
}
#endif

/**********************************************************************

    caller:     self

    prototype:

        BOOL
        Vlan_Init
            (
            );

        Description:
            This is the initialization routine for VLAN backend.

        Return:
            Status of operation.

**********************************************************************/
ANSC_STATUS Vlan_Init( void )
{
    ANSC_STATUS  returnStatus = ANSC_STATUS_SUCCESS;

#if defined(VLAN_MANAGER_HAL_ENABLED)
    returnStatus = vlan_eth_hal_init();
    if (returnStatus != ANSC_STATUS_SUCCESS) {
        CcspTraceError(("%s-%d: vlan_eth_hal_init failed\n", __FUNCTION__, __LINE__));
    }
#endif
    return returnStatus;
}

/**********************************************************************

    caller:     self

    prototype:

        ANSC_STATUS
        Vlan_GetStatus
            (
                PCOSA_DML_VLAN      pEntry
            );

    Description:
        The API updated current state of a VLAN interface
    Arguments:
        pEntry      The new configuration is passed through this argument, even Alias field can be changed.

    Return:
        Status of the operation

**********************************************************************/
ANSC_STATUS
Vlan_GetStatus
    (
        PDML_VLAN           pEntry
    )
{
    ANSC_STATUS returnStatus = ANSC_STATUS_SUCCESS;
    vlan_link_status_e status;

    if ( pEntry != NULL )
    {
        if (ANSC_STATUS_SUCCESS != Vlan_GetTaggedVlanInterfaceStatus(pEntry->Name, &status))
        {
            pEntry->Status = VLAN_IF_ERROR;
            CcspTraceError(("%s-%d: Failed to Get Tagged Vlan Interface=%s Status \n", __FUNCTION__, __LINE__, pEntry->Name));
        }
        else
        {
            pEntry->Status = status;
        }
    }
    return returnStatus;
}

static ANSC_STATUS Vlan_SetEthLink(PDML_VLAN pEntry, BOOL enable, BOOL PriTag)
{
    INT iEthLinkInstance = -1;
    INT EthLinkInstance = -1;
    ANSC_HANDLE pNewEntry = NULL;


    if (NULL == pEntry)
    {
        CcspTraceError(("%s Invalid buffer\n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    if (strlen(pEntry->LowerLayers) > 0)
        sscanf( pEntry->LowerLayers, "Device.X_RDK_Ethernet.Link.%d", &iEthLinkInstance);

    CcspTraceInfo(("%s-%d: iEthLinkInstance=%d \n",__FUNCTION__, __LINE__, iEthLinkInstance));
    if (iEthLinkInstance > 0)
    {
        pNewEntry = EthLink_GetEntry(NULL, (iEthLinkInstance - 1), (PULONG)&EthLinkInstance);
        if (pNewEntry == NULL)
        {
           CcspTraceError(("%s Failed to add table \n", __FUNCTION__));
           return ANSC_STATUS_FAILURE;
        }
    }
    else
    {
        CcspTraceError(("%s Failed to get EthLink Instance \n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    //Set PriorityTagging.
    if (enable == TRUE)
    {
        if (EthLink_SetParamBoolValue(pNewEntry, "PriorityTagging", PriTag) != TRUE)
        {
            CcspTraceError(("%s - Failed to set Enable data model\n", __FUNCTION__));
            return ANSC_STATUS_FAILURE;
        }
    }

    //Set Enable.
    if (EthLink_SetParamBoolValue(pNewEntry, "Enable", enable) != TRUE)
    {
        CcspTraceError(("%s - Failed to set Enable data model\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    //Set PriorityTagging.
    if (enable == FALSE)
    {
        if (EthLink_SetParamBoolValue(pNewEntry, "PriorityTagging", PriTag) != TRUE)
        {
            CcspTraceError(("%s - Failed to set Enable data model\n", __FUNCTION__));
            return ANSC_STATUS_FAILURE;
        }
    }

    //Commit
    if (EthLink_Commit(pNewEntry) != ANSC_STATUS_SUCCESS)
    {
        CcspTraceError(("%s - Failed to commit data model changes\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    CcspTraceInfo(("%s-%d:Successfully Set EthLink %s\n", __FUNCTION__, __LINE__, pEntry->Name));

    return ANSC_STATUS_SUCCESS;
}

#ifdef FEATURE_MAPT
/**********************************************************************

    caller:     self

    prototype:

        static int
        get_wan_interface_name(char *wan_if)

    Description:
        The API gets the wan interface name.

**********************************************************************/
static int get_wan_interface_name(char *wan_if)
{
    int ret = 0;
    char wan_ifname[BUFLEN_64] = {0};

    if (sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_WAN_IFACE_NAME,  wan_ifname, sizeof(wan_ifname)) == 0)
    {
        strcpy(wan_if, wan_ifname);
        ret = 1;
    }

    return ret;

}

/**********************************************************************

    caller:     self

    prototype:

        void
        mapt_ivi_check()

    Description:
        The API check the ivi.ko loaded or not. If present unload it before delete the vlan interface.

**********************************************************************/
void mapt_ivi_check() {
    FILE *file;
    char line[64];

    file = popen("cat /proc/modules | grep ivi","r");

    if( file == NULL) {
        CcspTraceError(("[%s][%d]Failed to open  /proc/modules \n", __FUNCTION__, __LINE__));
    }
    else {
        if( fgets (line, 64, file)!=NULL ) {
            if( strstr(line, "ivi")) {
                v_secure_system("ivictl -q");
                v_secure_system("rmmod -f /lib/modules/`uname -r`/extra/ivi.ko");
                sleep(1);
                CcspTraceInfo(("%s - ivi.ko removed\n", __FUNCTION__));
            }
        }
        pclose(file);
    }
}
#endif

void * Vlan_Disable(void *Arg)
{
    int ret;
    vlan_link_status_e status;

    PDML_VLAN pEntry = (PDML_VLAN)Arg;
    if ( NULL == pEntry )
    {
        CcspTraceError(("%s-%d: Failed, pEntry Arument is Null\n", __FUNCTION__, __LINE__));
        pthread_exit(NULL);
    }

    pthread_detach(pthread_self());

    pthread_mutex_lock(&vlan_access_mutex);
    //Set EthLink to False. it will take care UnTagged Created Vlan Interface
    if (Vlan_SetEthLink(pEntry, FALSE, FALSE) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("%s-%d: Failed to Enable EthLink\n", __FUNCTION__, __LINE__));
    }

    //Delete Created Tagged Vlan Interface
    if (pEntry->VLANId > 0)
    {
#ifdef FEATURE_MAPT
        char wan_ifname[BUFLEN_64] = {0};

        if(get_wan_interface_name(wan_ifname) ) {
            CcspTraceInfo(("[%s][%d]wan_ifname %s \n", __FUNCTION__, __LINE__, wan_ifname));
            if(! strcmp(pEntry->Name, wan_ifname)) {
               mapt_ivi_check();
            }
        }
#endif
        ret = Vlan_GetTaggedVlanInterfaceStatus(pEntry->Name, &status);
        if (ret != ANSC_STATUS_SUCCESS)
        {
            CcspTraceError(("[%s][%d] %s: Failed to get vlan interface status \n", __FUNCTION__, __LINE__, pEntry->Name));
        }
#if defined(VLAN_MANAGER_HAL_ENABLED)
        if ( ( status != VLAN_IF_NOTPRESENT ) && ( status != VLAN_IF_ERROR ) )
        {
	    ANSC_STATUS returnStatus = ANSC_STATUS_SUCCESS;
            returnStatus = vlan_eth_hal_deleteInterface(pEntry->Name, pEntry->InstanceNumber);
            if ( ANSC_STATUS_SUCCESS != returnStatus )
            {
                CcspTraceError(("%s - Failed to delete VLAN interface %s\n", __FUNCTION__, pEntry->Name));
            }
        }
        else
        {
            CcspTraceInfo(("%s - No VLAN interface found with this name %s\n", __FUNCTION__, pEntry->Name));
        }
#else
        Vlan_DeleteInterface(pEntry);
#endif
    }
    pEntry->Status = VLAN_IF_DOWN;
    EthLink_SendVirtualIfaceVlanStatus(pEntry->Path, "Down");
    CcspTraceInfo(("%s - %s:Successfully deleted VLAN interface %s\n", __FUNCTION__, VLAN_MARKER_VLAN_IF_CREATE, pEntry->Name));

    pthread_mutex_unlock(&vlan_access_mutex);
    pthread_exit(NULL);

}

#if !defined(VLAN_MANAGER_HAL_ENABLED)
static ANSC_STATUS Vlan_GetEthLinkMacOffSet(PDML_VLAN pEntry, int* pOffSet)
{
    INT iEthLinkInstance = -1;
    INT EthLinkInstance = -1;
    ANSC_HANDLE pNewEntry = NULL;


    if (NULL == pEntry)
    {
        CcspTraceError(("%s Invalid buffer\n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    if (strlen(pEntry->LowerLayers) > 0)
        sscanf( pEntry->LowerLayers, "Device.X_RDK_Ethernet.Link.%d", &iEthLinkInstance);

    CcspTraceInfo(("%s-%d: iEthLinkInstance=%d \n",__FUNCTION__, __LINE__, iEthLinkInstance));
    if (iEthLinkInstance > 0)
    {
        pNewEntry = EthLink_GetEntry(NULL, (iEthLinkInstance - 1), (PULONG)&EthLinkInstance);
        if (pNewEntry == NULL)
        {
           CcspTraceError(("%s Failed to add table \n", __FUNCTION__));
           return ANSC_STATUS_FAILURE;
        }
    }
    else
    {
        CcspTraceError(("%s Failed to get EthLink Instance \n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    //Get MacOffSet.
    if (EthLink_GetParamIntValue(pNewEntry, "MACAddrOffSet", pOffSet) != TRUE)
    {
        CcspTraceError(("%s - Failed to set Enable data model\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

// Mac address schema Handling for VLAN interface.
static ANSC_STATUS Vlan_SetMacAddr( PDML_VLAN pEntry )
{
    unsigned long long int number, new_mac;
    char acTmpReturnValue[256] = {0};
    char hex[32];
    char macStr[32];
    int i, j = 0;
    ULONG add = 0;


    if(NULL == pEntry)
    {
        CcspTraceInfo(("[%s][%d] Failed to set Mac Address\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    if(0 != platform_hal_GetBaseMacAddress(acTmpReturnValue))
    {
        CcspTraceError(("[%s][%d]Failed to get BaseMacAddress from HAL API\n", __FUNCTION__, __LINE__));

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

    if (Vlan_GetEthLinkMacOffSet(pEntry, &add) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("%s - Failed to set Enable data model\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    new_mac = number + add;

    snprintf(hex, sizeof(hex), "%016llx", new_mac);
    snprintf(macStr, sizeof(macStr), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
    hex[4], hex[5], hex[6], hex[7], hex[8], hex[9], hex[10], hex[11], hex[12], hex[13], hex[14], hex[15]);

    CcspTraceInfo(("%s-%d: macStr:%s,pEntry->Name:%s\n", __FUNCTION__, __LINE__, macStr, pEntry->Name));

    v_secure_system("ip link set dev %s address %s",pEntry->Name, macStr);

    return ANSC_STATUS_SUCCESS;
}

#endif
/**********************************************************************

    caller:     self

    prototype:

        ANSC_STATUS
        DmlSetVlan
            (
                PDML_VLAN      pEntry
            );

    Description:
        The API re-configures the designated VLAN table entry.
    Arguments:
        pEntry      The new configuration is passed through this argument, even Alias field can be changed.

    Return:
        Status of the operation

**********************************************************************/
#if defined(VLAN_MANAGER_HAL_ENABLED)
static ANSC_STATUS Vlan_CreateTaggedInterface(PDML_VLAN pEntry)
{
    ANSC_STATUS returnStatus = ANSC_STATUS_SUCCESS;
    vlan_configuration_t VlanCfg;

    if (pEntry == NULL)
    {
        CcspTraceError(("%s-%d: Failed to Create Tagged Vlan Interface\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    memset (&VlanCfg, 0, sizeof(vlan_configuration_t));

    strncpy(VlanCfg.BaseInterface, pEntry->BaseInterface, sizeof(VlanCfg.BaseInterface) - 1);
    strncpy(VlanCfg.L3Interface, pEntry->Name, sizeof(VlanCfg.L3Interface) - 1);
    strncpy(VlanCfg.L2Interface, pEntry->BaseInterface, sizeof(VlanCfg.L2Interface) - 1);
    VlanCfg.VLANId = pEntry->VLANId;
    VlanCfg.TPId   = pEntry->TPId;

    if (EthLink_GetMarking(pEntry->Alias, &VlanCfg) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("%s Failed to Get Marking, so Can't Create Vlan Interface(%s) \n", __FUNCTION__, pEntry->Alias));
        return ANSC_STATUS_FAILURE;
    }

    vlan_eth_hal_createInterface(&VlanCfg);

    //Free VlanCfg skb_config memory
    if (VlanCfg.skb_config != NULL)
    {
        free(VlanCfg.skb_config);
        VlanCfg.skb_config = NULL;
    }

    return returnStatus;
}
#else
static ANSC_STATUS Vlan_CreateTaggedInterface(PDML_VLAN pEntry)
{
    ANSC_STATUS returnStatus = ANSC_STATUS_SUCCESS;

    if (pEntry == NULL)
    {
        CcspTraceError(("%s-%d: Failed to Create Tagged Vlan Interface\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    v_secure_system("ip link add link %s name %s type vlan id %u", pEntry->Alias , pEntry->Name, pEntry->VLANId);

    v_secure_system("ip link set %s up", pEntry->Name);

    if (Vlan_SetMacAddr(pEntry) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("%s Failed to Set MacAddress \n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    return returnStatus;
}
#endif

void * Vlan_Enable(void *Arg)
{
    ANSC_STATUS returnStatus  = ANSC_STATUS_SUCCESS;
    vlan_link_status_e status;
    INT iIterator = 0;

    PDML_VLAN pEntry = (PDML_VLAN)Arg;
    if ( NULL == pEntry )
    {
        CcspTraceError(("%s-%d: Failed, pEntry Argument is Null\n", __FUNCTION__, __LINE__));
        pthread_exit(NULL);
    }

    pthread_detach(pthread_self());

    pthread_mutex_lock(&vlan_access_mutex);
    //Create Vlan Tagged Interface
    if(pEntry->VLANId > 0) {
        if (Vlan_SetEthLink(pEntry, TRUE, TRUE) == ANSC_STATUS_FAILURE)
        {
            CcspTraceError(("%s-%d: Failed to Enable EthLink\n", __FUNCTION__, __LINE__));
        }

        if (Vlan_GetTaggedVlanInterfaceStatus(pEntry->Name, &status) != ANSC_STATUS_SUCCESS)
        {
            CcspTraceError(("[%s][%d]Failed to get vlan interface status \n", __FUNCTION__, __LINE__));
        }
#if defined(VLAN_MANAGER_HAL_ENABLED)
        if ( ( status != VLAN_IF_NOTPRESENT ) && ( status != VLAN_IF_ERROR ) )
        {
            CcspTraceInfo(("%s %s:VLAN interface(%s) already exists, delete it first\n", __FUNCTION__, VLAN_MARKER_VLAN_IF_CREATE, pEntry->Name));
            returnStatus = vlan_eth_hal_deleteInterface(pEntry->Name, pEntry->InstanceNumber);
            if (ANSC_STATUS_SUCCESS != returnStatus)
            {
                CcspTraceError(("%s - Failed to delete the existing VLAN interface %s\n", __FUNCTION__, pEntry->Name));
            }
            CcspTraceInfo(("%s - %s:Successfully deleted VLAN interface %s\n", __FUNCTION__, VLAN_MARKER_VLAN_IF_DELETE, pEntry->Name));
        }
#endif
        returnStatus = Vlan_CreateTaggedInterface(pEntry);
        if (ANSC_STATUS_SUCCESS != returnStatus)
        {
            pEntry->Status = VLAN_IF_ERROR;
            CcspTraceError(("[%s][%d]Failed to create VLAN Tagged interface \n", __FUNCTION__, __LINE__));
        }

        //Get status of VLAN link
        while(iIterator < 10)
        {
            if (ANSC_STATUS_FAILURE == Vlan_GetTaggedVlanInterfaceStatus(pEntry->Name, &status))
            {
                CcspTraceError(("%s-%d: Failed to get Tagged Vlan Interface=%s Status \n", __FUNCTION__, __LINE__, pEntry->Name));
            }

            if (VLAN_IF_UP == status)
            {
                EthLink_SendVirtualIfaceVlanStatus(pEntry->Path, "Up");
                CcspTraceInfo(("%s-%d: Successfully Updated Vlan Status to WanManager for Interface(%s) \n", __FUNCTION__, __LINE__, pEntry->Name));
                break;
            }

            iIterator++;
            sleep(2);
            CcspTraceInfo(("%s-%d: Interface Status(%d), retry-count=%d \n", __FUNCTION__, __LINE__, status, iIterator));
        }
        long uptime = 0;
        get_uptime(&uptime);
        pEntry->LastChange  =  uptime;
    }
    else
    {
        //Enable EthLink and it will take care Creation of UnTagged Vlan Interface.
        if (Vlan_SetEthLink(pEntry, TRUE, FALSE) == ANSC_STATUS_FAILURE)
        {
            CcspTraceError(("%s-%d: Failed to Enable EthLink\n", __FUNCTION__, __LINE__));
        }
    }
    pEntry->Status = VLAN_IF_UP;

    pthread_mutex_unlock(&vlan_access_mutex);
    pthread_exit(NULL);

}
void VLAN_InitMutex()
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&vlan_access_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}
void VLAN_DelMutex()
{
    pthread_mutex_destroy(&vlan_access_mutex);
}
