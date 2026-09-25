/********************************** (C) COPYRIGHT *******************************
 * File Name          : hidmouseservice.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/10
 * Description        : 鼠标服务
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "CONFIG.h"
#include "hidmouseservice.h"
#include "hiddev.h"
#include "battservice.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */
// HID service
const uint8_t hidServUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(HID_SERV_UUID), HI_UINT16(HID_SERV_UUID)};

// HID Information characteristic
const uint8_t hidInfoUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(HID_INFORMATION_UUID), HI_UINT16(HID_INFORMATION_UUID)};

// HID Report Map characteristic
const uint8_t hidReportMapUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(REPORT_MAP_UUID), HI_UINT16(REPORT_MAP_UUID)};

// HID Control Point characteristic
const uint8_t hidControlPointUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(HID_CTRL_PT_UUID), HI_UINT16(HID_CTRL_PT_UUID)};

// HID Report characteristic
const uint8_t hidReportUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(REPORT_UUID), HI_UINT16(REPORT_UUID)};

// HID Protocol Mode characteristic
const uint8_t hidProtocolModeUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(PROTOCOL_MODE_UUID), HI_UINT16(PROTOCOL_MODE_UUID)};

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

// HID Information characteristic value (bcdHID 0x0111, RemoteWake | NormallyConnectable)
static const uint8_t hidInfo[HID_INFORMATION_LEN] = {
    LO_UINT16(0x0111), HI_UINT16(0x0111),  // bcdHID (USB HID version)
    0x00,                                  // bCountryCode
    (HID_FLAGS_REMOTE_WAKE | HID_FLAGS_NORMALLY_CONNECTABLE) // Flags
};

// HID Report Map characteristic value (mouse, 6 bytes, report ID 1)
static const uint8_t hidReportMap[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop) */
    0x09, 0x02,        /* Usage (Mouse) */
    0xA1, 0x01,        /* Collection (Application) */
    0x85, 0x01,        /*   Report ID (1) */
    0x09, 0x01,        /*   Usage (Pointer) */
    0xA1, 0x00,        /*   Collection (Physical) */
    0x05, 0x09,        /*     Usage Page (Button) */
    0x19, 0x01,        /*     Usage Minimum (Button 1) */
    0x29, 0x05,        /*     Usage Maximum (Button 5) */
    0x15, 0x00,        /*     Logical Minimum (0) */
    0x25, 0x01,        /*     Logical Maximum (1) */
    0x95, 0x05,        /*     Report Count (5) */
    0x75, 0x01,        /*     Report Size (1) */
    0x81, 0x02,        /*     Input (Data,Var,Abs)  -> 5 buttons */
    0x95, 0x01,        /*     Report Count (1) */
    0x75, 0x03,        /*     Report Size (3) */
    0x81, 0x03,        /*     Input (Const,Var,Abs) -> 3 pad bits, 1 byte total */
    0x05, 0x01,        /*     Usage Page (Generic Desktop) */
    0x09, 0x30,        /*     Usage (X) */
    0x09, 0x31,        /*     Usage (Y) */
    0x16, 0x00, 0x80,  /*     Logical Minimum (-32768) */
    0x26, 0xFF, 0x7F,  /*     Logical Maximum (32767) */
    0x75, 0x10,        /*     Report Size (16) */
    0x95, 0x02,        /*     Report Count (2) */
    0x81, 0x06,        /*     Input (Data,Var,Rel)  -> X,Y int16 */
    0x09, 0x38,        /*     Usage (Wheel) */
    0x15, 0x81,        /*     Logical Minimum (-127) */
    0x25, 0x7F,        /*     Logical Maximum (127) */
    0x75, 0x08,        /*     Report Size (8) */
    0x95, 0x01,        /*     Report Count (1) */
    0x81, 0x06,        /*     Input (Data,Var,Rel)  -> wheel int8 */
    0xC0,              /*   End Collection */
    0xC0               /* End Collection */
};

// HID report map length
uint16_t hidReportMapLen = sizeof(hidReportMap);

// HID report mapping table
static hidRptMap_t hidRptMap[HID_NUM_REPORTS];

/*********************************************************************
 * Profile Attributes - variables
 */

// HID Service attribute
static const gattAttrType_t hidService = {ATT_BT_UUID_SIZE, hidServUUID};

// Include attribute (Battery service)
static uint16_t include = GATT_INVALID_HANDLE;

// HID Information characteristic
static uint8_t hidInfoProps = GATT_PROP_READ;

// HID Report Map characteristic
static uint8_t hidReportMapProps = GATT_PROP_READ;

// HID External Report Reference Descriptor
static uint8_t hidExtReportRefDesc[ATT_BT_UUID_SIZE] =
    {LO_UINT16(BATT_LEVEL_UUID), HI_UINT16(BATT_LEVEL_UUID)};

// HID Control Point characteristic
static uint8_t hidControlPointProps = GATT_PROP_WRITE_NO_RSP;
static uint8_t hidControlPoint;

// HID Protocol Mode characteristic
static uint8_t hidProtocolModeProps = GATT_PROP_READ | GATT_PROP_WRITE_NO_RSP;
uint8_t        hidProtocolMode = HID_PROTOCOL_MODE_REPORT;

// HID Report characteristic, Mouse input
static uint8_t       hidReportMouseInProps = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8_t       hidReportMouseIn;
static gattCharCfg_t hidReportMouseInClientCharCfg[GATT_MAX_NUM_CONN];

// HID Report Reference characteristic descriptor, mouse input
static uint8_t hidReportRefMouseIn[HID_REPORT_REF_LEN] =
    {HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT};

// Feature Report
static uint8_t hidReportFeatureProps = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8_t hidReportFeature;

// HID Report Reference characteristic descriptor, Feature
static uint8_t hidReportRefFeature[HID_REPORT_REF_LEN] =
    {HID_RPT_ID_FEATURE, HID_REPORT_TYPE_FEATURE};

/*********************************************************************
 * Profile Attributes - Table
 */

static gattAttribute_t hidAttrTbl[] = {
    // HID Service
    {
        {ATT_BT_UUID_SIZE, primaryServiceUUID}, /* type */
        GATT_PERMIT_READ,                       /* permissions */
        0,                                      /* handle */
        (uint8_t *)&hidService                  /* pValue */
    },

    // Included service (battery)
    {
        {ATT_BT_UUID_SIZE, includeUUID},
        GATT_PERMIT_READ,
        0,
        (uint8_t *)&include},

    // HID Information characteristic declaration
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &hidInfoProps},

    // HID Information characteristic
    {
        {ATT_BT_UUID_SIZE, hidInfoUUID},
        GATT_PERMIT_ENCRYPT_READ,
        0,
        (uint8_t *)hidInfo},

    // HID Control Point characteristic declaration
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &hidControlPointProps},

    // HID Control Point characteristic
    {
        {ATT_BT_UUID_SIZE, hidControlPointUUID},
        GATT_PERMIT_ENCRYPT_WRITE,
        0,
        &hidControlPoint},

    // HID Protocol Mode characteristic declaration
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &hidProtocolModeProps},

    // HID Protocol Mode characteristic
    {
        {ATT_BT_UUID_SIZE, hidProtocolModeUUID},
        GATT_PERMIT_ENCRYPT_READ | GATT_PERMIT_ENCRYPT_WRITE,
        0,
        &hidProtocolMode},

    // HID Report Map characteristic declaration
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &hidReportMapProps},

    // HID Report Map characteristic
    {
        {ATT_BT_UUID_SIZE, hidReportMapUUID},
        GATT_PERMIT_ENCRYPT_READ,
        0,
        (uint8_t *)hidReportMap},

    // HID External Report Reference Descriptor
    {
        {ATT_BT_UUID_SIZE, extReportRefUUID},
        GATT_PERMIT_READ,
        0,
        hidExtReportRefDesc

    },

    // HID Report characteristic, mouse input declaration
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &hidReportMouseInProps},

    // HID Report characteristic, mouse input
    {
        {ATT_BT_UUID_SIZE, hidReportUUID},
        GATT_PERMIT_ENCRYPT_READ,
        0,
        &hidReportMouseIn},

    // HID Report characteristic client characteristic configuration
    {
        {ATT_BT_UUID_SIZE, clientCharCfgUUID},
        GATT_PERMIT_READ | GATT_PERMIT_ENCRYPT_WRITE,
        0,
        (uint8_t *)&hidReportMouseInClientCharCfg},

    // HID Report Reference characteristic descriptor, mouse input
    {
        {ATT_BT_UUID_SIZE, reportRefUUID},
        GATT_PERMIT_READ,
        0,
        hidReportRefMouseIn},

    // Feature Report declaration
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &hidReportFeatureProps},

    // Feature Report
    {
        {ATT_BT_UUID_SIZE, hidReportUUID},
        GATT_PERMIT_ENCRYPT_READ | GATT_PERMIT_ENCRYPT_WRITE,
        0,
        &hidReportFeature},

    // HID Report Reference characteristic descriptor, feature
    {
        {ATT_BT_UUID_SIZE, reportRefUUID},
        GATT_PERMIT_READ,
        0,
        hidReportRefFeature},
};

// Attribute index enumeration-- these indexes match array elements above
enum
{
    HID_SERVICE_IDX,              // HID Service
    HID_INCLUDED_SERVICE_IDX,     // Included Service
    HID_INFO_DECL_IDX,            // HID Information characteristic declaration
    HID_INFO_IDX,                 // HID Information characteristic
    HID_CONTROL_POINT_DECL_IDX,   // HID Control Point characteristic declaration
    HID_CONTROL_POINT_IDX,        // HID Control Point characteristic
    HID_PROTOCOL_MODE_DECL_IDX,   // HID Protocol Mode characteristic declaration
    HID_PROTOCOL_MODE_IDX,        // HID Protocol Mode characteristic
    HID_REPORT_MAP_DECL_IDX,      // HID Report Map characteristic declaration
    HID_REPORT_MAP_IDX,           // HID Report Map characteristic
    HID_EXT_REPORT_REF_DESC_IDX,  // HID External Report Reference Descriptor
    HID_REPORT_MOUSE_IN_DECL_IDX, // HID Report characteristic, mouse input declaration
    HID_REPORT_MOUSE_IN_IDX,      // HID Report characteristic, mouse input
    HID_REPORT_MOUSE_IN_CCCD_IDX, // HID Report characteristic client characteristic configuration
    HID_REPORT_REF_MOUSE_IN_IDX,  // HID Report Reference characteristic descriptor, mouse input
    HID_FEATURE_DECL_IDX,         // Feature Report declaration
    HID_FEATURE_IDX,              // Feature Report
    HID_REPORT_REF_FEATURE_IDX    // HID Report Reference characteristic descriptor, feature
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/*********************************************************************
 * PROFILE CALLBACKS
 */

// Service Callbacks
gattServiceCBs_t hidCBs = {
    HidDev_ReadAttrCB,  // Read callback function pointer
    HidDev_WriteAttrCB, // Write callback function pointer
    NULL                // Authorization callback function pointer
};

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      Hid_AddService
 *
 * @brief   Initializes the HID Service by registering
 *          GATT attributes with the GATT server.
 *
 * @return  Success or Failure
 */
bStatus_t Hid_AddService(void)
{
    uint8_t status = SUCCESS;

    // Initialize Client Characteristic Configuration attributes
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, hidReportMouseInClientCharCfg);

    // Register GATT attribute list and CBs with GATT Server App
    status = GATTServApp_RegisterService(hidAttrTbl, GATT_NUM_ATTRS(hidAttrTbl), GATT_MAX_ENCRYPT_KEY_SIZE, &hidCBs);

    // Set up included service
    Batt_GetParameter(BATT_PARAM_SERVICE_HANDLE,
                      &GATT_INCLUDED_HANDLE(hidAttrTbl, HID_INCLUDED_SERVICE_IDX));

    // Construct map of reports to characteristic handles
    // Each report is uniquely identified via its ID and type

    // Mouse input report
    hidRptMap[0].id = hidReportRefMouseIn[0];
    hidRptMap[0].type = hidReportRefMouseIn[1];
    hidRptMap[0].handle = hidAttrTbl[HID_REPORT_MOUSE_IN_IDX].handle;
    hidRptMap[0].cccdHandle = hidAttrTbl[HID_REPORT_MOUSE_IN_CCCD_IDX].handle;
    hidRptMap[0].mode = HID_PROTOCOL_MODE_REPORT;

    // Feature report
    hidRptMap[1].id = hidReportRefFeature[0];
    hidRptMap[1].type = hidReportRefFeature[1];
    hidRptMap[1].handle = hidAttrTbl[HID_FEATURE_IDX].handle;
    hidRptMap[1].cccdHandle = 0;
    hidRptMap[1].mode = HID_PROTOCOL_MODE_REPORT;

    // Battery level input report
    Batt_GetParameter(BATT_PARAM_BATT_LEVEL_IN_REPORT, &(hidRptMap[2]));

    // Setup report ID map
    HidDev_RegisterReports(HID_NUM_REPORTS, hidRptMap);

    return (status);
}

/*********************************************************************
 * @fn      Hid_SetParameter
 *
 * @brief   Set a HID Kbd parameter.
 *
 * @param   id - HID report ID.
 * @param   type - HID report type.
 * @param   uuid - attribute uuid.
 * @param   len - length of data to right.
 * @param   pValue - pointer to data to write.  This is dependent on
 *          the input parameters and WILL be cast to the appropriate
 *          data type (example: data type of uint16_t will be cast to
 *          uint16_t pointer).
 *
 * @return  GATT status code.
 */
uint8_t Hid_SetParameter(uint8_t id, uint8_t type, uint16_t uuid, uint8_t len, void *pValue)
{
    bStatus_t ret = SUCCESS;

    switch(uuid)
    {
        case REPORT_UUID:
            if(type == HID_REPORT_TYPE_FEATURE)
            {
                if(len == 1)
                {
                    hidReportFeature = *((uint8_t *)pValue);
                }
                else
                {
                    ret = ATT_ERR_INVALID_VALUE_SIZE;
                }
            }
            else
            {
                ret = ATT_ERR_ATTR_NOT_FOUND;
            }
            break;

        default:
            // ignore the request
            break;
    }

    return (ret);
}

/*********************************************************************
 * @fn      Hid_GetParameter
 *
 * @brief   Get a HID Kbd parameter.
 *
 * @param   id - HID report ID.
 * @param   type - HID report type.
 * @param   uuid - attribute uuid.
 * @param   pLen - length of data to be read
 * @param   pValue - pointer to data to get.  This is dependent on
 *          the input parameters and WILL be cast to the appropriate
 *          data type (example: data type of uint16_t will be cast to
 *          uint16_t pointer).
 *
 * @return  GATT status code.
 */
uint8_t Hid_GetParameter(uint8_t id, uint8_t type, uint16_t uuid, uint16_t *pLen, void *pValue)
{
    switch(uuid)
    {
        case REPORT_UUID:
            if(type == HID_REPORT_TYPE_FEATURE)
            {
                *((uint8_t *)pValue) = hidReportFeature;
                *pLen = 1;
            }
            else
            {
                *pLen = 0;
            }
            break;

        default:
            *pLen = 0;
            break;
    }

    return (SUCCESS);
}

/*********************************************************************
*********************************************************************/
