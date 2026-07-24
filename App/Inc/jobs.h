/******************************************************************************
 * jobs.h
 *
 * Job queue management routines
 ******************************************************************************/

#ifndef JOBS_H
#define JOBS_H

#include <stdint.h>

// Individual job entry

typedef struct
{
    uint8_t     u8_id;
    uint8_t     u8_param1;
    uint16_t    u16_param2;
}
job_t;

// Job queue management data

typedef struct
{
    job_t       *p_x_job_buffer;        // Pointer to job queue buffer (job_t x_jobs[n] array)
    uint8_t     u8_size;                // Size of job queue (# elements, not sizeof())
    uint8_t     u8_head;                // Index of next free job queue slot (new jobs added here)
    uint8_t     u8_tail;                // Index of next job to pull from queue
    uint8_t     u8_full;                // Set (nonzero) if job queue is full
    // Note: The u8_full value will be 0 if the queue is not full, 1 if it is
    // full (but not overflowed), and >= 2 if overflowed. The number of jobs
    // lost due to overflow will be (u8_full - 1).
}
job_queue_t;

// This enum provides identifiers that can be assigned to the u8_id field in a job_t entry.
// These are application-specific; modify the entries here to suit application needs.

enum
{
    JOB_NONE,

    // --- Add application-specific job/task ID's here ---

    JOB_BUTTON,
    JOB_PERIODIC,
    JOB_TEST,
    JOB_FAULT,
    JOB_PENDULUM_RUN,
    JOB_EXTI_REPORT,
    JOB_NVM_COMMIT,

    // ---------------------------------------------------

    // JOB_QUEUE_OVERFLOW must be declared/present
    // This job ID will be returned by u8_job_get if the job queue overflowed.
    // u8_param1 will be set to the number of jobs lost due to overflow.
    // The application can choose to ignore this job ID if it wants.
    JOB_QUEUE_OVERFLOW,

    JOB_ID_MAX,                         // Highest job ID in use +1
    JOB_ID_MAXVAL = 0xFF                // Maximum possible job ID
};

// Controls the size (# job entries) of the default job queue, which is allocated
// in and local to jobs.c
// Set this to 0 to disable creation and use of the default job queue

#define DEFAULT_JOB_QUEUE_SIZE          50

//------------------------------------------------------------------------------

extern void v_job_queue_init(job_queue_t *p_x_queue, job_t *p_x_job_buffer, uint8_t u8_size);
extern void v_job_add(job_queue_t *p_x_queue, uint8_t u8_job_id);
extern void v_job_add_with_params(job_queue_t *p_x_queue, uint8_t u8_job_id, uint8_t u8_param1, uint16_t u16_param2);
extern uint8_t u8_job_get(job_queue_t *p_x_queue, job_t *p_x_job);
extern void v_job_queue_clear(job_queue_t *p_x_queue);

#endif
