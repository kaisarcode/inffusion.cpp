/**
 * inffusion-pal - Platform helpers
 * Summary: Normalizes stdin and stdout handling across supported targets.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef INFUSSION_PAL_H
#define INFUSSION_PAL_H

#include <stdio.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/**
 * Prepares stdin and stdout for raw payload transport.
 * @return void
 */
static inline void inffusion_prepare_stdio(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

#endif
