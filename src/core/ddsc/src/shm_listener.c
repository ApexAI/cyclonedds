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
#include "dds__entity.h"
#include "dds/ddsi/q_receive.h"

#if defined (__cplusplus)
extern "C" {
#endif

void shm_listener_init(shm_listener_t* listener) {
    ddsrt_mutex_init(&listener->m_lock);
        
    listener->m_waitset = iox_ws_init(&listener->m_waitset_storage);

    listener->m_wakeup_trigger = iox_user_trigger_init(&listener->m_wakeup_trigger_storage);
    iox_ws_attach_user_trigger_event(listener->m_waitset, listener->m_wakeup_trigger, 0, NULL);

    listener->m_number_of_modifications_pending = 0;
    listener->m_number_of_attached_readers = 0;
    for(int32_t i=0; i<SHM_MAX_NUMBER_OF_READERS; ++i) {
        listener->m_readers_to_attach[i] = NULL;
        listener->m_readers_to_detach[i] = NULL;
    }

    listener->m_run_state = SHM_LISTENER_RUN;
    ddsrt_threadattr_t attr;
    ddsrt_threadattr_init (&attr);
    dds_return_t rc = ddsrt_thread_create(&listener->m_thread, "shm_listener_monitor_thread", 
                                          &attr, shm_listener_monitor_thread, listener);
    if(rc != DDS_RETCODE_OK) {
        listener->m_run_state = SHM_LISTENER_NOT_RUNNING;
    }

}

void shm_listener_destroy(shm_listener_t* listener) {
    printf("***destroy\n");
    if(listener->m_run_state != SHM_LISTENER_NOT_RUNNING) {
        listener->m_run_state = SHM_LISTENER_STOP;
        shm_listener_wake(listener);
        uint32_t result;
        dds_return_t rc = ddsrt_thread_join(listener->m_thread, &result);

        if(rc == DDS_RETCODE_OK) {
            listener->m_run_state = SHM_LISTENER_NOT_RUNNING;
        }
    }
    //note: we must ensure no readers are actively using the waitset anymore,
    //the listener and thus the waitset is to be destroyed after all readers are destroyed
    iox_ws_deinit(listener->m_waitset);
    ddsrt_mutex_destroy(&listener->m_lock);
}

dds_return_t shm_listener_wake(shm_listener_t* listener) {
    iox_user_trigger_trigger(listener->m_wakeup_trigger);
    return DDS_RETCODE_OK;
}

dds_return_t shm_listener_attach_reader(shm_listener_t* listener, struct dds_reader* reader) {
    //using the pointer is the fastest way and should be safe without deferred attach/detach  
    dds_entity_t handle = reader->m_entity.m_hdllink.hdl;
    uint64_t reader_id = (uint64_t) handle; 
    if(iox_ws_attach_subscriber_event(listener->m_waitset, reader->m_sub, SubscriberEvent_HAS_DATA, reader_id, NULL) !=WaitSetResult_SUCCESS) {  
        printf("error attaching reader %p\n", reader);      
        return DDS_RETCODE_OUT_OF_RESOURCES;
    }
    ++listener->m_number_of_attached_readers;

    printf("attached reader %p with id %ld\n", reader, reader_id);
    return DDS_RETCODE_OK;
}

dds_return_t shm_listener_detach_reader(shm_listener_t* listener, struct dds_reader* reader) {
    iox_ws_detach_subscriber_event(listener->m_waitset, reader->m_sub, SubscriberEvent_HAS_DATA);
    --listener->m_number_of_attached_readers;

    printf("detached reader %p\n", reader);
    return DDS_RETCODE_OK;
}

dds_return_t shm_listener_deferred_attach_reader(shm_listener_t* listener, struct dds_reader* reader) {
    ddsrt_mutex_lock(&listener->m_lock);

    //store the attach request
    for(int32_t i=0; i<SHM_MAX_NUMBER_OF_READERS; ++i) {
        if(listener->m_readers_to_attach[i] == NULL) {
            listener->m_readers_to_attach[i] = reader;
            ++listener->m_number_of_modifications_pending;
            shm_listener_wake(listener);

            ddsrt_mutex_unlock(&listener->m_lock);
            return DDS_RETCODE_OK;
        }
    }

    ddsrt_mutex_unlock(&listener->m_lock);   
    return DDS_RETCODE_OUT_OF_RESOURCES;
}

dds_return_t shm_listener_deferred_detach_reader(shm_listener_t* listener, struct dds_reader* reader) {
    ddsrt_mutex_lock(&listener->m_lock);

    for(int32_t i=0; i<SHM_MAX_NUMBER_OF_READERS; ++i) {
        if(listener->m_readers_to_attach[i] == reader) {
            listener->m_readers_to_attach[i] = NULL;

            ddsrt_mutex_unlock(&listener->m_lock);           
            return DDS_RETCODE_OK; //not attached yet, but we do not need to attach and then detach it
        }
    }
    // it must have been already attached, store the detach request
    for(int32_t i=0; i<SHM_MAX_NUMBER_OF_READERS; ++i) {
        if(listener->m_readers_to_detach[i] == NULL) {
            listener->m_readers_to_detach[i] = reader;
            ++listener->m_number_of_modifications_pending;
            shm_listener_wake(listener);

            ddsrt_mutex_unlock(&listener->m_lock);
            return DDS_RETCODE_OK;
        }
    }

    ddsrt_mutex_unlock(&listener->m_lock);    
    return DDS_RETCODE_OUT_OF_RESOURCES;
}

dds_return_t shm_listener_perform_deferred_modifications(shm_listener_t* listener) {
    ddsrt_mutex_lock(&listener->m_lock);
    //problem: we have a potential races: some of these readers may not even exist anymore
    //         but due to limitations of the waitset we also cannot attach/detach them while waiting
    //         (which will happen often)
    
    if(listener->m_number_of_modifications_pending == 0) {
        ddsrt_mutex_unlock(&listener->m_lock);
        return DDS_RETCODE_OK;
    }
    
    dds_return_t rc = DDS_RETCODE_OK;
    for(int32_t i=0; i<SHM_MAX_NUMBER_OF_READERS; ++i) {
        if(listener->m_readers_to_attach[i] != NULL) {
            if(shm_listener_attach_reader(listener, listener->m_readers_to_attach[i]) != DDS_RETCODE_OK) {
                rc = DDS_RETCODE_OUT_OF_RESOURCES;
            }
            listener->m_readers_to_attach[i] = NULL;
        }
        //note we cannot have the same pointer in attach AND detach requests (ensured by construction)
        if(listener->m_readers_to_detach[i] != NULL) {
            shm_listener_detach_reader(listener, listener->m_readers_to_detach[i]);
            listener->m_readers_to_detach[i] = NULL;
        }
    }
    listener->m_number_of_modifications_pending = 0;

    ddsrt_mutex_unlock(&listener->m_lock);
    return rc;
}


uint32_t shm_listener_monitor_thread(void* arg) {
    shm_listener_t* listener = arg; 
    uint64_t number_of_missed_events = 0;
    uint64_t number_of_events = 0;
    iox_event_info_t events[SHM_MAX_NUMBER_OF_READERS];

    //TODO: do we need better start/stop logic with restart capability?
    while(listener->m_run_state == SHM_LISTENER_RUN) {

        number_of_events = iox_ws_wait(listener->m_waitset, events, SHM_MAX_NUMBER_OF_READERS,
                                       &number_of_missed_events);

        //should not happen as the waitset is designed is configured here
        assert(number_of_missed_events == 0);

        //we woke up either due to termination request, modification request or
        //because some reader got data
        //check all the events and handle them accordingly

        for (uint64_t i = 0; i < number_of_events; ++i) {
            iox_event_info_t event = events[i];
            if (iox_event_info_does_originate_from_user_trigger(event, listener->m_wakeup_trigger))
            {
                // if(listener->m_run_state != SHM_LISTENER_RUN) {
                //     break;
                // }
                //TODO: I sense a subtle race here in the waitset usage (by design)
                //      which may cause aus to miss wake ups
                iox_user_trigger_reset_trigger(listener->m_wakeup_trigger);
                //do we have to do something or terminate?
                shm_listener_perform_deferred_modifications(listener);
                printf("shm listener woke up\n");
            } else {
                //some reader got data, identify the reader
                uint64_t reader_id = iox_event_info_get_event_id(event);                             
                dds_entity_t handle = (dds_entity_t) reader_id;
                dds_entity* entity;             

                //TODO: this is potentially costly, we may be able to use the pointer directly
                //      when it is used differently (listener vs. waitset, concurrent execution of
                //      chunk receive handling)
                dds_entity_pin(handle, &entity);

                if(!entity) {
                    printf("pinning reader %ld failed\n", reader_id);
                    continue; //pinning failed (?)
                }

                dds_reader* reader = (dds_reader*) entity;

                printf("reader %ld received data\n", reader_id);
                const void* chunk;
                while(iox_sub_take_chunk(reader->m_sub, &chunk) == ChunkReceiveResult_SUCCESS) {
                    //handle received chunk
                    //TODO: refactor the "callback"/handler
                    read_callback(chunk, reader);
                }
            }
        }

        //now that we woke up and performed the required actions
        //we will check for termination request and if there is none wait again
    }

    printf("shm listener thread stopped\n");
    return 0;
}

#if defined (__cplusplus)
}
#endif
