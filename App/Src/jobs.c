/******************************************************************************
 * jobs.h
 *
 * Job queue management routines
 ******************************************************************************/

#include <stdint.h>
#include <string.h>

// Don't really need most of the stuff included by device_config.h for this code,
// but do need access to the SAVE_AND_DISABLE_INTERRUPTS() and
// RESTORE_INTERRUPTS() macros declared in "platform.h"
#include "device_config.h"              // Includes debug_config.h, main.h, platform.h
#include "jobs.h"

//-------------------------------------------------------------------------------

// Default job queue - will be used when p_x_queue parameter for job functions is NULL

#if DEFAULT_JOB_QUEUE_SIZE > 0
job_queue_t x_default_job_queue;
job_t x_default_jobs[DEFAULT_JOB_QUEUE_SIZE];
#endif

/******************************************************************************
 *
 ******************************************************************************/

void v_job_queue_init(job_queue_t *p_x_queue, job_t *p_x_job_buffer, uint8_t u8_size)
{
    if (p_x_queue == NULL)
    {
    #if DEFAULT_JOB_QUEUE_SIZE > 0
        p_x_queue = &x_default_job_queue;
        p_x_job_buffer = x_default_jobs;
        u8_size = DEFAULT_JOB_QUEUE_SIZE;
    #else
        return;
    #endif
    }

    memset(p_x_queue, 0, sizeof(job_queue_t));
    memset(p_x_job_buffer, 0, u8_size * sizeof(job_t));

    p_x_queue->p_x_job_buffer = p_x_job_buffer;
    p_x_queue->u8_size = u8_size;
}

/******************************************************************************
 *
 ******************************************************************************/

void v_job_add(job_queue_t *p_x_queue, uint8_t u8_job_id)
{
    if (p_x_queue == NULL)
    {
    #if DEFAULT_JOB_QUEUE_SIZE > 0
        p_x_queue = &x_default_job_queue;
    #else
        return;
    #endif
    }

    // Don't allow job addition if queue has not been initialized

    if (p_x_queue->u8_size == 0)
    {
        return;
    }

    SAVE_AND_DISABLE_INTERRUPTS();

    if (! p_x_queue->u8_full)
    {
        job_t *p_x_job = &(p_x_queue->p_x_job_buffer[p_x_queue->u8_head]);
        p_x_job->u8_id = u8_job_id;
        p_x_job->u8_param1 = 0;
        p_x_job->u16_param2 = 0;
        p_x_queue->u8_head++;
        if (p_x_queue->u8_head >= p_x_queue->u8_size)
        {
            p_x_queue->u8_head = 0;
        }
        if (p_x_queue->u8_head == p_x_queue->u8_tail)
        {
            p_x_queue->u8_full = 1;
        }
    }
    else
    {
        // Job queue overflow
        if (p_x_queue->u8_full < 0xFF)
        {
            p_x_queue->u8_full++;
        }
    }

    RESTORE_INTERRUPTS();
}

/******************************************************************************
 *
 ******************************************************************************/

void v_job_add_with_params(job_queue_t *p_x_queue, uint8_t u8_job_id, uint8_t u8_param1, uint16_t u16_param2)
{
    if (p_x_queue == NULL)
    {
    #if DEFAULT_JOB_QUEUE_SIZE > 0
        p_x_queue = &x_default_job_queue;
    #else
        return;
    #endif
    }

    // Don't allow job addition if queue has not been initialized

    if (p_x_queue->u8_size == 0)
    {
        return;
    }

    SAVE_AND_DISABLE_INTERRUPTS();

    if (! p_x_queue->u8_full)
    {
        job_t *p_x_job = &(p_x_queue->p_x_job_buffer[p_x_queue->u8_head]);
        p_x_job->u8_id = u8_job_id;
        p_x_job->u8_param1 = u8_param1;
        p_x_job->u16_param2 = u16_param2;
        p_x_queue->u8_head++;
        if (p_x_queue->u8_head >= p_x_queue->u8_size)
        {
            p_x_queue->u8_head = 0;
        }
        if (p_x_queue->u8_head == p_x_queue->u8_tail)
        {
            p_x_queue->u8_full = 1;
        }
    }
    else
    {
        // Job queue overflow
        if (p_x_queue->u8_full < 0xFF)
        {
            p_x_queue->u8_full++;
        }
    }

    RESTORE_INTERRUPTS();
}

/******************************************************************************
 * u8_job_get(*p_x_queue, *p_x_job)
 *
 * Fetch a job descriptor from the job queue
 *
 * p_x_quque    Pointer to job queue to fetch job from.
 *              If this is NULL, the built-in/default job queue will be used.
 * p_x_job      Pointer to a job_t struct instance where the job details pulled
 *              from the queue will be copied.
 *              This can be NULL, in which case the oldest job from the
 *              p_x_queue will be pulled and discarded.
 *
 * Returns:     0 if the p_x_queue job queue was empty
 *                  *p_x_job will not be modified in this case
 *              1 if there was a job present in the p_x_queue
 *              2 if the job queue overflowed
 *                  p_x_job->u8_id will be set to JOB_QUEUE_OVERFLOW
 *                  p_x_job->u8_param1 will be set to the number of jobs lost
 ******************************************************************************/

uint8_t u8_job_get(job_queue_t *p_x_queue, job_t *p_x_job)
{
    uint8_t u8_job_available = 0;

    // Use default/built-in queue if none provided by caller

    if (p_x_queue == NULL)
    {
    #if DEFAULT_JOB_QUEUE_SIZE > 0
        p_x_queue = &x_default_job_queue;
    #else
        return 0;
    #endif
    }

    // Block ISRs (that might try to add jobs to the queue) from running during
    // the job queue fetch operation

    SAVE_AND_DISABLE_INTERRUPTS();

    // If the job queue overflowed, return a JOB_QUEUE_FULL job ID.
    // The u8_param1 field of the job struct will be set to the number of jobs
    // lost due to queue overflow.
    // Function return value will be 2 in this case.

    if (p_x_queue->u8_full > 1)     // u8_full > 1 if queue overflowed; counts # jobs lost
    {
        if (p_x_job != NULL)
        {
            p_x_job->u8_id = JOB_QUEUE_OVERFLOW;
            p_x_job->u8_param1 = p_x_queue->u8_full - 1;
            p_x_job->u16_param2 = 0;
        }
        // After reporting an overflow, the job queue overflow status/count can be
        // cleared. However, the queue is still full, so keep the full flag set
        p_x_queue->u8_full = 1;     // Overflow reported, clear overflow status
        u8_job_available = 2;       // Return value 2 indicates overflow occurred
    }

    // Normal job queue fetch
    // Pull a job from the back of the queue if one is available

    else if ( p_x_queue->u8_full ||
              (p_x_queue->u8_head != p_x_queue->u8_tail) )
    {
        if (p_x_job)
        {
            *p_x_job = p_x_queue->p_x_job_buffer[p_x_queue->u8_tail];
        }

        p_x_queue->u8_tail++;
        if (p_x_queue->u8_tail >= p_x_queue->u8_size)
        {
            p_x_queue->u8_tail = 0;
        }
        p_x_queue->u8_full = 0;

        u8_job_available = 1;
    }

    else
    {
        // If the job queue is empty, there is nothing to do
        // The p_x_job struct pointer provided by the caller will not be
        // modified in this case.
    }

    // Job queue manipulation complete, OK for ISRs to run

    RESTORE_INTERRUPTS();

    return u8_job_available;
}

/******************************************************************************
 *
 ******************************************************************************/

void v_job_queue_clear(job_queue_t *p_x_queue)
{
    if (p_x_queue == NULL)
    {
#if DEFAULT_JOB_QUEUE_SIZE > 0
        p_x_queue = &x_default_job_queue;
#else
        return;
#endif
    }

    SAVE_AND_DISABLE_INTERRUPTS();

    p_x_queue->u8_head = 0;
    p_x_queue->u8_tail = 0;
    p_x_queue->u8_full = 0;

    RESTORE_INTERRUPTS();
}
