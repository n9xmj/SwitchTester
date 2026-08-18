/******************************************************************************
 * nvm_list.h
 *
 * Diagnostic dump of an nvmparams pool -- header fields plus a hex listing of
 * every object.
 *
 * NOT PART OF THE nvmparams MODULE. This is a debugging aid that ships
 * alongside it, and it is the one facility whose header an application does
 * have to include separately. That is deliberate: a release build has no
 * reason to carry it, and leaving it out of nvmparams.h means the module keeps
 * its C-library-only dependency list. This file is the only thing in the
 * directory that touches <stdio.h>.
 *
 * To use it, rename nvm_list.c.example to nvm_list.c, add it to your build,
 * and include this header where you call it. To drop it, delete both files --
 * nothing in the module refers to them.
 *
 * Written entirely against the public nvmparams.h, which is also the point: if
 * a diagnostic dumper can be built from the published API alone, that API
 * exposes enough for a client to do real work.
 ******************************************************************************/

#ifndef NVM_LIST_H
#define NVM_LIST_H

#include "nvmparams.h"

/*----------------------------------------------------------------------------
 * Print the pool header and a listing of every object to stdout.
 *
 * p_x_pool     Pool to dump. Must have been initialised.
 *
 * Returns:     NVM_ERROR_NONE, or NVM_ERROR_PARAMETER for an unusable pool.
 *
 * Output is several lines and is written with printf(), so call it from a
 * human-facing context -- a debug menu -- rather than from an ISR or from the
 * middle of a machine-readable console exchange.
 *--------------------------------------------------------------------------*/

extern nvm_error_t x_nvm_list(nvm_pool_t *p_x_pool);

#endif /* NVM_LIST_H */
