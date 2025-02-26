/* bsdiff test
 */
#include <bsdiff.h>
#include <bspatch.h>
#include <fcntl.h>
#include <sdkconfig.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unity.h>

#define min(A, B) ((A) < (B) ? (A) : (B))

static int _w(struct bsdiff_stream* stream, const void* buffer, int size)
{
    if (fwrite(buffer, size, 1, (FILE*)stream->opaque) != 1) {
        return -1;
    }
    return 0;
}

static int bsdiff_f(char* oldf, char* newf, char* patchf)
{
    int fd;
    uint8_t *old, *new;
    off_t oldsize, newsize;
    FILE* pf;
    struct bsdiff_stream stream;

    stream.malloc = malloc;
    stream.free = free;
    stream.write = _w;

    if (((fd = open(oldf, O_RDONLY, 0)) < 0) || ((oldsize = lseek(fd, 0, SEEK_END)) == -1)
        || ((old = malloc(oldsize + 1)) == NULL) || (lseek(fd, 0, SEEK_SET) != 0) || (read(fd, old, oldsize) != oldsize)
        || (close(fd) == -1)) {
        return -1;
    }

    if (((fd = open(newf, O_RDONLY, 0)) < 0) || ((newsize = lseek(fd, 0, SEEK_END)) == -1)
        || ((new = malloc(newsize + 1)) == NULL) || (lseek(fd, 0, SEEK_SET) != 0) || (read(fd, new, newsize) != newsize)
        || (close(fd) == -1)) {
        return -1;
    }

    if ((pf = fopen(patchf, "w")) == NULL) {
        return -1;
    }

    stream.opaque = pf;
    if (bsdiff(old, oldsize, new, newsize, &stream)) {
        return -1;
    }

    if (fclose(pf)) {
        return -1;
    }

    free(old);
    free(new);

    return newsize;
}

struct NewCtx {
    uint8_t* new;
    int pos_write;
};

struct OldCtx {
	uint8_t* old;
	int oldsize;
};


static int _or(const struct bspatch_stream_i* stream, void* buffer, int pos, int length)
{
	struct OldCtx* old_ctx = (struct OldCtx*)stream->opaque;
	if (pos >= old_ctx->oldsize) {
		return -1;
	} else if (pos + length > old_ctx->oldsize) {
		return -2;
	}
	memcpy(buffer, old_ctx->old + pos, length);
	return 0;
}

static int _nw(const struct bspatch_stream_n* stream, const void* buffer, int length)
{
    struct NewCtx* new;
    new = (struct NewCtx*)stream->opaque;
    memcpy(new->new + new->pos_write, buffer, length);
    new->pos_write += length;
    return 0;
}

static int bspatch_f(char* oldf, char* newf, size_t newfs, char* patchf)
{
    int fd;
    uint8_t *old, *new, *patch;
    int64_t oldsize;
    int64_t patchsize;
    struct bspatch_stream_i oldstream;
    struct bspatch_stream_n newstream;
    struct stat sb;

    if (((fd = open(patchf, O_RDONLY, 0)) < 0) || ((patchsize = lseek(fd, 0, SEEK_END)) == -1)
        || ((patch = malloc(patchsize + 1)) == NULL) || (lseek(fd, 0, SEEK_SET) != 0) || (read(fd, patch, patchsize) != patchsize)
        || (fstat(fd, &sb)) || (close(fd) == -1)) {
        return -1;
    }

    if (((fd = open(oldf, O_RDONLY, 0)) < 0) || ((oldsize = lseek(fd, 0, SEEK_END)) == -1)
        || ((old = malloc(oldsize + 1)) == NULL) || (lseek(fd, 0, SEEK_SET) != 0) || (read(fd, old, oldsize) != oldsize)
        || (fstat(fd, &sb)) || (close(fd) == -1)) {
        return -1;
    }

    if (((fd = open(patchf, O_RDONLY, 0)) < 0)
	|| ((patchsize = lseek(fd, 0, SEEK_END)) == -1)
	|| (lseek(fd, 0, SEEK_SET) != 0)
	|| (close(fd) == -1)) {
	    return -1;
    }

    if ((new = malloc(newfs + 1)) == NULL) {
        return -1;
    }

    struct OldCtx old_ctx = { .old = old, .oldsize = oldsize };

    oldstream.read = _or;
    newstream.write = _nw;
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
		    return patch_result;
	    }
	    patch_remaining -= patch_chunk_sz;
    }

    if (((fd = open(newf, O_CREAT | O_TRUNC | O_WRONLY, sb.st_mode)) < 0) || (write(fd, new, newfs) != newfs)
        || (close(fd) == -1)) {
        return -1;
    }

    free(new);
    free(old);

    return 0;
}

static int cmp(char* filename1, char* filename2)
{
    FILE* file1 = fopen(filename1, "rb");
    FILE* file2 = fopen(filename2, "rb");


    int ret = -1;

    int k, t;
    while (true) {
        t = getc(file1);
        k = getc(file2);
        if (t != k || k == EOF) {
            break;
        }
    }

    if (k == t) {
        ret = 0;
    }

    fclose(file1);
    fclose(file2);

    return ret;
}

void test_bsdiff_different_files(void)
{
    /* create the patch from two well known files */
    const int newsize = bsdiff_f("main/test_bsdiff.c", "main/CMakeLists.txt", "build/test_patch.bin");
    TEST_ASSERT_GREATER_THAN(1, newsize);
    /* apply the patch */
    const int bspatch_result = bspatch_f("main/test_bsdiff.c", "build/CMakeLists.txt", newsize, "build/test_patch.bin");
    TEST_ASSERT_EQUAL(0, bspatch_result);
    /* verify the new file is the same as the original */
    const int cmp_result = cmp("main/CMakeLists.txt", "build/CMakeLists.txt");
    TEST_ASSERT_EQUAL(0, cmp_result);
}

void test_bsdiff_same_file(void)
{
    /* create the patch from two well known files */
    const int newsize = bsdiff_f("main/test_bsdiff.c", "main/test_bsdiff.c", "build/test_patch.bin");
    TEST_ASSERT_GREATER_THAN(1, newsize);
    /* apply the patch */
    const int bspatch_result = bspatch_f("main/test_bsdiff.c", "build/test_bsdiff.c", newsize, "build/test_patch.bin");
    TEST_ASSERT_EQUAL(0, bspatch_result);
    /* verify the new file is the same as the original */
    const int cmp_result = cmp("main/test_bsdiff.c", "build/test_bsdiff.c");
    TEST_ASSERT_EQUAL(0, cmp_result);
}

void test_bsdiff_same_file_wrong(void)
{
    const int cmp_result = cmp("main/test_bsdiff.c", "main/CMakeLists.txt");
    TEST_ASSERT_NOT_EQUAL(0, cmp_result);
}

void test_bsdiff_different_files_oldwrong(void)
{
    /* create the patch from two well known files */
    const int newsize = bsdiff_f("main/test_bsdiff.c", "main/CMakeLists.txt", "build/test_patch.bin");
    TEST_ASSERT_GREATER_THAN(1, newsize);
    /* apply the patch on top of a bad old file*/
    const int bspatch_result = bspatch_f("main/CMakeLists.c", "build/CMakeLists.txt", newsize, "build/test_patch.bin");
    TEST_ASSERT_NOT_EQUAL(0, bspatch_result);
}

void test_bsdiff_different_files_missingfile(void)
{
    /* create the patch from two well known files */
    const int res = bsdiff_f("main/test_bsdiffMISSING.c", "main/CMakeLists.txt", "build/test_patch.bin");
    TEST_ASSERT_EQUAL(-1, res);
    const int res2 = bsdiff_f("main/test_bsdiff.c", "main/CMakeListsMISSING.txt", "build/test_patch.bin");
    TEST_ASSERT_EQUAL(-1, res2);
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_bsdiff_different_files);
    RUN_TEST(test_bsdiff_same_file);
    RUN_TEST(test_bsdiff_same_file_wrong);
    RUN_TEST(test_bsdiff_different_files_oldwrong);
    RUN_TEST(test_bsdiff_different_files_missingfile);
    int failures = UNITY_END();
    return failures;
}
