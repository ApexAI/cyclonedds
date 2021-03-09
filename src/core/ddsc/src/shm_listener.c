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
    iox_ws_attach_user_trigger_event(listener->m_waitset, listener->m_wakeup_trigger, 0U, NULL);
    listener->m_run_state = SHM_LISTENER_RUN;
}

void shm_listener_deinit(shm_listener_t* listener) {
    listener->m_run_state = SHM_LISTENER_STOP;
    iox_user_trigger_trigger(listener->m_wakeup_trigger);
    //TODO: more cleanup (thread) ?
}

void shm_listener_attach_reader(shm_listener_t* listener, struct dds_reader* reader) {
    uint64_t reader_id = reader->m_entity.m_guid.entityid.u;
    iox_ws_attach_subscriber_event(listener->m_waitset, reader->m_sub, SubscriberEvent_HAS_DATA, reader_id, NULL);
}

void shm_listener_detach_reader(shm_listener_t* listener, struct dds_reader* reader) {
    iox_ws_detach_subscriber_event(listener->m_waitset, reader->m_sub, SubscriberEvent_HAS_DATA);
}

void shm_listener_wait_thread_main(shm_listener_t* listener) {

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
        //we can do this here since the thread will not start again and no readers should be active
        //and use the waitset
        //TODO: look out for races/lifetime issues of the waitset contained in dds_global
        iox_ws_deinit(listener->m_waitset);
        listener->m_run_state = SHM_LISTENER_STOPPED;
    }
}

#if defined (__cplusplus)
}
#endif
