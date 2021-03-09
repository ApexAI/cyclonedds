/*
 * Copyright(c) 2021 Apex.AI Inc. All rights reserved.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v. 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0, or the Eclipse Distribution License
 * v. 1.0 which is available at
 * http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * SPDX-License-Identifier: EPL-2.0 OR BSD-3-Clause
 */
//TODO: adapt filename, reorganize structure
//Isolate the functionality of the iceoryx waitset and an active listener thread
//which will later be replaced by a listener from iceoryx.
#ifndef _SHM_LISTENER_H_
#define _SHM_LISTENER_H_

#include "iceoryx_binding_c/binding.h"

#include "dds/ddsrt/threads.h"

#if defined (__cplusplus)
extern "C" {
#endif

#define SHM_MAX_NUMBER_OF_READERS 128

//forward declaration to avoid circular dependencies with dds__types.h
struct dds_reader;

enum shm_run_states {
    SHM_LISTENER_STOP = 0,
    SHM_LISTENER_RUN = 1,
    SHM_LISTENER_STOPPED = 2,
    SHM_LISTENER_NOT_RUNNING = 3
};
struct shm_listener {
 iox_ws_storage_t m_waitset_storage;
 iox_ws_t m_waitset;
 

 //use this if we wait but want to wake up for some reason
 //e.g. terminate, update the waitset etc.
 iox_user_trigger_t m_wakeup_trigger;
 uint32_t m_run_state; //TODO: should be atomic

 ddsrt_thread_t m_thread;

};

typedef struct shm_listener shm_listener_t;

void shm_listener_init(shm_listener_t* listener);

void shm_listener_destroy(shm_listener_t* listener);

void shm_listener_attach_reader(shm_listener_t* listener, struct dds_reader* reader);

void shm_listener_detach_reader(shm_listener_t* listener, struct dds_reader* reader);

uint32_t shm_listener_wait_thread(void* listener);

#if defined (__cplusplus)
}
#endif
#endif
