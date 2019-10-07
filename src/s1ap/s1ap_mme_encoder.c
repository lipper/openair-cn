/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the Apache License, Version 2.0  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */


/*! \file s1ap_mme_encoder.c
   \brief s1ap encode procedures for MME
   \author Sebastien ROUX <sebastien.roux@eurecom.fr>
   \date 2012, 2019
*/
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>

#include "bstrlib.h"

#include "intertask_interface.h"
#include "mme_api.h"
#include "s1ap_common.h"
#include "s1ap_mme_encoder.h"
#include "assertions.h"
#include "log.h"

static inline int                       s1ap_mme_encode_initiating (
  S1AP_S1AP_PDU_t * pdu,
  uint8_t ** buffer,
  uint32_t * length);
static inline int                       s1ap_mme_encode_successfull_outcome (
  S1AP_S1AP_PDU_t * pdu,
  uint8_t ** buffer,
  uint32_t * len);
static inline int                       s1ap_mme_encode_unsuccessfull_outcome (
  S1AP_S1AP_PDU_t * pdu,
  uint8_t ** buffer,
  uint32_t * len);


//------------------------------------------------------------------------------
int
s1ap_mme_encode_pdu (S1AP_S1AP_PDU_t *pdu, uint8_t **buffer, uint32_t *length)
{
  int ret = -1;
  DevAssert (pdu != NULL);
  DevAssert (buffer != NULL);
  DevAssert (length != NULL);


  switch (pdu->present) {
  case S1AP_S1AP_PDU_PR_initiatingMessage:
    ret = s1ap_mme_encode_initiating (pdu, buffer, length);
    break;

  case S1AP_S1AP_PDU_PR_successfulOutcome:
    ret = s1ap_mme_encode_successfull_outcome (pdu, buffer, length);
    break;

  case S1AP_S1AP_PDU_PR_unsuccessfulOutcome:
    ret = s1ap_mme_encode_unsuccessfull_outcome (pdu, buffer, length);
    break;

  default:
    OAILOG_NOTICE (LOG_S1AP, "Unknown message outcome (%d) or not implemented", (int)pdu->present);
    break;
  }
  //ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_S1AP_S1AP_PDU, pdu);
  return ret;
}



//------------------------------------------------------------------------------
static inline int
s1ap_mme_encode_initiating (
  S1AP_S1AP_PDU_t *pdu,
  uint8_t ** buffer,
  uint32_t * length)
{
  asn_encode_to_new_buffer_result_t res = { NULL, {0, NULL, NULL} };
  DevAssert(pdu != NULL);

  switch(pdu->choice.initiatingMessage.procedureCode) {
  case S1AP_ProcedureCode_id_downlinkNASTransport:
  case S1AP_ProcedureCode_id_InitialContextSetup:
  case S1AP_ProcedureCode_id_UEContextRelease:
  case S1AP_ProcedureCode_id_E_RABSetup:
  case S1AP_ProcedureCode_id_E_RABModify:
  case S1AP_ProcedureCode_id_E_RABRelease:
  case S1AP_ProcedureCode_id_HandoverResourceAllocation:
  case S1AP_ProcedureCode_id_MMEStatusTransfer:
  case S1AP_ProcedureCode_id_Paging:
    break;

  default:
    OAILOG_NOTICE (LOG_S1AP, "Unknown procedure ID (%d) for initiating message_p\n", (int)pdu->choice.initiatingMessage.procedureCode);
    *buffer = NULL;
    *length = 0;
    return -1;
  }

  memset(&res, 0, sizeof(res));
  res = asn_encode_to_new_buffer(NULL, ATS_ALIGNED_CANONICAL_PER, &asn_DEF_S1AP_S1AP_PDU, pdu);
  *buffer = res.buffer;
  *length = res.result.encoded;
  return 0;
}

//------------------------------------------------------------------------------
static inline int
s1ap_mme_encode_successfull_outcome (
  S1AP_S1AP_PDU_t * pdu,
  uint8_t ** buffer,
  uint32_t * length)
{
  asn_encode_to_new_buffer_result_t res = { NULL, {0, NULL, NULL} };
  DevAssert(pdu != NULL);

  switch (pdu->choice.successfulOutcome.procedureCode) {
  case S1AP_ProcedureCode_id_S1Setup:
  case S1AP_ProcedureCode_id_PathSwitchRequest:
  case S1AP_ProcedureCode_id_HandoverPreparation:
  case S1AP_ProcedureCode_id_HandoverCancel:
      break;

  default:
    OAILOG_DEBUG (LOG_S1AP, "Unknown procedure ID (%d) for successfull outcome message\n", (int)pdu->choice.successfulOutcome.procedureCode);
    *buffer = NULL;
    *length = 0;
    return -1;
  }

  memset(&res, 0, sizeof(res));
  res = asn_encode_to_new_buffer(NULL, ATS_ALIGNED_CANONICAL_PER, &asn_DEF_S1AP_S1AP_PDU, pdu);
  *buffer = res.buffer;
  *length = res.result.encoded;
  return 0;
}

//------------------------------------------------------------------------------
static inline int
s1ap_mme_encode_unsuccessfull_outcome (
  S1AP_S1AP_PDU_t * pdu,
  uint8_t ** buffer,
  uint32_t * length)
{

  asn_encode_to_new_buffer_result_t res = { NULL, {0, NULL, NULL} };
  DevAssert(pdu != NULL);

  switch(pdu->choice.unsuccessfulOutcome.procedureCode) {
  case S1AP_ProcedureCode_id_S1Setup:
  case S1AP_ProcedureCode_id_PathSwitchRequest:
  case S1AP_ProcedureCode_id_HandoverPreparation:
    break;

  default:
    OAILOG_DEBUG (LOG_S1AP, "Unknown procedure ID (%d) for unsuccessfull outcome message\n", (int)pdu->choice.unsuccessfulOutcome.procedureCode);
    *buffer = NULL;
    *length = 0;
    return -1;
  }

  memset(&res, 0, sizeof(res));
  res = asn_encode_to_new_buffer(NULL, ATS_ALIGNED_CANONICAL_PER, &asn_DEF_S1AP_S1AP_PDU, pdu);
  *buffer = res.buffer;
  *length = res.result.encoded;
  return 0;
}
