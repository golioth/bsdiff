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

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "bspatch.h"

#define RETURN_IF_NEGATIVE(expr)        \
    do                                  \
    {                                   \
        const int ret = (expr);         \
        if (ret < 0) {                      \
            BSPATCH_DEBUG("Error on line %d\n", __LINE__); \
            return ret;                 \
        }                               \
    } while(0)

#define min(A, B) ((A) < (B) ? (A) : (B))

static int64_t offtin(uint8_t *buf)
{
	int64_t y;

	y=buf[7]&0x7F;
	y=y*256;y+=buf[6];
	y=y*256;y+=buf[5];
	y=y*256;y+=buf[4];
	y=y*256;y+=buf[3];
	y=y*256;y+=buf[2];
	y=y*256;y+=buf[1];
	y=y*256;y+=buf[0];

	if(buf[7]&0x80) y=-y;

	return y;
}

int bspatch(struct bspatch_ctx* ctx,
	    struct bspatch_stream_i *old,
	    struct bspatch_stream_n *new,
	    const uint8_t* patch,
	    int patch_size)
{
	const int64_t half_len = BSPATCH_BUF_SIZE / 2;

	int patch_remaining = patch_size;
	while (patch_remaining > 0) {
		BSPATCH_DEBUG("patch remaining: %d\n", patch_remaining);
		int patch_offset = patch_size - patch_remaining;

		switch (ctx->state) {
			case BSPATCH_STATE_RESET:
			{
				/* Reset everything except oldpos, which needs to persist across
				 * control blocks */
				int oldpos = ctx->oldpos;
				memset(ctx, 0, sizeof(*ctx));
				ctx->oldpos = oldpos;
				ctx->state = BSPATCH_STATE_RD_CTRL;
				break;
			}

			case BSPATCH_STATE_RD_CTRL:
			{
				/*
				 * Read 3 control data words (each 8 bytes).
				 *
				 * Let X = ctrl[0], Y = ctrl[1], and Z = ctrl[2]
				 *
				 * The patch algorithm will:
				 *
				 *    1. Add X bytes from old to X bytes from patch and write the
				 *       resulting X bytes to new.
				 *    2. Read Y bytes from patch and write them to new.
				 *    3. Seek forward Z bytes in old (might be negative).
				 */
				int ctrl_remaining = 24 - ctx->buf_offset;
				assert(ctrl_remaining >= 0);
				if (ctrl_remaining == 0) {
					ctx->ctrl[0]=offtin(&ctx->buf[0]);
					ctx->ctrl[1]=offtin(&ctx->buf[8]);
					ctx->ctrl[2]=offtin(&ctx->buf[16]);

					/* Sanity-check */
					if (ctx->ctrl[0]<0 || ctx->ctrl[0]>INT_MAX || ctx->ctrl[1]<0 || ctx->ctrl[1]>INT_MAX) {
						BSPATCH_DEBUG("Failed sanity check: %ld %ld\n", ctx->ctrl[0], ctx->ctrl[1]);
						return BSPATCH_ERROR;
					}
					BSPATCH_DEBUG("ctrl[0] = %ld\n", ctx->ctrl[0]);
					BSPATCH_DEBUG("ctrl[1] = %ld\n", ctx->ctrl[1]);
					BSPATCH_DEBUG("ctrl[2] = %ld\n", ctx->ctrl[2]);

					/* Go to next state */
					BSPATCH_DEBUG("New state: BSPATCH_STATE_RD_DIFF\n");
					ctx->state = BSPATCH_STATE_RD_DIFF;
					break;
				}

				int ctrl_to_read = min(24, ctrl_remaining);
				ctrl_to_read = min(ctrl_to_read, patch_remaining);
				BSPATCH_DEBUG("ctrl read %d\n", ctrl_to_read);
				memcpy(ctx->buf + ctx->buf_offset, patch + patch_offset, ctrl_to_read);
				ctx->buf_offset += ctrl_to_read;
				patch_remaining -= ctrl_to_read;

				break;
			}

			case BSPATCH_STATE_RD_DIFF:
			{
				int diff_remaining = ctx->ctrl[0] - ctx->diff_offset;
				assert(diff_remaining >= 0);
				if (diff_remaining == 0) {
					/* Adjust pointers */
					ctx->oldpos+=ctx->ctrl[0];
					/* Go to next state */
					BSPATCH_DEBUG("New state: BSPATCH_STATE_RD_EXTRA\n");
					ctx->state = BSPATCH_STATE_RD_EXTRA;
					break;
				}

				/* Read diff string and add old data on the fly */
				int diff_towrite = min(diff_remaining, half_len);
				diff_towrite = min(diff_towrite, patch_remaining);
				BSPATCH_DEBUG("diff read %d\n", diff_towrite);
				memcpy(&ctx->buf[half_len], patch + patch_offset, diff_towrite);
				RETURN_IF_NEGATIVE(old->read(old, ctx->buf, ctx->oldpos + ctx->diff_offset, diff_towrite));
				ctx->diff_offset += diff_towrite;
				patch_remaining -= diff_towrite;

				for(int k=0;k<diff_towrite;k++) {
					ctx->buf[k + half_len] += ctx->buf[k];
				}

				BSPATCH_DEBUG("diff write %d\n", diff_towrite);
				RETURN_IF_NEGATIVE(new->write(new, &ctx->buf[half_len], diff_towrite));
				break;
			}

			case BSPATCH_STATE_RD_EXTRA:
			{
				int extra_remaining = ctx->ctrl[1] - ctx->extra_offset;
				assert(extra_remaining >= 0);
				if (extra_remaining == 0) {
					/* Adjust pointers */
					ctx->oldpos+=ctx->ctrl[2];
					/* Go to next state */
					BSPATCH_DEBUG("New state: BSPATCH_STATE_RESET\n");
					ctx->state = BSPATCH_STATE_RESET;
					break;
				}

				/* Read extra string and copy over to new on the fly*/
				int extra_towrite = min(extra_remaining, BSPATCH_BUF_SIZE);
				extra_towrite = min(extra_towrite, patch_remaining);
				BSPATCH_DEBUG("extra read %d\n", extra_towrite);
				memcpy(ctx->buf, patch + patch_offset, extra_towrite);
				RETURN_IF_NEGATIVE(new->write(new, ctx->buf, extra_towrite));
				ctx->extra_offset += extra_towrite;
				patch_remaining -= extra_towrite;
				break;
			}

			default:
				break;
		}
	};

	return BSPATCH_SUCCESS;
}

#if defined(BSPATCH_EXECUTABLE)

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

struct NewCtx {
	uint8_t* new;
	int pos_write;
};

struct OldCtx {
	uint8_t* old;
	int oldsize;
};


static int old_read(const struct bspatch_stream_i* stream, void* buffer, int pos, int length) {
	struct OldCtx* old_ctx = (struct OldCtx*)stream->opaque;
	if (pos >= old_ctx->oldsize) {
		return -1;
	} else if (pos + length > old_ctx->oldsize) {
		return -2;
	}
	memcpy(buffer, old_ctx->old + pos, length);
	return 0;
}

static int new_write(const struct bspatch_stream_n* stream, const void *buffer, int length) {
	struct NewCtx* new;
	new = (struct NewCtx*)stream->opaque;
	memcpy(new->new + new->pos_write, buffer, length);
	new->pos_write += length;
	return 0;
}

int main(int argc,char * argv[])
{
	int fd;
	uint8_t *old, *new, *patch;
	int64_t oldsize, newsize, patchsize;
	struct bspatch_stream_i oldstream;
	struct bspatch_stream_n newstream;
	struct stat sb;

	if(argc!=5) errx(1,"usage: %s oldfile newfile newsize patchfile\n",argv[0]);

	newsize = atoi(argv[3]);

	/* Read patch file */
	if(((fd=open(argv[4],O_RDONLY,0))<0) ||
		((patchsize=lseek(fd,0,SEEK_END))==-1) ||
		((patch=malloc(patchsize+1))==NULL) ||
		(lseek(fd,0,SEEK_SET)!=0) ||
		(read(fd,patch,patchsize)!=patchsize) ||
		(fstat(fd, &sb)) ||
		(close(fd)==-1)) err(1,"%s",argv[4]);

	/* Read old file */
	if(((fd=open(argv[1],O_RDONLY,0))<0) ||
		((oldsize=lseek(fd,0,SEEK_END))==-1) ||
		((old=malloc(oldsize+1))==NULL) ||
		(lseek(fd,0,SEEK_SET)!=0) ||
		(read(fd,old,oldsize)!=oldsize) ||
		(fstat(fd, &sb)) ||
		(close(fd)==-1)) err(1,"%s",argv[1]);

	/* Allocate buffer for new file */
	if((new=malloc(newsize+1))==NULL) err(1,NULL);

	struct OldCtx old_ctx = { .old = old, .oldsize = oldsize };

	oldstream.read = old_read;
	newstream.write = new_write;
	oldstream.opaque = &old_ctx;
	struct NewCtx ctx = { .pos_write = 0, .new = new };
	newstream.opaque = &ctx;

	struct bspatch_ctx bspatch_ctx = {};
	int patch_remaining = patchsize;
	while (patch_remaining) {
		int patch_offset = patchsize - patch_remaining;
		int patch_chunk_sz = min(patch_remaining, 512);
		BSPATCH_DEBUG("--------------\n");
		int patch_result = bspatch(&bspatch_ctx, &oldstream, &newstream, patch + patch_offset, patch_chunk_sz);
		if (patch_result < 0) {
			errx(patch_result, "bspatch");
			break;
		}
		patch_remaining -= patch_chunk_sz;
	}

	/* Write the new file */
	if(((fd=open(argv[2],O_CREAT|O_TRUNC|O_WRONLY,sb.st_mode))<0) ||
		(write(fd,new,newsize)!=newsize) || (close(fd)==-1))
		err(1,"%s",argv[2]);

	free(new);
	free(old);

	return 0;
}

#endif
