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

#if defined (__cplusplus)
extern "C" {
#endif

void shm_listener_init(shm_listener_t* listener) {
    listener->m_waitset = iox_ws_init(&listener->m_waitset_storage);
    iox_ws_attach_user_trigger_event(listener->m_waitset, listener->m_trigger, 0U, NULL);
}

void shm_listener_attach_reader(shm_listener_t* listener, struct dds_reader* reader) {
    uint32_t reader_id = reader->m_entity.m_guid.entityid.u;
    iox_ws_attach_subscriber_event(listener->m_waitset, reader->m_sub, SubscriberEvent_HAS_DATA, reader_id, NULL);
}

void shm_listener_detach_reader(shm_listener_t* listener, struct dds_reader* reader) {
    iox_ws_detach_subscriber_event(listener->m_waitset, reader->m_sub, SubscriberEvent_HAS_DATA);
}

#if defined (__cplusplus)
}
#endif
