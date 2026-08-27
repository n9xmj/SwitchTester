/******************************************************************************
 * event_queue_config.h
 *
 * SwitchTester's configuration for the vendored event_queue module.
 *
 * Derived from App/event_queue/event_queue_config.h.example -- see that file
 * for the full commentary on each setting.
 *
 * DO NOT #include THIS FILE DIRECTLY. event_queue.h includes it for you, and
 * is the only header the application needs.
 ******************************************************************************/

#ifndef EVENT_QUEUE_H_INSIDE
#error "Do not include event_queue_config.h directly -- include event_queue.h instead."
#endif

#ifndef EVENT_QUEUE_CONFIG_H
#define EVENT_QUEUE_CONFIG_H

/* This project lets the module allocate ring buffers on request (a NULL
 * pv_buffer in the create config), through the default C-library malloc/free.
 * EVENT_QUEUE_MALLOC / EVENT_QUEUE_FREE are deliberately left undefined --
 * see the .example header for the alternate-allocator seam. */
#define EVENT_QUEUE_ENABLE_MALLOC       1

/* Default ring size for a zeroed/NULL create config. */
#define EVENT_QUEUE_DEFAULT_SIZE        256

#endif /* EVENT_QUEUE_CONFIG_H */
