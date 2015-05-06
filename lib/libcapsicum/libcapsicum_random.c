/*-
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <nv.h>

#include "libcapsicum.h"
#include "libcapsicum_random.h"

#define	MAXSIZE	(1024 * 1024)

int
cap_random_buf(cap_channel_t *chan, void *buf, size_t nbytes)
{
	nvlist_t *nvl;
	const void *randbuf;
	uint8_t *ptr;
	size_t left, randbufsize;

	left = nbytes;
	ptr = buf;

	while (left > 0) {
		nvl = nvlist_create(0);
		nvlist_add_string(nvl, "cmd", "generate");
		nvlist_add_number(nvl, "size",
		    (uint64_t)(left > MAXSIZE ? MAXSIZE : left));
		nvl = cap_xfer_nvlist(chan, nvl, 0);
		if (nvl == NULL)
			return (-1);
		if (nvlist_get_number(nvl, "error") != 0) {
			errno = (int)nvlist_get_number(nvl, "error");
			nvlist_destroy(nvl);
			return (-1);
		}

		randbuf = nvlist_get_binary(nvl, "data", &randbufsize);
		memcpy(ptr, randbuf, randbufsize);

		nvlist_destroy(nvl);

		ptr += randbufsize;
		assert(left >= randbufsize);
		left -= randbufsize;
	}

	return (0);
}
