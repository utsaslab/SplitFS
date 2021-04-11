/*
 * Copyright 2016-2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "hash_map.h"
#include "os_thread.h"
#include "out.h"

#define INITIAL_NBUCKETS OPEN_MAX

/* hash map bucket */
struct hash_map_bucket {
    uint32_t key;
    void *value;
};

/* hash map */
struct hash_map {

	/* number of elements in "buckets" */
	size_t nbuckets;

	/* buckets */
	struct hash_map_bucket *buckets;

	/* number of used slots */
	size_t entries;
};

/*
 * hash_map_alloc -- allocates hash map
 */
struct hash_map *
hash_map_alloc(void)
{
	struct hash_map *map = calloc(1, sizeof(*map));
	if (!map)
		return NULL;

	map->nbuckets = INITIAL_NBUCKETS;
	map->buckets = calloc(map->nbuckets, sizeof(map->buckets));
	if (!map->buckets) {
		free(map);
		return NULL;
	}

	return map;
}

/*
 * hash_map_traverse -- returns number of live entries
 */
int
hash_map_traverse(struct hash_map *map)
{
	int num = 0;

	for (unsigned i = 0; i < map->nbuckets; ++i) {
		struct hash_map_bucket *bucket = &map->buckets[i];

		void *value = bucket->value;
		if (value) {
			num++;
		}
	}

	return num;
}

/*
 * hash_map_free -- destroys inode hash map
 */
void
hash_map_free(struct hash_map *map)
{
	free(map->buckets);
	free(map);
}

/*
 * pf_hash -- returns hash value of the key
 */
static inline uint32_t
hash(struct hash_map *map, uint32_t key)
{
	return (key);
}

/*
 * hash_map_rebuild -- rebuilds the whole inode hash map
 *
 * Returns 0 on success, negative value (-errno) on failure, 1 on hash map
 * conflict.
 */
static int
hash_map_rebuild(struct hash_map *c, size_t new_sz)
{
	struct hash_map_bucket *new_buckets =
			calloc(new_sz, sizeof(new_buckets));
	size_t idx;

	if (!new_buckets)
		return -errno;

	for (size_t i = 0; i < c->nbuckets; ++i) {
		struct hash_map_bucket *b = &c->buckets[i];

        idx = hash(c, b->key) % new_sz;
        struct hash_map_bucket *newbucket = &new_buckets[idx];
        if (newbucket->key == 0)
            newbucket->value = b->value;
	}

	free(c->buckets);
	c->nbuckets = new_sz;
	c->buckets = new_buckets;

	return 0;
}

/*
 * hash_map_remove -- removes key/value from the hash map
 */
int
hash_map_remove(struct hash_map *map, uint32_t key,
		void *value)
{
	size_t idx = hash(map, key) % map->nbuckets;
	struct hash_map_bucket *b = &map->buckets[idx];
	if (b->value == value)
		memset(b, 0, sizeof(struct hash_map_bucket));

	map->entries--;

	return 0;
}

/*
 * hash_map_get -- returns value associated with specified key
 */
void *
hash_map_get(struct hash_map *map, uint32_t key)
{
	size_t idx = hash(map, key) % map->nbuckets;

	struct hash_map_bucket *b = &map->buckets[idx];
	if (b->key == key)
		return b->value;

	return NULL;
}

/*
 * hash_map_put -- inserts key/value into hash map
 *
 * Returns existing value if key already existed or inserted value.
 */
void *
hash_map_put(struct hash_map *map, uint32_t key, void *value)
{
	uint32_t idx = (uint32_t) (hash(map, key) % map->nbuckets);

	struct hash_map_bucket *b = &map->buckets[idx];
	uint32_t empty_slot = UINT32_MAX;
    if (b->key == key)
        return b->value;

    if (empty_slot == UINT32_MAX && b->key == 0)
        empty_slot = idx;

	while (empty_slot == UINT32_MAX) {
		size_t new_sz = map->nbuckets*2;
		int res;
		res = hash_map_rebuild(map, new_sz);

		if (res < 0) {
			errno = -res;
			return NULL;
		}

		idx = (uint32_t) (hash(map, key) % map->nbuckets);
		b = &map->buckets[idx];
        if (b->key == 0)
            empty_slot = idx;
	}

	b->key = key;
	b->value = value;
	map->entries++;

	return value;
}
