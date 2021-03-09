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


#include "shm__listener.h"
#include "dds__types.h"
#include "dds/ddsi/q_receive.h"

#if defined (__cplusplus)
extern "C" {
#endif

void shm_listener_init(shm_listener_t* listener) {
    listener->m_waitset = iox_ws_init(&listener->m_waitset_storage);
    iox_ws_attach_user_trigger_event(listener->m_waitset, listener->m_wakeup_trigger, 0, NULL);
    listener->m_run_state = SHM_LISTENER_RUN;

    ddsrt_threadattr_t attr;
    ddsrt_threadattr_init (&attr);
    dds_return_t rc = ddsrt_thread_create(&listener->m_thread, "shm_listener_thread", 
                                          &attr, shm_listener_wait_thread, listener);
    if(rc != DDS_RETCODE_OK) {
        listener->m_run_state = SHM_LISTENER_NOT_RUNNING;
    }

}

void shm_listener_destroy(shm_listener_t* listener) {
    if(listener->m_run_state != SHM_LISTENER_NOT_RUNNING) {
        listener->m_run_state = SHM_LISTENER_STOP;
        iox_user_trigger_trigger(listener->m_wakeup_trigger);
        uint32_t result;
        dds_return_t rc = ddsrt_thread_join(listener->m_thread, &result);

        if(rc == DDS_RETCODE_OK) {
            listener->m_run_state = SHM_LISTENER_NOT_RUNNING;
        }
    }
    //note: we must ensure no readers are actively using the waitset anymore,
    //the listener and thus the waitset is to be destroyed after all readers are destroyed
    iox_ws_deinit(listener->m_waitset);
}

void shm_listener_attach_reader(shm_listener_t* listener, struct dds_reader* reader) {
    uint64_t reader_id = reader->m_entity.m_guid.entityid.u;
    iox_ws_attach_subscriber_event(listener->m_waitset, reader->m_sub, SubscriberEvent_HAS_DATA, reader_id, NULL);
}

void shm_listener_detach_reader(shm_listener_t* listener, struct dds_reader* reader) {
    iox_ws_detach_subscriber_event(listener->m_waitset, reader->m_sub, SubscriberEvent_HAS_DATA);
}

uint32_t shm_listener_wait_thread(void* arg) {
    shm_listener_t* listener = arg; 

    uint64_t missed_elements = 0;
    uint64_t number_of_events = 0;

    iox_event_info_t events[SHM_MAX_NUMBER_OF_READERS];

    //TODO: do we need better start/stop logic with restart capability?
    while(listener->m_run_state == SHM_LISTENER_RUN) {

        number_of_events = iox_ws_wait(listener->m_waitset, events, SHM_MAX_NUMBER_OF_READERS, &missed_elements);

        for (uint64_t i = 0; i < number_of_events; ++i) {
            iox_event_info_t event = events[i];
            if (iox_event_info_does_originate_from_user_trigger(event, listener->m_wakeup_trigger))
            {
                //do we have to do something or terminate?
                //TODO: waitset modification here
            } else {
                //some reader got data
                uint64_t reader_id = iox_event_info_get_event_id(event);

                //TODO: can we do this? how can we get the reader from the id?
                dds_reader * const reader = (dds_reader *) reader_id;
                const void* chunk;

                while(iox_sub_take_chunk(reader->m_sub, &chunk)) {
                    //handle received chunk
                    //TODO: refactor the "callback"/handler
                    read_callback(chunk, reader);
                }
            }
        }
    }
    listener->m_run_state = SHM_LISTENER_STOPPED;
    return 0;
}

#if defined (__cplusplus)
}
#endif
