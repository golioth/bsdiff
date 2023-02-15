/*-
 * Copyright 2003-2005 Colin Percival
 * Copyright 2012 Matthew Endsley
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BSPATCH_H
#define BSPATCH_H

#include <stdint.h>

#ifndef BSPATCH_BUF_SIZE
#define BSPATCH_BUF_SIZE 256
#endif

#ifndef BSPATCH_DEBUG
#define BSPATCH_DEBUG(...) //printf(__VA_ARGS__)
#endif

struct bspatch_stream_i
{
	void* opaque;
	int (*read)(const struct bspatch_stream_i* stream, void* buffer, int pos, int length);
};

struct bspatch_stream_n
{
	void* opaque;
	int (*write)(const struct bspatch_stream_n* stream, const void *buffer, int length);
};

enum bspatch_state {
	BSPATCH_STATE_RESET,
	BSPATCH_STATE_RD_CTRL,
	BSPATCH_STATE_RD_DIFF,
	BSPATCH_STATE_RD_EXTRA,
};

struct bspatch_ctx
{
	enum bspatch_state state;
	int64_t ctrl[3];
	uint8_t buf[BSPATCH_BUF_SIZE];
	uint32_t buf_offset;
	uint32_t diff_offset;
	uint32_t extra_offset;
	int oldpos;

};

#define BSPATCH_SUCCESS (0)
#define BSPATCH_ERROR (-1)
/*
 * Processes patch_size bytes of patch, reading from old stream and writing to new stream
 * in the process.
 *
 * You can call this function multiple times with sequential chunks of a patch.
 * In other words, you don't need to pass in the full patch contents, just pass in however
 * many patch bytes you have (even if just 1 byte).
 *
 * Returns BSPATCH_SUCCESS on success, all patch bytes processed successfully
 * Returns BSPATCH_ERROR on error in patching logic
 * Returns any <0 return code from stream read() and write() functions (which imply error)
 */
int bspatch(struct bspatch_ctx* ctx,
	    struct bspatch_stream_i *old,
	    struct bspatch_stream_n *new,
	    const uint8_t* patch,
	    int patch_size);

#endif
