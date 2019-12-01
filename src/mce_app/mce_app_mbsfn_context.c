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

/*! \file mce_app_mbsfn_context.c
  \brief
  \author Dincer Beken
  \company Blackned GmbH
  \email: dbeken@blackned.de
*/

#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include <inttypes.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "gcc_diag.h"
#include "dynamic_memory_check.h"
#include "assertions.h"
#include "log.h"
#include "msc.h"
#include "3gpp_requirements_36.413.h"
#include "common_types.h"
#include "conversions.h"
#include "intertask_interface.h"
#include "dlsch_tbs_full.h"
#include "mme_config.h"
#include "enum_string.h"
#include "timer.h"
#include "mce_app_mbms_service_context.h"
#include "mce_app_defs.h"
#include "mce_app_itti_messaging.h"
#include "mme_app_procedures.h"
#include "common_defs.h"

// todo: think about locking the MCE_APP context or EMM context, which one to lock, why to lock at all? lock seperately?
////------------------------------------------------------------------------------
//int lock_ue_contexts(mbms_service_t * const ue_context) {
//  int rc = RETURNerror;
//  if (ue_context) {
//    struct timeval start_time;
//    gettimeofday(&start_time, NULL);
//    struct timespec wait = {0}; // timed is useful for debug
//    wait.tv_sec=start_time.tv_sec + 5;
//    wait.tv_nsec=start_time.tv_usec*1000;
//    rc = pthread_mutex_timedlock(&ue_context->recmutex, &wait);
//    if (rc) {
//      OAILOG_ERROR (LOG_MCE_APP, "Cannot lock UE context mutex, err=%s\n", strerror(rc));
//#if ASSERT_MUTEX
//      struct timeval end_time;
//      gettimeofday(&end_time, NULL);
//      AssertFatal(!rc, "Cannot lock UE context mutex, err=%s took %ld seconds \n", strerror(rc), end_time.tv_sec - start_time.tv_sec);
//#endif
//    }
//#if DEBUG_MUTEX
//    OAILOG_TRACE (LOG_MCE_APP, "UE context mutex locked, count %d lock %d\n",
//        ue_context->recmutex.__data.__count, ue_context->recmutex.__data.__lock);
//#endif
//  }
//  return rc;
//}
////------------------------------------------------------------------------------
//int unlock_ue_contexts(mbms_service_t * const ue_context) {
//  int rc = RETURNerror;
//  if (ue_context) {
//    rc = pthread_mutex_unlock(&ue_context->recmutex);
//    if (rc) {
//      OAILOG_ERROR (LOG_MCE_APP, "Cannot unlock UE context mutex, err=%s\n", strerror(rc));
//    }
//#if DEBUG_MUTEX
//    OAILOG_TRACE (LOG_MCE_APP, "UE context mutex unlocked, count %d lock %d\n",
//        ue_context->recmutex.__data.__count, ue_context->recmutex.__data.__lock);
//#endif
//  }
//  return rc;
//}
//
/****************************************************************************/
/*******************  L O C A L    D E F I N I T I O N S  *******************/
/****************************************************************************/

//------------------------------------------------------------------------------
static
int mce_app_get_mch_mcs(mbsfn_area_context_t * const mbsfn_area_context, const qci_e qci);


//------------------------------------------------------------------------------
static
void mce_app_calculate_csa_common_pattern(struct csa_patterns_s * csa_patterns_p, mchs_t * mchs, uint8_t num_mbsfn_idx, uint8_t total_mbsfn_areas_tbs);

//------------------------------------------------------------------------------
static
void mce_app_reuse_csa_pattern(struct csa_patterns_s * csa_patterns_mbsfn_p, mchs_t * mchs, const struct csa_patterns_s * const csa_patterns_alloced);

//------------------------------------------------------------------------------
static
int mce_app_csa_pattern_(struct csa_patterns_s * new_csa_patterns, int total_subframes_per_csa_period_necessary);

//------------------------------------------------------------------------------
static
int mce_app_calculate_overall_csa_pattern(struct csa_patterns_s * const csa_patterns_p, mchs_t * const mchs, const uint8_t num_mbsfn_idx,
		const uint8_t total_mbsfn_areas_to_be_scheduled, struct csa_patterns_s * const union_csa_patterns_allocated_p);

//------------------------------------------------------------------------------
static
bool mce_mbsfn_area_compare_by_mbms_sai (__attribute__((unused)) const hash_key_t keyP,
		void * const elementP,
		void * parameterP, void **resultP);

//------------------------------------------------------------------------------
static
int mce_app_log_method_single_rf_csa_pattern(struct csa_pattern_s * new_csa_patterns, int num_radio_frames, int num_total_csa_pattern_offset);

//------------------------------------------------------------------------------
static
void mce_app_calculate_mbsfn_mchs(struct mbsfn_area_context_s * const mbsfn_area_context, mbms_services_t * const mbms_services_active, mchs_t *const mchs);

//------------------------------------------------------------------------------
static
bool mce_app_update_mbsfn_area(const mbsfn_area_id_t mbsfn_area_id, const mbms_service_area_id_t mbms_service_area_id, const uint32_t m2_enb_id, const sctp_assoc_id_t assoc_id);

//------------------------------------------------------------------------------
static int
mce_insert_mbsfn_area_context (
  mce_mbsfn_area_contexts_t * const mce_mbsfn_area_contexts_p, const struct mbsfn_area_context_s *const mbsfn_area_context);

//------------------------------------------------------------------------------
static
void clear_mbsfn_area(mbsfn_area_context_t * const mbsfn_area_context);

//------------------------------------------------------------------------------
static
bool mce_app_create_mbsfn_area(const mbsfn_area_id_t mbsfn_area_id, const mbms_service_area_id_t mbms_service_area_id, const uint32_t m2_enb_id, const sctp_assoc_id_t assoc_id);

//------------------------------------------------------------------------------
static
void mce_update_mbsfn_area(struct mbsfn_areas_s * const mbsfn_areas, const mbsfn_area_id_t mbsfn_area_id, const mbms_service_area_id_t mbms_service_area_id, uint32_t m2_enb_id, const sctp_assoc_id_t assoc_id);

//------------------------------------------------------------------------------
struct mbsfn_area_context_s                           *
mce_mbsfn_area_exists_mbsfn_area_id(
  mce_mbsfn_area_contexts_t * const mce_mbsfn_areas_p, const mbsfn_area_id_t mbsfn_area_id)
{
  struct mbsfn_area_context_s                    *mbsfn_area_context = NULL;
  hashtable_ts_get (mce_mbsfn_areas_p->mbsfn_area_id_mbsfn_area_htbl, (const hash_key_t)mbsfn_area_id, (void **)&mbsfn_area_context);
  return mbsfn_area_context;
}

//------------------------------------------------------------------------------
struct mbsfn_area_context_s                           *
mce_mbsfn_area_exists_mbms_service_area_id(
		mce_mbsfn_area_contexts_t * const mce_mbsfn_areas_p, const mbms_service_area_id_t mbms_service_area_id)
{
  hashtable_rc_t              h_rc 					= HASH_TABLE_OK;
	mbsfn_area_id_t							mbsfn_area_id = INVALID_MBSFN_AREA_ID;

  h_rc = hashtable_uint64_ts_get (mce_mbsfn_areas_p->mbms_sai_mbsfn_area_ctx_htbl, (const hash_key_t)mbms_service_area_id, &mbsfn_area_id);
  if (HASH_TABLE_OK == h_rc) {
    return mce_mbsfn_area_exists_mbsfn_area_id(mce_mbsfn_areas_p, (mbsfn_area_id_t) mbsfn_area_id);
  }
  return NULL;
}

//------------------------------------------------------------------------------
struct mbsfn_area_context_s                      *
mbsfn_area_mbms_service_id_in_list (const mce_mbsfn_area_contexts_t * const mce_mbsfn_area_contexts_p,
  const mbms_service_area_id_t mbms_sai)
{
  mbsfn_area_context_t                   *mbsfn_area_context_ref = NULL;
  mbms_service_area_id_t                 *mbms_sai_p = (mbms_service_area_id_t*)&mbms_sai;
  hashtable_ts_apply_callback_on_elements(mce_mbsfn_area_contexts_p->mbsfn_area_id_mbsfn_area_htbl, mce_mbsfn_area_compare_by_mbms_sai, (void *)mbms_sai_p, (void**)&mbsfn_area_context_ref);
  return mbsfn_area_context_ref;
}

//------------------------------------------------------------------------------
bool mce_app_check_mbsfn_mcch_modif (const hash_key_t keyP,
               void * const mbsfn_area_context_ref,
               void * parameterP,
               void **resultP)
{
	long 											  mcch_repeat_rf_abs  		= *((long*)parameterP);
	mbsfn_area_id_t			 	      mbsfn_area_id 					= (mbsfn_area_id_t)keyP;
	mbsfn_areas_t	 	      		 *mbsfn_areas							= (mbsfn_areas_t*)*resultP;

	/*** Get the MBSFN areas to be modified. */
	mbsfn_area_context_t * mbsfn_area_context = (mbsfn_area_context_t*)mbsfn_area_context_ref;
	/** Assert that the bitmap is not full. Capacity should have been checked before. */
//	DevAssert(mbsfn_areas->mbsfn_csa_offsets != 0xFF);

	/** MBMS service may have started before. And should prevail in the given MCCH modification period. */
	if(mcch_repeat_rf_abs % mbsfn_area_context->privates.fields.mbsfn_area.mcch_modif_period_rf){
		OAILOG_DEBUG(LOG_MCE_APP, "MBSFN Area " MBSFN_AREA_ID_FMT " MCCH modification period not reached yet for "
				"MCCH repetition RF (%d).\n", mbsfn_area_id, mcch_repeat_rf_abs);
		return false;
	}
	OAILOG_INFO(LOG_MCE_APP, "MBSFN Area " MBSFN_AREA_ID_FMT " MCCH modification period REACHED for "
					"MCCH repetition RF (%d).\n", mbsfn_area_id, mcch_repeat_rf_abs);
	// 8 CSA patterns per MBSFN area are allowed, currently just once considered!!
	/**
	 * To calculate the CSA[7], need too MBSFN areas and # of the current MBSFN area.
	 */
	long mcch_modif_period_abs[2] = {
			mcch_repeat_rf_abs / mbsfn_area_context->privates.fields.mbsfn_area.mcch_modif_period_rf,
			mcch_repeat_rf_abs / mbsfn_area_context->privates.fields.mbsfn_area.mcch_modif_period_rf
	};

	/**
	 * MBSFN Areas overall object will be returned in the method below.
	 */
	if(!mce_app_check_mbsfn_resources(keyP, mbsfn_area_context_ref, mcch_modif_period_abs,
			&mbsfn_areas)){
		OAILOG_DEBUG(LOG_MCE_APP, "MBSFN Area " MBSFN_AREA_ID_FMT " MCCH modification period REACHED for "
				"MCCH repetition RF (%d). No CSA modification detected. \n", mbsfn_area_id, mcch_repeat_rf_abs);
		return false;
	}

// todo: check for changes in the cSA pattern of the MBSFN context here.
//	/** Check for changes in the CSA pattern. */
//	bool change = memcmp((void*)&mbsfn_area_context->privates.fields.mbsfn_area.csa_patterns, (void*)&new_csa_patterns, sizeof(struct csa_patterns_s)) != 0;
//
//	pthread_rwlock_trywrlock(&mce_app_desc.rw_lock);	// todo: lock mce_desc
//	memcpy((void*)&mbsfn_area_context->privates.fields.mbsfn_area.csa_patterns, (void*)&new_csa_patterns, sizeof(struct csa_patterns_s));
//	// todo: update other fields..
//	pthread_rwlock_unlock(&mce_app_desc.rw_lock);

	// todo: assume that MCCH modification timer increments even when no update occurs.
	OAILOG_DEBUG(LOG_MCE_APP, "MBSFN Area " MBSFN_AREA_ID_FMT " MCCH modification period REACHED for "
			"MCCH repetition RF (%d). CSA modification detected. Updating the scheduling. \n", mbsfn_area_id, mcch_repeat_rf_abs);
	memcpy((void*)&mbsfn_areas->mbsfn_area_cfg[mbsfn_areas->num_mbsfn_areas++].mbsfnArea, (void*)&mbsfn_area_context->privates.fields.mbsfn_area, sizeof(mbsfn_area_t));
	/**
	 * MBSFN area is to be modified (MBMS service was added, removed or modified).
	 * Iterate through the whole list.
	 */
	// todo: if the bitmap is full, we might return true..
	return false;
}

/**
 * Check the resources for the given period over all MBSFN areas.
 * We assume that the M2 eNBs share the band for multiple MBSFN areas, thus also the resources.
 */
//------------------------------------------------------------------------------
bool mce_app_check_mbsfn_resources (const hash_key_t keyP,
               void * const mbsfn_area_context_ref,
               void * parameterP,
               void **resultP)
{
	OAILOG_FUNC_IN(LOG_MCE_APP);

	/**
	 * Array of two MCCH modification periods start and end.
	 */
	long 											 *mcch_modif_periods_abs	= ((long*)parameterP);
	mbsfn_area_context_t       *mbsfn_area_context      = (mbsfn_area_context_t*)mbsfn_area_context_ref;
	mchs_t										  mchs										= {0};
	mbsfn_area_id_t			 	      mbsfn_area_id 					= (mbsfn_area_id_t)keyP;
	mbms_services_t 			 			mbms_services_active 		= {0},
														 *mbms_services_active_p 	= &mbms_services_active;

	/** CSA pattern allocation for the actual MBSFN area. May contain new patterns and reuse old patterns. */
	struct csa_patterns_s 		  new_csa_patterns 			  = {0};
	/** Contain the allocated MBSFN Areas. CSA Pattern Allocation for the current MBSFN area will be done based on the already received MBSFN areas. */
	mbsfn_areas_t							 *mbsfn_areas							= (mbsfn_areas_t*)*resultP;

	/** Check the total MBMS Resources across MBSFN areas for the MCCH modification period in question. */
	mme_config_read_lock(&mme_config);
	mbms_service_t * mbms_services_active_array[mme_config.mbms.max_mbms_services];
	memset(&mbms_services_active_array, 0, (sizeof(mbms_service_t*) * mme_config.mbms.max_mbms_services));
	mbms_services_active.mbms_service_array = mbms_services_active_array;
	mme_config_unlock(&mme_config);

	/**
	 * Get all MBMS services which are active in the given absolute MCCH period of the MBMS Service START!!
	 * Calculate the total CSA period duration, where we will check the capacity afterwards
	 * Contains the precise numbers of MBSFN subframes needed for data, including the ones, that will be scheduled for the last common CSA Pattern[7].
	 * The mbsfn_areas object, which contains the allocated resources for the other MBSFN areas, should be updated automatically.
	 */

	/** Get the active MBMS services for the MBSFN area. */
	long parametersP[3] = {mcch_modif_periods_abs[0], mcch_modif_periods_abs[1], mbsfn_area_id};
	hashtable_ts_apply_callback_on_elements((hash_table_ts_t * const)mce_app_desc.mce_mbms_service_contexts.mbms_service_index_mbms_service_htbl,
			mce_app_get_active_mbms_services, (void*)parametersP, (void**)&mbms_services_active_p);
	mce_app_calculate_mbsfn_mchs(mbsfn_area_context, &mbms_services_active, &mchs);

	struct csa_pattern_s *resultint_csa_pattern =NULL; // CALCULATE FROM THE MBSFN AREAS!
	/**
	 * In consideration of the total number of MBSFN with which we calculate CSA[7], where we reserve subframes for the MCCH,
	 * calculate the subframe for the MBSFN area.
	 */
	if(mce_app_calculate_overall_csa_pattern(&new_csa_patterns, &mchs, mbsfn_areas->num_mbsfn_areas,
			mbsfn_areas->num_mbsfn_areas_to_be_scheduled, resultint_csa_pattern) == RETURNerror) {
		OAILOG_ERROR(LOG_MCE_APP, "CSA pattern for MBSFN Area " MBSFN_AREA_ID_FMT" does exceed resources.\n", mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_area_id);
		OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNerror);
	}
	OAILOG_INFO(LOG_MCE_APP, "Successfully calculated CSA pattern for MBSFN Area " MBSFN_AREA_ID_FMT ". Checking resources. \n",
			mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_area_id);
	/**
	 * Update the MBSFN Areas with a new configuration.
	 * We wan't to make sure, not to modify the MBSFN areas object in the method mce_app_calculate_overall_csa_pattern,
	 * that's why we copy it.
	 */
	memcpy((void*)&mbsfn_areas->mbsfn_area_cfg[mbsfn_areas->num_mbsfn_areas].mbsfnArea.csa_patterns,
			(void*)&new_csa_patterns, sizeof(struct csa_patterns_s));
	mbsfn_areas->num_mbsfn_areas++;

	// TODO: UPDATE THE RESULTING CSA PATTERN union
	// TODO: IN THE OUTER FUNCTION UPDATE THE RESULTING PATTERN --> Make the union pattern from all the MBSFN areas..

	/**
	 * Set the radio frame offset of all the CSA patterns used so far using the MBSFN Areas item.
	 * Then also create a constant pointer to used CSA patterns.
	 * Last CSA pattern is universal, shared among different MBSFN areas and includes the MCCHs in each MCCH repetition period
	 * (other subframes of that CSA Patter Repetition period in the CSA Period are not used).
	 */
//	todo: csa_patterns_p->total_csa_pattern_offset = mbsfn_areas->total_csa_pattern_offset;
	/**
	 * Set the current offset of CSA patterns.
	 * todo: later optimize this..
	 */

	//	mbsfn_areas->total_csa_pattern_offset = new_csa_patterns.total_csa_pattern_offset;

	OAILOG_FUNC_RETURN(LOG_MCE_APP, false);
}

//------------------------------------------------------------------------------
static bool mce_mbsfn_area_reset_m2_enb_id (__attribute__((unused))const hash_key_t keyP,
               void * const mbsfn_area_ctx_ref,
               void * m2_enb_id_P,
               void __attribute__((unused)) **unused_resultP)
{
  const mbsfn_area_context_t * const mbsfn_area_ctx = (const mbsfn_area_context_t *)mbsfn_area_ctx_ref;
  if (mbsfn_area_ctx == NULL) {
    return false;
  }
  /**
   * Remove the key from the MBSFN Area context.
   * No separate counter is necessarry.*/
  hashtable_uint64_ts_remove(mbsfn_area_ctx->privates.m2_enb_id_hashmap, *((const hash_key_t*)m2_enb_id_P));
  return false;
}

////------------------------------------------------------------------------------
//static
//bool mce_mbsfn_area_reset_m2_enb_id (__attribute__((unused))const hash_key_t keyP,
//               void * const mbsfn_area_ref,
//               void * m2_end_idP,
//               void __attribute__((unused)) **unused_resultP)
//{
//  const mbsfn_area_context_t * mbsfn_area_ctx =  mbsfn_area_context_t * mbsfn_area_ref;
//  if (mbsfn_area_ctx == NULL) {
//    return false;
//  }
//  m2ap_dump_enb(enb_ref);
//  return false;
//}

//------------------------------------------------------------------------------
void mce_app_reset_m2_enb_registration(const uint32_t m2_enb_id, const sctp_assoc_id_t assoc_id) {
	OAILOG_FUNC_IN(LOG_MCE_APP);
	/**
	 * Apply a callback function on all registered MBSFN areas.
	 * Remove for each the M2 eNB and decrement the eNB count.
	 */
  hashtable_ts_apply_callback_on_elements(mce_app_desc.mce_mbsfn_area_contexts.mbsfn_area_id_mbsfn_area_htbl,
  		mce_mbsfn_area_reset_m2_enb_id, (void *)&m2_enb_id, NULL);

	OAILOG_FUNC_OUT(LOG_MCE_APP);
}

//------------------------------------------------------------------------------
int mce_app_get_local_mbsfn_areas(const mbms_service_area_t *mbms_service_areas, const uint32_t m2_enb_id, const sctp_assoc_id_t assoc_id, mbsfn_areas_t * const mbsfn_areas)
{
	OAILOG_FUNC_IN(LOG_MCE_APP);

	mbsfn_area_id_t				  mbsfn_area_id      				= INVALID_MBSFN_AREA_ID;
	mbsfn_area_context_t	 *mbsfn_area_context 				= NULL; 									/**< Context stored in the MCE_APP hashmap. */
  /** A single local MBMS service area defined for all MBSFN areas. */
  uint8_t			  					local_mbms_service_area 	= 0;

	/**
   * For each MBMS Service area, check if it is a valid local MBMS Service area.
   * Create a dedicated MBSFN area for it.
   */
	for(int num_mbms_area = 0; num_mbms_area < mbms_service_areas->num_service_area; num_mbms_area++) {
		mbsfn_area_id = INVALID_MBSFN_AREA_ID;
		/**
		 * Get from the MME config, if the MBMS Service Area is global or not.
		 */
	  mme_config_read_lock (&mme_config);
		if(!mme_config.mbms.mbms_local_service_area_types || !mme_config.mbms.mbms_local_service_areas){
		  mme_config_unlock(&mme_config);
		  OAILOG_WARNING(LOG_MCE_APP, "No local MBMS Service Areas are configured.\n");
			OAILOG_FUNC_RETURN(LOG_MCE_APP, 0);
		}
		/** Get the MBSFN Area ID, should be a deterministic function, depending only on the MBMS Service Area Id. */
		if(mbms_service_areas->serviceArea[num_mbms_area] <= 0){
	  	/** Skip the MBMS Area. No modifications on the MBSFN area*/
			mme_config_unlock(&mme_config);
			continue;
	  }
		if(mbms_service_areas->serviceArea[num_mbms_area] <= mme_config.mbms.mbms_global_service_area_types){
			/** Global MBMS Service Areas will be checked later after Local MBMS Service Areas. */
		  mme_config_unlock(&mme_config);
		  continue;
		}
		/**
		 * Check if it is a valid local MBMS service area.
		 */
	  int val = (mbms_service_areas->serviceArea[num_mbms_area] - (mme_config.mbms.mbms_global_service_area_types + 1));
	  int local_area_index = val / mme_config.mbms.mbms_local_service_area_types; /**< 0..  mme_config.mbms.mbms_local_service_areas - 1. */
	  int local_area_type = val % mme_config.mbms.mbms_local_service_area_types;  /**< 0..  mme_config.mbms.mbms_local_service_area_types - 1. */
	  if(local_area_index < mme_config.mbms.mbms_local_service_areas) {
	  	/**
	  	 * Check that no other local service area is set for the eNB.
	  	 */
	  	if(local_mbms_service_area) {
	  		if(local_mbms_service_area != local_area_type +1){
	  			mme_config_unlock(&mme_config);
	  			OAILOG_ERROR(LOG_MCE_APP, "A local MBMS area (%d) is already set for the eNB. Skipping new local MBMS area (%d). \n",
	  					local_mbms_service_area, local_area_type +1);
	  			continue;
	  		}
	  		/** Continuing with the same local MBMS service area. */
	  	} else {
	  		local_mbms_service_area = local_area_type + 1;
	  		OAILOG_INFO(LOG_MCE_APP, "Setting local MBMS service area (%d) for the eNB. \n", local_mbms_service_area);
	  	}
	  	OAILOG_INFO(LOG_MCE_APP, "Found a valid local MBMS Service Area ID " MBMS_SERVICE_AREA_ID_FMT ". \n", mbms_service_areas->serviceArea[num_mbms_area]);
	  	/** Return the MBSFN Area. */
	  	if(mme_config.mbms.mbms_global_mbsfn_area_per_local_group){
	  		mbsfn_area_id = mme_config.mbms.mbms_global_service_area_types
	  				+ local_area_type * (mme_config.mbms.mbms_local_service_area_types + mme_config.mbms.mbms_global_service_area_types) + (local_area_type +1);
	  	} else {
	  		/** Return the MBMS service area as the MBSFN area. We use the same identifiers. */
	  		mbsfn_area_id = mbms_service_areas->serviceArea[num_mbms_area];
	  	}
	  	/**
	  	 * Check if the MBSFN service area is already set for this particular request.
	  	 * In both cases, we should have unique MBSFN areas for the MBMS service areas.
	  	 */
			mme_config_unlock(&mme_config);
			mce_update_mbsfn_area(mbsfn_areas, mbsfn_area_id, mbms_service_areas->serviceArea[num_mbms_area], m2_enb_id, assoc_id);
	  } else {
	  	OAILOG_ERROR(LOG_MCE_APP, "MBMS Service Area ID " MBMS_SERVICE_AREA_ID_FMT " is not a valid local MBMS service area. Skipping. \n", mbms_service_areas->serviceArea[num_mbms_area]);
	  }
	}
	/** Get the local service area (geographical). */
	OAILOG_FUNC_RETURN(LOG_MCE_APP, local_mbms_service_area);
}

/**
 * Function to handle global MBSFN areas.
 * This assumes, that the MBSFN areas object already did undergone local MBMS service area operations.
 */
//------------------------------------------------------------------------------
void mce_app_get_global_mbsfn_areas(const mbms_service_area_t *mbms_service_areas, const uint32_t m2_enb_id,
		const sctp_assoc_id_t assoc_id, mbsfn_areas_t * const mbsfn_areas, int local_mbms_service_area)
{
	OAILOG_FUNC_IN(LOG_MCE_APP);
	mbsfn_area_id_t					mbsfn_area_id			 = INVALID_MBSFN_AREA_ID;
	/** Iterate through the whole list again. */
	for(int num_mbms_area = 0; num_mbms_area < mbms_service_areas->num_service_area; num_mbms_area++) {
		mbsfn_area_id = INVALID_MBSFN_AREA_ID;
		/**
		 * Get from the MME config, if the MBMS Service Area is global or not.
		 */
	  mme_config_read_lock (&mme_config);
		if(!mme_config.mbms.mbms_global_service_area_types){
		  mme_config_unlock(&mme_config);
		  OAILOG_ERROR(LOG_MCE_APP, "No global MBMS Service Areas are configured.\n");
			OAILOG_FUNC_OUT(LOG_MCE_APP);
		}
		/** Get the MBSFN Area ID, should be a deterministic function, depending only on the MBMS Service Area Id. */
		if(mbms_service_areas->serviceArea[num_mbms_area] <= 0){
	  	/** Skip the MBMS Area. No modifications on the MBSFN area*/
		  mme_config_unlock(&mme_config);
			continue;
	  }
		/** Check if it is a local MBMS Area. */
		if(mbms_service_areas->serviceArea[num_mbms_area] > mme_config.mbms.mbms_global_service_area_types){
			/** Skip the local area. */
		  mme_config_unlock(&mme_config);
		  continue;
		}
		/**
		 * Found a global area. If the eNB has a local area and if we use local MBMS service areas,
		 * we use a different MBSFN a
		 * MBMS Service areas would be set. */
		if(mme_config.mbms.mbms_global_mbsfn_area_per_local_group) {
			/**
			 * We check if the eNB is in a local MBMS area, so assign a group specific MBSFN Id.
			 * todo: later --> Resources will be partitioned accordingly.
			 */
			if(local_mbms_service_area) {
				OAILOG_INFO(LOG_MCE_APP, "Configuring local MBMS group specific global MBMS Service Area " MBMS_SERVICE_AREA_ID_FMT " for local group %d.\n",
						mbms_service_areas->serviceArea[num_mbms_area], local_mbms_service_area);
				mbsfn_area_id = mme_config.mbms.mbms_global_service_area_types +
						(local_mbms_service_area -1) * (mme_config.mbms.mbms_local_service_area_types + mme_config.mbms.mbms_global_service_area_types)
								+ mbms_service_areas->serviceArea[num_mbms_area];
				/** If the MBSFN area is already allocated, skip to the next one.
				 * Found a new local specific valid global MBMS group.
				 * Check if there exists an MBSFN group for that.
				 */
  		  mme_config_unlock(&mme_config);
  			mce_update_mbsfn_area(mbsfn_areas, mbsfn_area_id, mbms_service_areas->serviceArea[num_mbms_area], m2_enb_id, assoc_id);
  		  continue;
			}
		}
		/**
		 * Local specific global MBMS areas are not allowed or eNB does not belong to any local MBMS area.
		 * Return the MBSFN Id as the global MBMS Service Area.
		 */
		mbsfn_area_id = mbms_service_areas->serviceArea[num_mbms_area];
		/**
		 * If the MBSFN area is already allocated, skip to the next one.
		 * Found a new local specific valid global MBMS group.
		 * Check if there exists an MBSFN group for that.
		 */
		mce_update_mbsfn_area(mbsfn_areas, mbsfn_area_id, mbms_service_areas->serviceArea[num_mbms_area], m2_enb_id, assoc_id);
		mme_config_unlock(&mme_config);
	}
	/** Done checking all global MBMS service areas. */
	OAILOG_FUNC_OUT(LOG_MCE_APP);
}

//------------------------------------------------------------------------------
bool mce_app_get_active_mbms_services (const hash_key_t keyP,
               void * const mbms_service_ref,
               void * parameterP,
               void **resultP)
{
	/** Check active services, for start and end MCCH modification periods. */
	long 												 mcch_modif_period_abs_start 	= (((long*)parameterP)[0]);
	long 												 mcch_modif_period_abs_stop		= (((long*)parameterP)[1]);

	mbsfn_area_id_t							 mbsfn_area_id 				 				= (mbsfn_area_id_t)(((long*)parameterP)[1]);
	mbms_service_index_t 	       mbms_service_idx 		 				= (mbms_service_index_t)keyP;
	mbms_services_t	 	      		*mbms_services			 	 				= (mbms_services_t*)*resultP;
	mbms_service_t 					    *mbms_service 				 				= (mbms_service_t*)mbms_service_ref;

	if(mbsfn_area_id == INVALID_MBSFN_AREA_ID || mbms_service->privates.fields.mbsfn_area_id == mbsfn_area_id){
		/** MBMS service may have started before. And should prevail in the given MCCH modification period. */
		if(mbms_service->privates.fields.mbms_service_mcch_start_period <= mcch_modif_period_abs_stop
				&& mcch_modif_period_abs_start <= mbms_service->privates.fields.mbms_service_mcch_stop_period){
			/**
			 * Received an active MBMS service, whos start/stop intervals overlap with the given intervals.
			 * Add it to the service of active MBMS.
			 */
			mbms_services->mbms_service_array[mbms_services->num_mbms_service++] = mbms_service;
		}
	}
	/** Iterate through the whole list. */
	return false;
}

//------------------------------------------------------------------------------
int mce_app_mbms_arp_preempt(const mbms_services_t * mbms_services_active){
	OAILOG_FUNC_IN(LOG_MCE_APP);

	uint8_t 	low_arp_prio_level   		 = 0;
	int mbms_service_active_list_index = -1;

	/** Get all MBMS services, which area active in the given MCCH modification period. */
	if(!mbms_services_active->num_mbms_service){
  	OAILOG_FUNC_RETURN(LOG_MCE_APP, INVALID_MBMS_SERVICE_INDEX);
  }
	/** Remove the MBMS session with the lowest ARP prio level. */
	for(int num_ms = 0; num_ms < mbms_services_active->num_mbms_service; num_ms++){
		if(mbms_services_active->mbms_service_array[num_ms]) {
			// todo: check the once which are vulnerable
			if(mbms_services_active->mbms_service_array[num_ms]->privates.fields.mbms_bc.eps_bearer_context.bearer_level_qos.pvi) {
				if(low_arp_prio_level < mbms_services_active->mbms_service_array[num_ms]->privates.fields.mbms_bc.eps_bearer_context.bearer_level_qos.pl) {
					/**
					 * Found a new MBMS Service with preemption vulnerability & lowest ARP priority level.
					 */
					low_arp_prio_level = mbms_services_active->mbms_service_array[num_ms]->privates.fields.mbms_bc.eps_bearer_context.bearer_level_qos.pl;
					mbms_service_active_list_index = num_ms;
				}
			}
		}
	}
	OAILOG_WARNING(LOG_MCE_APP, "Found MBMS Service idx (%d) with lowest ARP Prio (%d). \n", mbms_service_active_list_index, low_arp_prio_level);
	OAILOG_FUNC_RETURN(LOG_MCE_APP, mbms_service_active_list_index);
}

/****************************************************************************/
/*********************  L O C A L    F U N C T I O N S  *********************/
/****************************************************************************/

/**
 * Calculate the TBS index based on TS 36.213 Tabke 7.1.7.1-1.
 */
//------------------------------------------------------------------------------
static
int get_itbs(uint8_t mcs){
	if(mcs <= 9)
		return mcs;
	else if (mcs <=16)
		return (mcs-1);
	else if(mcs <=27)
		return (mcs-2);
	else return -1;
}

//------------------------------------------------------------------------------
static
int mce_app_get_mch_mcs(mbsfn_area_context_t * const mbsfn_area_context, const qci_e qci) {
	uint32_t m2_enb_count = mbsfn_area_context->privates.m2_enb_id_hashmap->num_elements;
	DevAssert(m2_enb_count); /**< Must be larger than 1, else there is an error. */
	int mcs = get_qci_mcs(qci, ceil(mbsfn_area_context->privates.fields.mbsfn_area.mch_mcs_enb_factor * m2_enb_count));
	OAILOG_INFO(LOG_MCE_APP, "Calculated new MCS (%d) for MBSFN Area " MBSFN_AREA_ID_FMT" with %d eNBs. \n",
			mcs, mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_area_id, m2_enb_count);
	return mcs;
}

//------------------------------------------------------------------------------
static
bool mce_mbsfn_area_compare_by_mbms_sai (__attribute__((unused)) const hash_key_t keyP,
                                    void * const elementP,
                                    void * parameterP, void **resultP)
{
  const uint32_t                  * const cteid_p = (const uint32_t*const)parameterP;
  mbms_service_t                  * mbms_service_ref  = (mbms_service_t*)elementP;
  if ( *cteid_p == mbms_service_ref->privates.fields.mbms_bc.mbms_ip_mc_distribution.cteid) {
    *resultP = elementP;
    return true;
  }
  return false;
}

//------------------------------------------------------------------------------
static
bool mce_app_update_mbsfn_area(const mbsfn_area_id_t mbsfn_area_id, const mbms_service_area_id_t mbms_service_area_id, const uint32_t m2_enb_id, const sctp_assoc_id_t assoc_id) {
	OAILOG_FUNC_IN(LOG_MCE_APP);
	mbsfn_area_context_t 									* mbsfn_area_context = NULL;
	if(!pthread_rwlock_trywrlock(&mce_app_desc.rw_lock)) {
		mbsfn_area_context = mce_mbsfn_area_exists_mbsfn_area_id(&mce_app_desc.mce_mbsfn_area_contexts, mbsfn_area_id);
		if(mbsfn_area_context) {
			/** Found an MBSFN area, check if the eNB is registered. */
			if(hashtable_ts_is_key_exists (&mbsfn_area_context->privates.m2_enb_id_hashmap, (const hash_key_t)m2_enb_id) == HASH_TABLE_OK) {
	 			/** MBSFN Area contains eNB Id. Continuing. */
	 			DevMessage("MBSFN Area " + mbsfn_area_id + " has M2 eNB id " + m2_enb_id". Error during resetting M2 eNB.");
	 		}
	 		/**
	 		 * Updating the eNB count.
	 		 * MCS will be MCH specific of the MBSFN areas, and depend on the QCI/BLER.
	 		 */
			hashtable_uint64_ts_insert(&mbsfn_area_context->privates.m2_enb_id_hashmap, (const hash_key_t)m2_enb_id, NULL);
			/** Check if the MCCH timer is running, if not start it. */
			pthread_rwlock_unlock(&mce_app_desc.rw_lock);
			OAILOG_FUNC_RETURN (LOG_MME_APP, true);
		}
		OAILOG_INFO(LOG_MCE_APP, "No MBSFN Area could be found for the MBMS SAI " MBMS_SERVICE_AREA_ID_FMT ". Cannot update. \n", mbms_service_area_id);
		pthread_rwlock_unlock(&mce_app_desc.rw_lock);
	}
	OAILOG_FUNC_RETURN (LOG_MME_APP, false);
}

//------------------------------------------------------------------------------
static int
mce_insert_mbsfn_area_context (
  mce_mbsfn_area_contexts_t * const mce_mbsfn_area_contexts_p,
  const struct mbsfn_area_context_s *const mbsfn_area_context)
{
  OAILOG_FUNC_IN (LOG_MCE_APP);
  hashtable_rc_t                          h_rc 					= HASH_TABLE_OK;
  mbsfn_area_id_t													mbsfn_area_id = INVALID_MBSFN_AREA_ID;

  DevAssert (mce_mbsfn_area_contexts_p);
  DevAssert (mbsfn_area_context);
  mbsfn_area_id = mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_area_id;
  DevAssert (mbsfn_area_id != INVALID_MBSFN_AREA_ID);
  DevAssert (mbsfn_area_context->privates.fields.mbsfn_area.mbms_service_area_id != INVALID_MBMS_SERVICE_AREA_ID);
  h_rc = hashtable_ts_is_key_exists (mce_mbsfn_area_contexts_p->mbsfn_area_id_mbsfn_area_htbl, (const hash_key_t)mbsfn_area_id);
  if (HASH_TABLE_OK == h_rc) {
	  OAILOG_ERROR(LOG_MCE_APP, "The MBSFN area " MBSFN_AREA_ID_FMT" is already existing. \n", mbsfn_area_id);
	  OAILOG_FUNC_RETURN (LOG_MCE_APP, RETURNerror);
  }
  h_rc = hashtable_ts_insert (mce_mbsfn_area_contexts_p->mbsfn_area_id_mbsfn_area_htbl, (const hash_key_t)mbsfn_area_id, (void *)mbsfn_area_context);
  if (HASH_TABLE_OK != h_rc) {
	  OAILOG_ERROR(LOG_MCE_APP, "Error could not register the MBSFN Area context %p with MBSFN Area Id " MBSFN_AREA_ID_FMT" and MBMS Service Index " MBMS_SERVICE_INDEX_FMT ". \n",
			  mbsfn_area_context, mbsfn_area_id, mbsfn_area_context->privates.fields.mbsfn_area.mbms_service_area_id);
	  OAILOG_FUNC_RETURN (LOG_MCE_APP, RETURNerror);
  }

  /** Also a separate map for MBMS Service Area is necessary. We then match MBMS Sm Service Request directly to MBSFN area. */
  if (mbsfn_area_context->privates.fields.mbsfn_area.mbms_service_area_id) {
  	h_rc = hashtable_uint64_ts_insert (mce_mbsfn_area_contexts_p->mbms_sai_mbsfn_area_ctx_htbl,
			  (const hash_key_t)mbsfn_area_context->privates.fields.mbsfn_area.mbms_service_area_id, (void *)((uintptr_t)mbsfn_area_id));
	  if (HASH_TABLE_OK != h_rc) {
		  OAILOG_ERROR(LOG_MCE_APP, "Error could not register the MBSFN Service context %p with MBSFN Area Id "MBSFN_AREA_ID_FMT" to MBMS Service Index " MBMS_SERVICE_INDEX_FMT ". \n",
				  mbsfn_area_context, mbsfn_area_id, mbsfn_area_context->privates.fields.mbsfn_area.mbms_service_area_id);
		  OAILOG_FUNC_RETURN (LOG_MCE_APP, RETURNerror);
	  }
  }
  /*
   * Updating statistics
   */
  __sync_fetch_and_add (&mce_mbsfn_area_contexts_p->nb_mbsfn_area_managed, 1);
  __sync_fetch_and_add (&mce_mbsfn_area_contexts_p->nb_mbsfn_are_since_last_stat, 1);
  OAILOG_FUNC_RETURN (LOG_MCE_APP, RETURNok);
}

//------------------------------------------------------------------------------
static
void clear_mbsfn_area(mbsfn_area_context_t * const mbsfn_area_context) {
	OAILOG_FUNC_IN (LOG_MCE_APP);
	mbsfn_area_id_t mbsfn_area_id = mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_area_id;
	mbsfn_area_context->privates.fields.mbsfn_area.mbms_service_area_id = INVALID_MBSFN_AREA_ID;
	memset(&mbsfn_area_context->privates.fields, 0, sizeof(mbsfn_area_context->privates.fields));
	/** Clear the M2 eNB Hashmap. */
	hashtable_uint64_ts_destroy(mbsfn_area_context->privates.m2_enb_id_hashmap);
	OAILOG_INFO(LOG_MME_APP, "Successfully cleared MBSFN area "MME_UE_S1AP_ID_FMT ". \n", mbsfn_area_id);
	OAILOG_FUNC_OUT(LOG_MME_APP);
}

//------------------------------------------------------------------------------
static
bool mce_app_create_mbsfn_area(const mbsfn_area_id_t mbsfn_area_id, const mbms_service_area_id_t mbms_service_area_id, const uint32_t m2_enb_id, const sctp_assoc_id_t assoc_id) {
	OAILOG_FUNC_IN(LOG_MCE_APP);

	mbsfn_area_context_t 									* mbsfn_area_context = NULL;
	// todo: check the lock mechanism
	if(!pthread_rwlock_trywrlock(&mce_app_desc.rw_lock)) {
		/** Try to get a free MBMS Service context. */
		mbsfn_area_context = STAILQ_FIRST(&mce_app_desc.mce_mbsfn_area_contexts_list);
		DevAssert(mbsfn_area_context); /**< todo: with locks, it should be guaranteed, that this should exist. */
		if(mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_area_id != INVALID_MBSFN_AREA_ID){
			OAILOG_ERROR(LOG_MCE_APP, "No free MBSFN area left. Cannot allocate a new one.\n");
			pthread_rwlock_unlock(&mce_app_desc.rw_lock);
			OAILOG_FUNC_RETURN (LOG_MCE_APP, false);
		}
		/** Found a free MBSFN Area: Remove it from the head, add the MBSFN area id and set it to the end. */
		STAILQ_REMOVE_HEAD(&mce_app_desc.mce_mbsfn_area_contexts_list, entries); /**< free_mbsfn is removed. */
		/** Remove the EMS-EBR context of the bearer-context. */
		OAILOG_INFO(LOG_MCE_APP, "Clearing received current MBSFN area %p.\n", mbsfn_area_context);
		clear_mbsfn_area(mbsfn_area_context); /**< Stop all timers and clear all fields. */

		mme_config_read_lock(&mme_config);
		/**
		 * Initialize the M2 eNB Id bitmap.
		 * We wan't to make sure, to keep the MBSFN structure size unchanged, no memory overwrites.
		 * todo: optimization possible with hashFunc..
		 */
		mbsfn_area_context->privates.m2_enb_id_hashmap = hashtable_uint64_ts_create((hash_size_t)mme_config.mbms.max_m2_enbs, NULL, NULL);
		mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_area_id			  = mbsfn_area_id;
		/** A single MBMS area per MBSFN area. */
		mbsfn_area_context->privates.fields.mbsfn_area.mbms_service_area_id = mbms_service_area_id;

		/** Set the CSA period to a fixed RF128, to calculate the resources based on a 1sec base. */
		mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_csa_period_rf  = get_csa_period_rf(CSA_PERIOD_RF128);

		/**
		 * Set the MCCH configurations from the MmeCfg file.
		 * MME config is already locked.
		 */
		mbsfn_area_context->privates.fields.mbsfn_area.mcch_modif_period_rf 			= mme_config.mbms.mbms_mcch_modification_period_rf;
		mbsfn_area_context->privates.fields.mbsfn_area.mcch_repetition_period_rf  = mme_config.mbms.mbms_mcch_repetition_period_rf;
		mbsfn_area_context->privates.fields.mbsfn_area.mbms_mcch_msi_mcs 					= mme_config.mbms.mbms_mcch_msi_mcs;
		mbsfn_area_context->privates.fields.mbsfn_area.mch_mcs_enb_factor			 	  = mme_config.mbms.mch_mcs_enb_factor;
		mbsfn_area_context->privates.fields.mbsfn_area.mbms_sf_slots_half					= mme_config.mbms.mbms_subframe_slot_half;

		// todo: a function which checks multiple MBSFN areas for MCCH offset
		// todo: MCCH offset is calculated in the MCH resources?
		mbsfn_area_context->privates.fields.mbsfn_area.mcch_offset_rf			 				= 0;
		/** Indicate the MCCH subframes. */
		// todo: a function, depending on the number of total MBSFN areas, repetition/modification periods..
		// todo: calculate at least for 1 MCH of 1 MCCH the MCCH allocation, eNB can allocate dynamically for the correct subframe?
		mbsfn_area_context->privates.fields.mbsfn_area.m2_enb_band = mme_config.mbms.mbms_m2_enb_band;
		mbsfn_area_context->privates.fields.mbsfn_area.m2_enb_bw	 = mme_config.mbms.mbms_m2_enb_bw;
		/** Set the M2 eNB Id. Nothing else needs to be done for the MCS part. */
		hashtable_uint64_ts_insert(mbsfn_area_context->privates.m2_enb_id_hashmap, (hash_key_t)m2_enb_id, NULL);
		/** Add the MBSFN area to the back of the list. */
		STAILQ_INSERT_TAIL(&mce_app_desc.mce_mbsfn_area_contexts_list, mbsfn_area_context, entries);
		/** Add the MBSFN area into the MBMS service Hash Map. */
		DevAssert (mce_insert_mbsfn_area_context(&mce_app_desc.mce_mbsfn_area_contexts, mbsfn_area_context) == 0);
		pthread_rwlock_unlock(&mce_app_desc.rw_lock);
		mme_config_unlock(&mme_config);
		OAILOG_FUNC_RETURN (LOG_MME_APP, true);
	}
	OAILOG_FUNC_RETURN (LOG_MME_APP, false);
}

//------------------------------------------------------------------------------
static
void mce_update_mbsfn_area(struct mbsfn_areas_s * const mbsfn_areas, const mbsfn_area_id_t mbsfn_area_id, const mbms_service_area_id_t mbms_service_area_id, uint32_t m2_enb_id, const sctp_assoc_id_t assoc_id) {
	OAILOG_FUNC_IN(LOG_MCE_APP);
	mbsfn_area_context_t * mbsfn_area_context = NULL;
	/**
	 * Updated the response (MBSFN areas). Check if the MBSFN areas are existing or not.
	 */
	if(mce_app_update_mbsfn_area(mbsfn_area_id, mbms_service_area_id, m2_enb_id, assoc_id)){
		/**
		 * Successfully updated existing MBMS Service Area with eNB information.
		 * Set it in the MBSFN response!
		 */
		mbsfn_area_context = mce_mbsfn_area_exists_mbsfn_area_id(&mce_app_desc.mce_mbsfn_area_contexts, mbsfn_area_id);
		memcpy((void*)&mbsfn_areas->mbsfn_area_cfg[mbsfn_areas->num_mbsfn_areas++].mbsfnArea, (void*)&mbsfn_area_context->privates.fields.mbsfn_area, sizeof(mbsfn_area_t));
		OAILOG_INFO(LOG_MCE_APP, "MBSFN Area " MBSFN_AREA_ID_FMT " is already active. Successfully updated for MBMS_SAI " MBMS_SERVICE_AREA_ID_FMT " with eNB information (m2_enb_id=%d, sctp_assoc=%d).\n",
				mbsfn_area_id, mbms_service_area_id, m2_enb_id, assoc_id);
		OAILOG_FUNC_OUT(LOG_MCE_APP);
	} else {
		/** Could not update. Check if it needs to be created. */
		if(mce_app_create_mbsfn_area(mbsfn_area_id, mbms_service_area_id, m2_enb_id, assoc_id)){
			mbsfn_area_context = mce_mbsfn_area_exists_mbsfn_area_id(&mce_app_desc.mce_mbsfn_area_contexts, mbsfn_area_id);
			memcpy((void*)&mbsfn_areas->mbsfn_area_cfg[mbsfn_areas->num_mbsfn_areas++].mbsfnArea, (void*)&mbsfn_area_context->privates.fields.mbsfn_area, sizeof(mbsfn_area_t));
			/**
			 * Intelligently, amongst your MBSFN areas set the MCCH subframes.
			 */
			//		if(get_enb_type(mbsfn_area_context->privates.fields.mbsfn_area.m2_enb_band) == ENB_TYPE_NULL){
						mbsfn_area_context->privates.fields.mbsfn_area.mbms_mcch_subframes				= 0b10001; // todo: just temporary..
			//		} else if(get_enb_type(mbsfn_area_context->privates.fields.mbsfn_area.m2_enb_band) == ENB_TYPE_TDD) {
			//			mbsfn_area_context->privates.fields.mbsfn_area.mbms_mcch_subframes				= 0b000001;
			//		}
			OAILOG_INFO(LOG_MCE_APP, "Created new MBSFN Area " MBSFN_AREA_ID_FMT " for MBMS_SAI " MBMS_SERVICE_AREA_ID_FMT " with eNB information (m2_enb_id=%d, sctp_assoc=%d).\n",
					mbsfn_area_id, mbms_service_area_id, m2_enb_id, assoc_id);
			OAILOG_FUNC_OUT(LOG_MCE_APP);
		}
	}
	OAILOG_ERROR(LOG_MCE_APP, "MBSFN Area " MBSFN_AREA_ID_FMT " for MBMS_SAI " MBMS_SERVICE_AREA_ID_FMT " with eNB information (m2_enb_id=%d, sctp_assoc=%d) could neither be created nor updated. Skipping..\n",
			mbsfn_area_id, mbms_service_area_id, m2_enb_id, assoc_id);
	OAILOG_FUNC_OUT(LOG_MCE_APP);
}

//------------------------------------------------------------------------------
static
int mce_app_log_method_single_rf_csa_pattern(struct csa_pattern_s * new_csa_patterns, int num_radio_frames, int num_total_csa_pattern_offset) {
	int power2 								= 0;
	int radio_frames_alloced 	= 0;
	/**
	 * Check each power layer. Calculate a CSA pattern for each with a different offset.
	 * todo: currently, total CSA patterns cannot overlap.. later optimize this.
	 *
	 * Set a new single RF CSA pattern for this.
	 * We may not use the last CSA pattern.
	 */
	while(num_radio_frames){
		/**< Determines the periodicity of CSA patterns.. */
		DevAssert((power2 = floor(log2(num_radio_frames))));
		radio_frames_alloced = pow(2, power2);
		if(num_total_csa_pattern_offset == 7){
			/** Error, currently, we don't share radio frames among MBSFN areas, so the offsets will be distinct for each. */
			OAILOG_ERROR(LOG_MCE_APP, "No more CSA patterns left to allocate resources for MBSFN context. \n");
			return RETURNerror;
		}
		/** Calculate the number of radio frames, that can scheduled in a single RF CSA pattern in this periodicity. */
	  new_csa_patterns[num_total_csa_pattern_offset].mbms_csa_pattern_rfs 						= CSA_ONE_FRAME;
	  new_csa_patterns[num_total_csa_pattern_offset].csa_pattern_repetition_period_rf = get_csa_rf_alloc_period_rf(CSA_RF_ALLOC_PERIOD_RF32) / (radio_frames_alloced / 4);
	  /** Currently allocate all subframes in excess. */
	  // todo: currently schedule all radio frames.. later less..
	  new_csa_patterns[num_total_csa_pattern_offset].csa_pattern_sf.mbms_mch_csa_pattern_1rf = 0b00111111;
	  // todo: sum the number of subframes..
	  /** Reduce the total number of remaining subframes to be scheduled. */
	  // todo: do something with the number of subframes..
	  int num_subframes_allocated = CSA_SF_SINGLE_FRAME * (MBMS_CSA_PERIOD_GCS_AS_RF/(new_csa_patterns[num_total_csa_pattern_offset].csa_pattern_repetition_period_rf));
	  /**
	   * We allocated some MBSFN radio frames starting with the highest priority.
	   * Reduce the number of remaining MBSFN radio frames.
	   */
	  num_radio_frames -= radio_frames_alloced;
	}
	/** Successfully scheduled all radio frames! */
	OAILOG_INFO(LOG_MCE_APP, "Successfully scheduled all radio subframes for the MCHs into the CSA patterns. Current number of CSA offset is (%d). \n", num_total_csa_pattern_offset);
	return RETURNok;
}

//------------------------------------------------------------------------------
static
void mce_app_calculate_mbsfn_mchs(struct mbsfn_area_context_s * mbsfn_area_context, mbms_services_t * const mbms_services_active, mchs_t *const mchs) {
	int 									 total_duration_in_ms 						= 0;
	bitrate_t 						 pmch_total_available_br_per_sf 	= 0;

	total_duration_in_ms = mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_csa_period_rf * 10;
	/**
	 * No hash callback, just iterate over the active MBMS services.
	 */
	for(int num_mbms_service = 0; num_mbms_service < mbms_services_active->num_mbms_service; num_mbms_service++) {
		/**
		 * Get the MBSFN area.
		 * Calculate the resources based on the active eNBs in the MBSFN area.
		 */
		qci_e	qci = mbms_services_active->mbms_service_array[num_mbms_service]->privates.fields.mbms_bc.eps_bearer_context.bearer_level_qos.qci;
		// todo: Current all 15 QCIs fit!! todo --> later it might not!
		qci_ordinal_e qci_ord = get_qci_ord(qci);
		mch_t mch = mchs->mch_array[qci_ord];
		if(!mch.mch_qci){
			DevAssert(!mch.total_gbr_dl_bps);
			mch.mch_qci = qci;
		}
		/** Calculate per MCH the total bandwidth (bits per seconds // multiplied by 1000 @sm decoding). */
		mch.total_gbr_dl_bps += mbms_services_active->mbms_service_array[num_mbms_service]->privates.fields.mbms_bc.eps_bearer_context.bearer_level_qos.gbr.br_dl;
		/** Add the TMGI. */
		memcpy((void*)&mch.mbms_session_list.tmgis[mch.mbms_session_list.num_mbms_sessions++], (void*)&mbms_services_active->mbms_service_array[num_mbms_service]->privates.fields.tmgi, sizeof(tmgi_t));
	}

	/**
	 * The CSA period is set as 1 second (RF128). The minimum time of a service duration is set to the MCCH modification period!
	 * MSP will be set to the half length of the CSA period for now. Should be enough!
	 * Calculate the actual MCS of the MCH and how many bit you can transmit with an SF.
	 */
	for(int num_mch = 0; num_mch < MAX_MCH_PER_MBSFN; num_mch++){
		mch_t mch = mchs->mch_array[num_mch];
		if(mch.mch_qci) {
			/**
			 * Set MCH.
			 * Calculate per MCH, the necessary subframes needed in the CSA period.
			 * Calculate the MCS of the MCH.
			 */
			int mcs = mce_app_get_mch_mcs(mbsfn_area_context, mch.mch_qci);
			if(mcs == -1){
				DevMessage("Error while calculating MCS for MBSFN Area " + mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_area_id + " and QCI " + qci);
			}
			/** Calculate the necessary transport blocks. */
			int itbs = get_itbs(mcs);
			if(itbs == -1){
				DevMessage("Error while calculating TBS index for MBSFN Area " + mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_area_id + " for MCS " + mcs);
			}
			/**
			 * We assume a single antenna port and just one Transport Block per subframe.
			 * No MIMO is expected.
			 * Number of bits transmitted per 1ms (1028)ms.
			 * Subframes, allocated for this MCH gives us the following capacity.
			 * No MCH subframe-interleaving is forseen. So each MCH will have own subframes. Calculate the capacity of the subframes.
			 * The duration is the CSA period, we calculate the MCHs on.
			 * ITBS starts from zero, so use actual values.
			 */
			bitrate_t available_br_per_subframe = TBStable[itbs][mbsfn_area_context->privates.fields.mbsfn_area.m2_enb_bw -1];
			bitrate_t mch_total_br_per_ms = mch.total_gbr_dl_bps /1000; /**< 1000 */
			bitrate_t total_bitrate_in_csa_period = mch_total_br_per_ms * total_duration_in_ms; /**< 1028*/
			/** Check how many subframes we need. */
			mch.mch_subframes_per_csa_period = ceil(total_bitrate_in_csa_period / available_br_per_subframe);
			/** Check if half or full slot. */
			if(mbsfn_area_context->privates.fields.mbsfn_area.mbms_sf_slots_half){
				/** Multiply by two, since only half a slot is used. */
				mch.mch_subframes_per_csa_period *=2;
			}
			/** Don't count the MCCH. */
			mchs->total_subframes_per_csa_period_necessary += mch.mch_subframes_per_csa_period;
		}
	}
}

//------------------------------------------------------------------------------
static
void mce_app_calculate_csa_common_pattern(struct csa_patterns_s * csa_patterns_p, mchs_t * mchs, uint8_t num_mbsfn_idx, uint8_t total_mbsfn_areas_tbs){
  /**
   * Depending on how many MBSFN areas are to be scheduled (i), set the ith subframe to the MBSFN area.
   * Check that there should be at least one MBSFN area to be scheduled.
   */
	csa_patterns_p->csa_pattern[COMMON_CSA_PATTERN].csa_pattern_offset_rf = COMMON_CSA_PATTERN;
	csa_patterns_p->csa_pattern[COMMON_CSA_PATTERN].mbms_csa_pattern_rfs = CSA_ONE_FRAME;
	csa_patterns_p->csa_pattern[COMMON_CSA_PATTERN].csa_pattern_repetition_period_rf = get_csa_rf_alloc_period_rf(CSA_RF_ALLOC_PERIOD_RF32); /**< One Frame CSA pattern only occurs in every 32RFs. Shared by all MBSFN areas. */
	/**
	 * Set the bits depending on the number of the total MBSFN areas.
	 * Set all bits to the MBSFN area if the total num is 1.
	 * Else, just set the ith bit.
	 */

	// TODO FDD/TDD specific setting with UL/DL percentages..

	int csa_bits_set 	= (6/total_mbsfn_areas_tbs); 		   /**< 0x06, 0x03, 0x02. */
	int csa_offset 		= num_mbsfn_idx * csa_bits_set;    /**< Also indicates the # of current MBSFN area. */
	for(int i_bit = 0; i_bit < csa_bits_set; i_bit++){
		/** Set the bits of the last CSA. */
		csa_patterns_p->csa_pattern[COMMON_CSA_PATTERN].csa_pattern_sf.mbms_mch_csa_pattern_1rf = ((0x01 << (csa_offset + i_bit)));
	}
	// todo: set the last subframe of the MCH as the end-1 repetition period of the CSA pattern!
	/** Amount of subframes that can be allocated in the CSA[COMMON_CSA_PATTERN]. */
	int num_final_csa_data_sf = csa_bits_set * ((MBMS_CSA_PERIOD_GCS_AS_RF / csa_patterns_p->csa_pattern[COMMON_CSA_PATTERN].csa_pattern_repetition_period_rf) -1);
	OAILOG_DEBUG(LOG_MCE_APP, "Set %d SFs in COMMON_CSA for MBSFN area. Removing from total #sf %d.", num_final_csa_data_sf, mchs->total_subframes_per_csa_period_necessary);
	if(mchs->total_subframes_per_csa_period_necessary >= num_final_csa_data_sf)
		mchs->total_subframes_per_csa_period_necessary -= num_final_csa_data_sf;
	else
		mchs->total_subframes_per_csa_period_necessary = 0;
}

//------------------------------------------------------------------------------
static
void mce_app_reuse_csa_pattern_set_subframes_fdd(struct csa_pattern_s * csa_pattern_mbsfn, struct csa_pattern_s * csa_pattern, int *mch_subframes_to_be_scheduled_p){
	OAILOG_FUNC_IN(LOG_MCE_APP);

	// todo : TDD combinations
	/** Check any subframes are left: Count the bits in each octet. */
	uint8_t csa_pattern_4rf_ = 0;
	uint8_t num_bits_checked = 0;
	uint8_t num_subframes_per_csa_pattern = 0;
	uint8_t num_bits_in_csa_pattern = (8 * csa_pattern->mbms_csa_pattern_rfs);
	while(num_bits_checked < num_bits_in_csa_pattern){
		csa_pattern_4rf_ = (csa_pattern->csa_pattern_sf.mbms_mch_csa_pattern_4rf >> num_bits_checked) & 0x3F; /**< Last one should be 0. */
		num_bits_checked+=8;
		if(csa_pattern_4rf_ != 0x3F){
			/**
			 * Pattern not filled fully.
			 * Check how many subframes are set.
			 */
			/** Count the number of set MBSFN subframes, no matter if 1 or 4 RFs. */
			for (; csa_pattern_4rf_; num_subframes_per_csa_pattern++)
			{
				csa_pattern_4rf_ &= (csa_pattern_4rf_-1);
			}
			/** Assert that the remaining subframes are zero! We wan't to set them in order without gaps!. */
			DevAssert(!(csa_pattern->csa_pattern_sf.mbms_mch_csa_pattern_4rf >> num_bits_checked)); /**< Does not need to be in bounds. */
			break;
		}
	}
	if(num_bits_checked >= num_bits_in_csa_pattern){
		OAILOG_DEBUG(LOG_MCE_APP, "4RF-CSA pattern has no free subframes left. Checking the other CSA patterns. \n");
		OAILOG_FUNC_OUT(LOG_MCE_APP);
	}
	DevAssert(CSA_SF_SINGLE_FRAME-num_subframes_per_csa_pattern);

	/**
	 * Copy the offset, repetition period and type.
	 * */
	csa_pattern_mbsfn->csa_pattern_offset_rf = csa_pattern->csa_pattern_offset_rf;
	csa_pattern_mbsfn->csa_pattern_repetition_period_rf = csa_pattern->csa_pattern_repetition_period_rf;
	csa_pattern_mbsfn->mbms_csa_pattern_rfs = csa_pattern->mbms_csa_pattern_rfs;
	/** Derive the SF allocation from the existing CSA pattern. */
	int num_sf_free = (MBMS_CSA_PERIOD_GCS_AS_RF / csa_pattern->csa_pattern_repetition_period_rf) * (CSA_SF_SINGLE_FRAME-num_subframes_per_csa_pattern)
			+ ((num_bits_in_csa_pattern - num_bits_checked) * CSA_SF_SINGLE_FRAME);
//	OAILOG_DEBUG(LOG_MCE_APP, "Found (%d) empty subframes in (%d)-RF CSA with offset (%d) and repetition period (%d)RF. \n",
//			num_sf_free, csa_pattern->mbms_csa_pattern_rfs, csa_pattern->csa_pattern_offset_rf, csa_pattern->csa_pattern_repetition_rf);

	/** Remove from the total MCH subframes, the part-pattern free subframes. */
	uint8_t csa_pattern_to_be_set = csa_pattern_mbsfn->csa_pattern_sf.mbms_mch_csa_pattern_4rf >> (num_bits_checked - 8);
	for(int num_sf = 0; num_sf < (CSA_SF_SINGLE_FRAME-num_subframes_per_csa_pattern); num_sf++ ){
		/** Reduced the number of SFs. */
		*mch_subframes_to_be_scheduled_p -= (MBMS_CSA_PERIOD_GCS_AS_RF / csa_pattern->csa_pattern_repetition_period_rf);
		/** Set the subframe in the CSA allocation pattern. */
		csa_pattern_to_be_set |= (0x01 << ((CSA_SF_SINGLE_FRAME-num_subframes_per_csa_pattern) -1));
		if(!*mch_subframes_to_be_scheduled_p){
//			OAILOG_INFO(LOG_MCE_APP, "Completely set all SFs of the MCH in previously allocated CSA pattern. \n",
//					num_sf_free, csa_pattern->csa_pattern_offset_rf, csa_pattern->csa_pattern_repetition_rf);
			break;
		}
	}
	if(!*mch_subframes_to_be_scheduled_p){
		OAILOG_INFO(LOG_MCE_APP, "Stuffed all bits of the MBSFN CSA pattern into the first found CSA octet. \n");
		OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNok);
	}

	while((num_bits_checked + 8) > num_bits_in_csa_pattern && *mch_subframes_to_be_scheduled_p){
		/** We fitted into the last pattern element. Continue. */
		int sf = (int)(ceil(*mch_subframes_to_be_scheduled_p / (MBMS_CSA_PERIOD_GCS_AS_RF / csa_pattern->csa_pattern_repetition_period_rf))) % (CSA_SF_SINGLE_FRAME + 1);
		// todo: devAssert(0x3F3F3F3F);
		// todo: continue or so..
		int i_sf = 1;
		while(sf) { /**< Starting from the left, set the bits.*/
			csa_pattern_mbsfn->csa_pattern_sf.mbms_mch_csa_pattern_4rf |= (0x01 << (CSA_SF_SINGLE_FRAME - i_sf)) << num_bits_checked;
			sf--;
			*mch_subframes_to_be_scheduled_p-=sf;
		}
		num_bits_checked+=8;
	}
	if(!*mch_subframes_to_be_scheduled_p){
		OAILOG_INFO(LOG_MCE_APP, "Stuffed all bits of the MBSFN CSA pattern into the remaining CSA octets. \n");
		OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNok);
	}
//	OAILOG_INFO(LOG_MCE_APP, "Iterated through the CSA Pattern with offset (%d) and repetition period. (%d) subframes are remaining. "
//			"Checking the remaning subframe patterns. \n", csa_pattern->csa_pattern_offset_rf, csa_pattern_repetition_rf, *mch_subframes_to_be_scheduled_p);
	/**
	 * Set the remaining subframes of the partial frame as set!.
	 * Remaining subframes should also be empty, if any, exist, allocate as much as possible.
	 */
}

/**
 * We cannot enter this method one by one for each MBSFN area.
 * @param: csa_patterns_alloced: should be a union of the CSA patterns of all previously scheduled MBSFN areas.
 */
//------------------------------------------------------------------------------
static
void mce_app_reuse_csa_pattern(struct csa_patterns_s * csa_patterns_mbsfn_p, mchs_t * mchs, const struct csa_patterns_s * const csa_patterns_alloced){
	OAILOG_FUNC_IN(LOG_MCE_APP);
	/**
	 * Iterate the CSA patterns, till the COMMON_CSA pattern.
	 * Check for any available 4RF and 1RF CSA patterns.
	 * Start with the lowest repetition factor (4). Move up to 16.
	 */
	for(csa_frame_num_e csa_frame_num = CSA_FOUR_FRAME; csa_frame_num*=4; csa_frame_num <= 4) {
		for(csa_rf_alloc_period_e num_csa_repetition = CSA_RF_ALLOC_PERIOD_RF32; num_csa_repetition >= CSA_PERIOD_RF8; num_csa_repetition--){ /**< We use 32, 16, 8. */
			/** The index is always absolute and not necessarily equal to the CSA offset. */
			for(int num_csa_pattern = 0; num_csa_pattern < COMMON_CSA_PATTERN; num_csa_pattern++){
				/** Check if 4RF. */
				int csa_pattern_repetition_rf = get_csa_rf_alloc_period_rf(num_csa_repetition);
				struct csa_pattern_s * csa_pattern = &csa_patterns_alloced->csa_pattern[num_csa_pattern];
				if(csa_pattern->mbms_csa_pattern_rfs == csa_frame_num && csa_pattern->csa_pattern_repetition_period_rf == csa_pattern_repetition_rf){
					struct csa_pattern_s * csa_pattern_mbsfn = &csa_patterns_mbsfn_p->csa_pattern[num_csa_pattern];

					//
					//	// TODO FDD/TDD specific setting with UL/DL percentages..
					//
					mce_app_reuse_csa_pattern_set_subframes_fdd(csa_pattern_mbsfn, csa_pattern, &mchs->total_subframes_per_csa_period_necessary);
					/** No return expected, just check if all subframes where scheduled. */
					if(!mchs->total_subframes_per_csa_period_necessary){
						/**
						 * No more MCH subframes to be scheduled.
						 * No further CSA offsets need to be defined, we can re-use the existing. */
						OAILOG_INFO(LOG_MCE_APP, "Fitted %d newly received MCHs into existing CSA patterns. \n", mchs->num_mch);
						OAILOG_FUNC_OUT(LOG_MCE_APP);
					} else {
						OAILOG_INFO(LOG_MCE_APP, "After checking CSA (%d)RF-pattern with offset %d and period (%d), "
								"still %d subframes exist for (%d) MCHs. \n", csa_frame_num, csa_pattern->csa_pattern_offset_rf,
								csa_pattern->csa_pattern_repetition_period_rf, mchs->total_subframes_per_csa_period_necessary, mchs->num_mch);
						OAILOG_FUNC_OUT(LOG_MCE_APP);
					}
				}
			}
		}
	}
	OAILOG_INFO(LOG_MCE_APP, "After all existing CSA patterns, still %d subframes exist for (%d) MCHs. \n", mchs->total_subframes_per_csa_period_necessary, mchs->num_mch);
}

//------------------------------------------------------------------------------
static
int mce_app_csa_pattern_(struct csa_patterns_s * new_csa_patterns, int total_subframes_per_csa_period_necessary){
	OAILOG_FUNC_IN(LOG_MCE_APP);
//	memset((void*)&csa_pattern->csa_pattern_sf, 0, sizeof(csa_pattern->csa_pattern_sf));
	/**
	 * Except for the last CSA Pattern, we received a new array (7) CSA patterns.
	 * Check the necessary MBSFN subframes and distribute them.
	 *
	 * Start with a single frame logic. Check the number of radio frames necessary first.
	 * Total number of subframes, have been reduced by the subframes set in CSA[MCCH].
	 */
	// todo: later  mix here MBSFNs into same radio frame.. or finally, when remaining.. (currently ceil is used, later floor)..
	// todo: check how many subframes you can set per FDD/TDD
	int num_radio_frames = ceil(total_subframes_per_csa_period_necessary/6);
	// todo: optimize this as well.
	num_radio_frames = (num_radio_frames + num_radio_frames %4); /**< Make it the next multiple of 4. */

	//	uint32_t					  mbms_mch_csa_pattern_rf 			= *((uint32_t*)&mbsfn_area_context->privates.fields.mbsfn_area.csa_pattern.csa_pattern_sf);
	////	while(num_subframes_per_csa_pattern < (6 * mbsfn_area_context->privates.fields.mbsfn_area.csa_pattern.mbms_csa_pattern_rfs)){
	//	uint32_t					  	mbsfn_mch_bitmap 	= mbms_mch_csa_pattern_rf;
	//	uint8_t 						num_subframes_per_csa_pattern = 0;
	//		// todo: check 1 or 4 radio frame!
	//		/** Check how many times, inside the CSA Period, the CSA pattern occurs. */
	//		int num_csa_pattern_per_csa_period = mbsfn_area_context->privates.fields.mbsfn_area.mbsfn_csa_period_rf / mbsfn_area_context->privates.fields.mbsfn_area.csa_pattern.csa_pattern_repetition_period_rf;
	//		DevAssert(mbsfn_area_context->privates.fields.mbsfn_area.csa_pattern.csa_pattern_repetition_period_rf >= mbsfn_area_context->privates.fields.mbsfn_area.csa_pattern.mbms_csa_pattern_rfs);
	//		/** Each time, one or 4 times the above CSA pattern may occur. */
	//		// todo: currently just one RF per CSA( not 4)!		num_csa_pattern = num_csa_pattern * mbsfn_area_context->privates.fields.csa_frame_num;
	//		/** .. gives the total number of subframes in the CSA period. */
	//		int total_num_subframes_per_csa_period = (num_subframes_per_csa_pattern * num_csa_pattern_per_csa_period);
	//		if(total_num_subframes_per_csa_period >= mchs->total_subframes_per_csa_period_necessary){
	//			OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNok);
	//		}
	//		/**
	//		 * Increment number of CSA subframes: Set the first rightmost bit to one!
	//		 * Don't decrement, we don't calculate the optimum pattern.
	//		 */
	//		mbms_mch_csa_pattern_rf |= ((0x01) << (num_subframes_per_csa_pattern +1));
	//		// todo: later decrement the MCH repetition period.. and RF allocation..
	//	}



	/**
	 * We will repeat the 4Frame CSA pattern check for each.
	 * Repeat at least once.
	 */
	for(csa_rf_alloc_period_e csa_rf_alloc_period = CSA_RF_ALLOC_PERIOD_RF8; csa_rf_alloc_period >= CSA_RF_ALLOC_PERIOD_RF32 ; csa_rf_alloc_period++){
		/**
		 * Check if the MBSFN services has subframes left above the threshold.
		 * If so, schedule the MBSFN subframes, and set the CSA pattern.
		 */
		// todo: check if a remaining enough offset (<COMMON_CSA_PATTERN), still exists.
	}
	int csa_4frame_rfs_repetition = ((num_radio_frames / 0.75)/(4 * 4));
	if(csa_4frame_rfs_repetition) {
		/**
		 * Allocate a 4RF CSA pattern with the given period.
		 * For the remaining RFs calculate a single frame CSA pattern.
		 */
		// todo: check if there are CSA offsets left. if not reject..
		if(new_csa_patterns->total_csa_pattern_offset + 4 <= COMMON_CSA_PATTERN){
			/** Allocate a 4 Frame CSA pattern with the given periodicity. */
			new_csa_patterns->csa_pattern[new_csa_patterns->total_csa_pattern_offset].mbms_csa_pattern_rfs											= CSA_FOUR_FRAME;
			new_csa_patterns->csa_pattern[new_csa_patterns->total_csa_pattern_offset].csa_pattern_repetition_period_rf 				= (get_csa_rf_alloc_period_rf(CSA_RF_ALLOC_PERIOD_RF32))/csa_4frame_rfs_repetition;
			// todo: allocate as many subframes as needed and not all of them!
			new_csa_patterns->csa_pattern[new_csa_patterns->total_csa_pattern_offset].csa_pattern_sf.mbms_mch_csa_pattern_4rf	= 0x3F3F3F3F; /**< Currently. set a full frame. */
			/**
			 * Set a full 4Frame CSA period.
			 * Increment the total csa_pattern offset.
			 */
			new_csa_patterns->total_csa_pattern_offset +=4;
			num_radio_frames -= (csa_4frame_rfs_repetition * 4 * 4);
		} else {
			OAILOG_ERROR(LOG_MCE_APP, "Cannot allocate one more CSA pattern. Rejecting resource allocation. \n");
			OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNerror);
		}
	}

	if(num_radio_frames){
		/** Check if another CSA pattern can be allocated. */
		if(new_csa_patterns->total_csa_pattern_offset +1 <= COMMON_CSA_PATTERN){
			// todo: check the offset if this much CSA patterns can be scheduled..
			if(mce_app_log_method_single_rf_csa_pattern(new_csa_patterns, &num_radio_frames, new_csa_patterns->total_csa_pattern_offset) == RETURNerror){
				OAILOG_ERROR(LOG_MCE_APP, "Error while scheduling the CSA pattern.\n");
				OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNerror);
			}
			/** No more radio frames should be left, all should be allcated by the last single RF CSA pattern. */
			DevAssert(!num_radio_frames);
		} else {
			/**
			 * One more CSA Pattern cannot be scheduled.
			 * We cannot fit the given MCHs into the CSA Period.
			 * Reject the resource allocation.
			 */
			OAILOG_ERROR(LOG_MCE_APP, "Cannot allocate one more CSA pattern. Rejecting resource allocation. \n");
			OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNerror);
		}
	}

//	for(new_csa_pattern->csa_pattern_repetition_period_rf = 1; csa_pattern->csa_pattern_repetition_period_rf <= max_csa_pattern_repetition; csa_pattern->csa_pattern_repetition_period_rf++){
//		for(int num_csa_pattern_subframe = 1; num_csa_pattern_subframe <= (csa_pattern->mbms_csa_pattern_rfs * 6); num_csa_pattern_subframe++) {
//			// todo: how/when to increment the CSA period?!
//			int total_csa_subframe = (csa_period_rf / (CSA_PATTERN_REPETITION_MIN / csa_pattern->csa_pattern_repetition_period_rf)) * num_csa_pattern_subframe;
//			if(total_subframes_per_csa_period_necessary <= total_csa_subframe) {
//				OAILOG_INFO(LOG_MCE_APP, "For (%d) necessary MBSFN subframes, we have (%d) num_csa_pattern_repetitions and subframes [%d] per (%d)-RF CSA Pattern.\n",
//						total_subframes_per_csa_period_necessary, csa_pattern->csa_pattern_repetition_period_rf, csa_pattern->csa_pattern_sf.mbms_mch_csa_pattern_1rf, csa_pattern->mbms_csa_pattern_rfs);
//				/** Set it into the CSA pattern. */
//				int num_csa_pattern = (2^num_csa_pattern_subframe) -1;
//				memcpy((void*)&csa_pattern->csa_pattern_sf, (void*)&num_csa_pattern, sizeof(uint8_t) * csa_pattern->mbms_csa_pattern_rfs);
//				return true;
//			}
//		}
//	}
	OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNok);
}

/**
 * Check the CSA pattern for this MBSFN!
 * May reuse the CSA patterns of the already allocated MBSFNs (const - unchangeable).
 * We use the total number of MBSFN area to update first CSA[7] where we also leave the last repetition for MCCH subframes.
 * If the MCCH Modification period is 2*CSA_PERIOD (2s), the subframes in the last repetition will not be filled with data,
 * because it would overwrite in the CSA period where the MCCHs occur.
 */
//------------------------------------------------------------------------------
static
int mce_app_calculate_overall_csa_pattern(struct csa_patterns_s * const csa_patterns_p, mchs_t * const mchs, const uint8_t num_mbsfn_idx,
		const uint8_t total_mbsfn_areas_to_be_scheduled, struct csa_patterns_s * const union_csa_patterns_allocated_p) {
	OAILOG_FUNC_IN(LOG_MCE_APP);
	/** Calculate the Common CSA pattern for the MBSFN area and reduce the #SFs left. */
	mce_app_calculate_csa_common_pattern(csa_patterns_p, mchs, num_mbsfn_idx, total_mbsfn_areas_to_be_scheduled);
	if(!mchs->total_subframes_per_csa_period_necessary){
		/**
		 * Very few MBSFN subframes were needed, all fittet into CSA_COMMON.
		 * Total CSA pattern offset is not incremented. We check it always against COMMON_CSA_PATTERN (reserved).
		 */
		OAILOG_INFO(LOG_MCE_APP, "Fitted all data into CSA_COMMON for MBSFN-Area-idx (%d).\n", num_mbsfn_idx);
		OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNok);
	}

	/**
	 * Update the CSA pattern of the MBSFN area with the remaining subframes. Start checking by the already allocated CSA subframes.
	 * We don't need consecutive allocates of subframes/radio frames between MBSFN areas.
	 * No return value is needed, since we will try to allocate new resources, if MBSFN SFs remain.
	 * The new csa_patterns will be derived from the already allocated csa_patterns in the mbsfn_areas object.
	 */
	if(num_mbsfn_idx){
		OAILOG_INFO(LOG_MCE_APP, "Checking previous MBSFN area CSA patterns.\n");
		for(int num_mbsfn_id = 0; num_mbsfn_id < num_mbsfn_idx; num_mbsfn_id++) {
			mce_app_reuse_csa_pattern(csa_patterns_p, mchs->total_subframes_per_csa_period_necessary, union_csa_patterns_allocated_p);
			if(mchs->total_subframes_per_csa_period_necessary){
				/**
				 * Total CSA pattern offset is not incremented. We check it always against COMMON_CSA_PATTERN (reserved).
				 */
				OAILOG_INFO(LOG_MCE_APP, "Fitted all data into previously allocated CSA patterns for MBSFN-Area-idx (%d). No need to calculate new CSA patterns.\n",
						num_mbsfn_idx);
				OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNok);
			}
		}
	}
	if(!mchs->total_subframes_per_csa_period_necessary){
		if(!mce_app_csa_pattern_(csa_patterns_p, mchs->total_subframes_per_csa_period_necessary)) {
			// todo: handle this case --> move from 1 to 4 RF, extend CSA period. etc.. (further discussions needs to be made about this).
			OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNerror);
		}
	}
	OAILOG_FUNC_RETURN(LOG_MCE_APP, RETURNok);
}
