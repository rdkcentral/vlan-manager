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

#include "vlan_mgr_apis.h"
#include "ethernet_apis.h"
#include "ethernet_internal.h"
#include "plugin_main_apis.h"
#include "poam_irepfo_interface.h"
#include "sys_definitions.h"
#include "ccsp_psm_helper.h"

extern ANSC_HANDLE              bus_handle;

/**********************************************************************

    caller:     owner of the object

    prototype:

        ANSC_HANDLE
        EthernetCreate
            (
            );

    description:

        This function constructs cosa ethernet object and return handle.

    argument:

    return:     newly created ethernet object.

**********************************************************************/

ANSC_HANDLE
EthernetCreate
    (
        VOID
    )
{
    PDATAMODEL_ETHERNET        pMyObject    = (PDATAMODEL_ETHERNET) NULL;

    /*
     * We create object by first allocating memory for holding the variables and member functions.
     */
    pMyObject = (PDATAMODEL_ETHERNET)AnscAllocateMemory(sizeof(DATAMODEL_ETHERNET));

    if ( !pMyObject )
    {
        return  (ANSC_HANDLE)NULL;
    }

    /*
     * Initialize the common variables and functions for a container object.
     */
    //pMyObject->Oid             = DATAMODEL_ETHERNET_OID;
    pMyObject->Create            = EthernetCreate;
    pMyObject->Remove            = EthernetRemove;
    pMyObject->Initialize        = EthernetInitialize;

    pMyObject->Initialize   ((ANSC_HANDLE)pMyObject);

    return  (ANSC_HANDLE)pMyObject;
}

static ANSC_STATUS DmlEthGetPSMRecordValue ( char *pPSMEntry, char *pOutputString )
{
    int ret_val = ANSC_STATUS_SUCCESS;
    int   retPsmGet = CCSP_SUCCESS;
    char *strValue  = NULL;

    //Validate buffer
    if( ( NULL == pPSMEntry ) && ( NULL == pOutputString ) )
    {
        CcspTraceError(("%s %d Invalid buffer\n",__FUNCTION__,__LINE__));
        return retPsmGet;
    }

    retPsmGet = PSM_VALUE_GET_VALUE(pPSMEntry, strValue);

    if ( retPsmGet == CCSP_SUCCESS )
    {
        //Copy till end of the string
        snprintf( pOutputString, strlen( strValue ) + 1, "%s", strValue );

        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    return ret_val;
}

static ANSC_STATUS EthLink_Initialize( ANSC_HANDLE hThisObject)
{
    PDATAMODEL_ETHERNET pMyObject = (PDATAMODEL_ETHERNET)hThisObject;
    PDML_ETHERNET pEthCfg = NULL;
    char acPSMQuery[128]    = { 0 };
    char acPSMValue[64]     = { 0 };
    INT linkCount = 0;
    INT nIndex = 0;
    int ret ;

    /* get link count */
    snprintf( acPSMQuery, sizeof( acPSMQuery ), PSM_ETHLINK_COUNT );
    ret = DmlEthGetPSMRecordValue( acPSMQuery, acPSMValue )   ;
    if ( ret == 0)
    {
        linkCount = atoi (acPSMValue);
    }

    pMyObject->ulEthlinkInstanceNumber = linkCount ;

    pEthCfg = (PDML_ETHERNET)AnscAllocateMemory(sizeof(DML_ETHERNET)* linkCount);
    if (pEthCfg == NULL)
    {
        CcspTraceError(("%s-%d: Failed to Alloc Memory for EthLink \n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    memset(pEthCfg, 0, sizeof(DML_ETHERNET) * linkCount);

    for(nIndex = 0; nIndex < linkCount; nIndex++)
    {
	 DML_ETHERNET_INIT(&pEthCfg[nIndex]);
         pEthCfg[nIndex].InstanceNumber = nIndex + 1;

         /* get enable value from psm */
         snprintf( acPSMQuery, sizeof( acPSMQuery ), PSM_ETHLINK_ENABLE,nIndex + 1 );
         ret = DmlEthGetPSMRecordValue( acPSMQuery, acPSMValue )   ;
         if ( ret == 0)
         {
              if(strcmp (acPSMValue ,PSM_ENABLE_STRING_TRUE) == 0)
              {
                    pEthCfg[nIndex].Enable = TRUE ;
              }
              else
              {
                    pEthCfg[nIndex].Enable = FALSE;
              }

         }

         /* get alias from psm */
         snprintf( acPSMQuery, sizeof( acPSMQuery ), PSM_ETHLINK_ALIAS,nIndex + 1 );
         ret = DmlEthGetPSMRecordValue( acPSMQuery, acPSMValue )   ;
         if ( ret == 0)
         {
       	      strcpy(pEthCfg[nIndex].Alias, acPSMValue);
         }

         /* get name from psm */
         snprintf( acPSMQuery, sizeof( acPSMQuery ), PSM_ETHLINK_NAME,nIndex + 1 );
         ret = DmlEthGetPSMRecordValue( acPSMQuery, acPSMValue )   ;
         if ( ret == 0)
         {
              strcpy(pEthCfg[nIndex].Name, acPSMValue);
         }

         /* get lowerlayers from psm */
         snprintf( acPSMQuery, sizeof( acPSMQuery ), PSM_ETHLINK_LOWERLAYERS,nIndex + 1 );
         ret = DmlEthGetPSMRecordValue( acPSMQuery, acPSMValue )   ;
         if ( ret == 0)
         {
              strcpy(pEthCfg[nIndex].LowerLayers, acPSMValue);
         }

         /* get base interface from psm */
         snprintf( acPSMQuery, sizeof( acPSMQuery ), PSM_ETHLINK_BASEIFACE,nIndex + 1 );
         ret = DmlEthGetPSMRecordValue( acPSMQuery, acPSMValue )   ;
         if ( ret == 0)
         {
              strcpy(pEthCfg[nIndex].BaseInterface, acPSMValue);
         }

         snprintf( acPSMQuery, sizeof( acPSMQuery ), PSM_ETHLINK_MACOFFSET,nIndex + 1 );
         ret = DmlEthGetPSMRecordValue( acPSMQuery, acPSMValue )   ;
         if ( ret == 0)
         {
             pEthCfg[nIndex].MACAddrOffSet = atoi(acPSMValue);
         }
         /*TODO:
	  *Need to be Removed Path From PSM Once RBUS Support Available in VlanManager and WanManager.
	  */
         snprintf( acPSMQuery, sizeof( acPSMQuery ), PSM_ETHLINK_PATH,nIndex + 1 );
         ret = DmlEthGetPSMRecordValue( acPSMQuery, acPSMValue )   ;
         if ( ret == 0)
         {
              strcpy(pEthCfg[nIndex].Path, acPSMValue);
         }
    }
    pMyObject->EthLink = pEthCfg;

    return ANSC_STATUS_SUCCESS;
}
/**********************************************************************

    caller:     self

    prototype:

        ANSC_STATUS
        EthernetInitialize
            (
                ANSC_HANDLE                 hThisObject
            );

    description:

        This function initiate cosa ethernet object and return handle.

    argument:	ANSC_HANDLE                 hThisObject
            This handle is actually the pointer of this object
            itself.

    return:     operation status.

**********************************************************************/

ANSC_STATUS
EthernetInitialize
    (
        ANSC_HANDLE                 hThisObject
    )
{
    ANSC_STATUS                     returnStatus     = ANSC_STATUS_SUCCESS;
    PDATAMODEL_ETHERNET             pMyObject        = (PDATAMODEL_ETHERNET)hThisObject;

    /* Call Initiation */
    returnStatus = EthLink_Init(NULL, NULL);
    if ( returnStatus != ANSC_STATUS_SUCCESS )
    {
        CcspTraceError(("%s-%d: Failed to Init SysEvent \n", __FUNCTION__, __LINE__));
        return returnStatus;
    }

    returnStatus = EthLink_Initialize(pMyObject);
    if ( returnStatus != ANSC_STATUS_SUCCESS )
    {
        CcspTraceError(("%s-%d: Failed to Init EthLink \n", __FUNCTION__, __LINE__));
        return returnStatus;
    }

    return returnStatus;
}

/**********************************************************************

    caller:     self

    prototype:

        ANSC_STATUS
        EthernetRemove
            (
                ANSC_HANDLE                 hThisObject
            );

    description:

        This function initiate  cosa ethernet object and return handle.

    argument:   ANSC_HANDLE                 hThisObject
            This handle is actually the pointer of this object
            itself.

    return:     operation status.

**********************************************************************/

ANSC_STATUS
EthernetRemove
    (
        ANSC_HANDLE                 hThisObject
    )
{
    ANSC_STATUS                     returnStatus = ANSC_STATUS_SUCCESS;
    PDATAMODEL_ETHERNET             pMyObject    = (PDATAMODEL_ETHERNET)hThisObject;

    /* Remove self */
    AnscFreeMemory((ANSC_HANDLE)pMyObject);

    return returnStatus;
}

