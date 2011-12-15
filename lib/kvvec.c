/*
 * key+value vector library
 *
 * Small and simple, but pretty helpful when parsing configurations
 * from random formats into something a program can easily make sense
 * of.
 *
 * The main type (struct kvvec *) should possibly be opaque since
 * all callers should use the kvvec_foreach() variable to trudge
 * around in the key/value vector.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "kvvec.h"

struct kvvec *kvvec_init(int hint)
{
	struct kvvec *kvv;

	kvv = calloc(1, sizeof(*kvv));
	if (!kvv)
		return NULL;

	kvvec_grow(kvv, hint);
	return kvv;
}

int kvvec_grow(struct kvvec *kvv, int hint)
{
	struct key_value **kv;

	if (!kvv)
		return -1;
	if (hint < kvv->kv_alloc)
		return 0;

	kv = realloc(kvv->kv, sizeof(struct key_value *) * hint);
	if (!kv)
		return -1;

	memset(&kv[kvv->kv_alloc], 0, hint - kvv->kv_alloc);
	kvv->kv = kv;
	kvv->kv_alloc = hint;
	return 0;
}

int kvvec_addkv_wlen(struct kvvec *kvv, char *key, int keylen, char *value, int valuelen)
{
	struct key_value *kv;

	if (!key)
		return -1;

	if (kvv->kv_pairs >= kvv->kv_alloc - 1) {
		if (kvvec_grow(kvv, kvv->kv_pairs + 5))
			return -1;
	}

	kv = malloc(sizeof(struct key_value));
	kv->key = key;
	kv->key_len = keylen;
	kv->value = value;
	kv->value_len = valuelen;

	if (!keylen) {
		kv->key_len = strlen(key);
	}
	if (value) {
		if (!valuelen) {
			kv->value_len = strlen(value);
		}
	} else {
		kv->value_len = 0;
		kv->value = '\0';
	}
	kvv->kv[kvv->kv_pairs++] = kv;
	kvv->kvv_sorted = 0;

	return 0;
}

static int kv_compare(const void *a_, const void *b_)
{
	struct key_value *a = *(struct key_value **)a_;
	struct key_value *b = *(struct key_value **)b_;
	int ret;

	return strcmp(a->key, b->key);
	if (a->key_len != b->key_len) {
		return a->key_len - b->key_len;
	}
	ret = strcmp(a->key, b->key);
	if (ret)
		return ret;

	if (a->value_len != b->value_len) {
		return a->value_len - b->value_len;
	}

	if (!a->value && !b->value)
		return 0;
	if (a->value && !b->value)
		return 1;
	if (b->value && !a->value)
		return -1;

	return strcmp(a->value, b->value);
}

int kvvec_sort(struct kvvec *kvv)
{
	qsort(kvv->kv, kvv->kv_pairs, sizeof(struct key_value *), kv_compare);
	kvv->kvv_sorted = 1;
	return 0;
}

int kvvec_foreach(struct kvvec *kvv, void *arg, int (*callback)(struct key_value *,void *))
{
	int i;

	for (i = 0; i < kvv->kv_pairs; i++) {
		callback(kvv->kv[i], arg);
	}
	return 0;
}

int kvvec_destroy(struct kvvec *kvv, int free_kvs)
{
	int i;

	for (i = 0; i < kvv->kv_pairs; i++) {
		struct key_value *kv = kvv->kv[i];
		if (free_kvs) {
			free(kv->key);
			if (kv->value && kv->value_len) {
				free(kv->value);
			}
		}
		free(kv);
	}

	free(kvv->kv);
	free(kvv);
	return 0;
}

/*
 * Caller can tell us to over-allocate the buffer if he/she wants
 * to put extra stuff at the end of it.
 */
struct kvvec_buf *kvvec2buf(struct kvvec *kvv, char kv_sep, char pair_sep, int overalloc)
{
	struct kvvec_buf *kvvb;
	int i;
	unsigned long len = 0;

	kvvb = malloc(sizeof(struct kvvec_buf));
	if (!kvvb)
		return NULL;

	/* overalloc + (kv_sep_size * kv_pairs) + (pair_sep_size * kv_pairs) */
	kvvb->bufsize = overalloc + (kvv->kv_pairs * 2);
	for (i = 0; i < kvv->kv_pairs; i++) {
		struct key_value *kv = kvv->kv[i];
		kvvb->bufsize += kv->key_len + kv->value_len;
	}
	kvvb->buf = malloc(kvvb->bufsize);
	if (!kvvb->buf) {
		free(kvvb);
		return NULL;
	}

	for (i = 0; i < kvv->kv_pairs; i++) {
		struct key_value *kv = kvv->kv[i];
		memcpy(kvvb->buf + len, kv->key, kv->key_len);
		len += kv->key_len;
		kvvb->buf[len++] = kv_sep;
		if (kv->value_len) {
			memcpy(kvvb->buf + len, kv->value, kv->value_len);
			len += kv->value_len;
		}
		kvvb->buf[len++] = pair_sep;
	}
	memset(kvvb->buf + len, 0, kvvb->bufsize - len);
	kvvb->buflen = len;
	return kvvb;
}

/*
 * Converts a buffer of random bytes to a key/value vector.
 * This requires a fairly rigid format in the input data to be of
 * much use, but it's nifty for ipc where only computers are
 * involved, and it will parse the kvvec2buf() produce nicely.
 */
struct kvvec *buf2kvvec(const char *str, unsigned int len,
			const char kvsep, const char pair_sep)
{
	struct kvvec *kvv;
	unsigned int num_pairs = 0, i, offset = 0;

	if (!str || !len)
		return NULL;

	fprintf(stderr, "buf2kvvec(): Parsing string with %u bytes::\n", len);
	//write(fileno(stderr), str, len);
	/* first we count the number of key/value pairs */
	for (;;) {
		const char *ptr = memchr(str + offset, pair_sep, len - offset);
		if (!ptr)
			break;
		num_pairs++;
		ptr++;
		offset += (unsigned long)ptr - ((unsigned long)str + offset);
	}
	if (!num_pairs) {
		fprintf(stderr, "No key/value pairs found\n");
		return NULL;
	}

	kvv = kvvec_init(num_pairs);
	if (!kvv)
		return NULL;

	offset = 0;
	for (i = 0; i < num_pairs; i++) {
		struct key_value *kv;
		char *key_end_ptr, *kv_end_ptr;

		fprintf(stderr, "@kvvec: offset: %d\n", offset);
		/* keys can't begin with nul bytes */
		if (offset && str[offset] == '\0') {
			fprintf(stderr, "@kvvec: Found nul byte at start of key. %d pairs parsed\n", kvv->kv_pairs);
			return kvv;
		}

		kv = malloc(sizeof(*kv));

		key_end_ptr = memchr(str + offset, kvsep, len - offset);
		if (!key_end_ptr)
			break;
		kv->key_len = (unsigned long)key_end_ptr - ((unsigned long)str + offset);
		kv->key = malloc(kv->key_len + 1);
		memcpy(kv->key, str + offset, kv->key_len);
		kv->key[kv->key_len] = 0;

		offset += kv->key_len + 1;

		kv_end_ptr = memchr(str + offset + 1, pair_sep, len - offset);
		if (!kv_end_ptr)
			break;
		kv->value_len = (unsigned long)kv_end_ptr - ((unsigned long)str + offset);
		kv->value = malloc(kv->value_len + 1);
		kv->value[kv->value_len] = 0;
		memcpy(kv->value, str + offset, kv->value_len);

		offset += kv->value_len + 1;
		kvv->kv[kvv->kv_pairs++] = kv;
	}

	return kvv;
}
