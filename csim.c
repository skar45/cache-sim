#include "cachelab.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int hits = 0;
int misses = 0;
int evictions = 0;

typedef struct CacheLine {
  long tag;
  char valid;
} CacheLine;

typedef struct Node {
  CacheLine* cache;
  struct Node* prev;
  struct Node* next;
} Node;

typedef struct LL {
  struct Node* head;
  struct Node* tail;
  // cache lines per set as length of linked list
  int size;
} LL;

typedef struct Cache {
  // length of cache_set
  unsigned int sets;
  // size of block per cache line
  unsigned int blocks; 
  struct LL** cache_set;
} Cache;

CacheLine* create_cl(int tag, int valid) {
  struct CacheLine* cache = (CacheLine*)malloc(sizeof(CacheLine));
  cache->tag = tag;
  cache->valid = valid;
  return cache;
}

LL* init_ll(int size) {
  if (size == 0) return NULL;
  struct LL* list = (LL*)malloc(sizeof(LL));
  struct Node* hnode = (Node*)malloc(sizeof(Node));
  hnode->cache = create_cl(0,0);

  list->head = hnode;
  list->tail = hnode;
  list->size = size;

  Node* iter_node = list->head;
  int i;
  for (i = 1; i < size; i++) {
    struct Node* node = (Node*)malloc(sizeof(Node));
    node->cache = create_cl(0,0);
    // set iter as prev
    node->prev = iter_node;
    // set the new node in iter as next
    iter_node->next = node;
    iter_node = node;
  }
  list->tail = iter_node;

  return list;
}

// 0 miss, 1 eviction, 2 hit
int lru_read(LL* list, long tag) {
  Node* iter_node = list->head;
  Node* invalid_node = NULL;
  Node* tag_node = NULL;

  // 0, 1, 2
  int ret_code;

  while (iter_node) {
    int valid = iter_node->cache->valid;
    long iter_tag = iter_node->cache->tag;
    if (valid == 0) {
      invalid_node = iter_node;
    }
    if (iter_tag == tag && valid == 1) {
      tag_node = iter_node;
      // it's a cache hit
      ret_code = 2;
      break;
    }
    iter_node = iter_node->next;
  }

  if (invalid_node && tag_node == NULL) {
    iter_node = invalid_node;
    // no tag in cache but an empty line means a plain old cache miss
    ret_code = 0;
  } else if (tag_node == NULL) {
    iter_node = list->tail;
    // no tag in cache and no empty line therefore we need to evict a cache line
    ret_code = 1;
  }

  // LRU ordering
  while (iter_node->prev) {
    CacheLine* cache = iter_node->cache;
    CacheLine* prev_cache = iter_node->prev->cache;
    cache->tag = prev_cache->tag;
    cache->valid = prev_cache->valid;
    iter_node = iter_node->prev;
  }

  list->head->cache->tag = tag;
  list->head->cache->valid = 1;
  return ret_code;
}

void get_set_tag(long addr, int sets, int blocks, int* set, long* tag) {
  int b_bits = log2(blocks);
  int s_bits = log2(sets);
  /**
   * Get set bits:
   * | 1111 | 1111 | 1111 |
   *   ^ tag  ^ set  ^ block offset
   *  remove block offset with shift: addr >> block offset
   *  remove tag with bitwise & with [1..1] bits the length of set bits: (2^set - 1) & addr
   */
  *set = ((2 << (s_bits - 1)) - 1) & (addr >> b_bits);
  *tag = addr >> (s_bits + b_bits);
}

int read_from_cache(Cache* c, long addr, int block_size) {
  int set;
  long tag;
  get_set_tag(addr, c->sets, c->blocks, &set, &tag);
  int cc = lru_read(c->cache_set[set], tag);
  switch (cc) {
    case 0:
      misses++;
      break;
    case 1:
      misses++;
      evictions++;
      break;
    case 2:
      hits++;
      break;
  }
  return 1;
}

int store_in_cache(Cache* c, long addr, int block_size) {
  int set;
  long tag;
  get_set_tag(addr, c->sets, c->blocks, &set, &tag);
  int cc = lru_read(c->cache_set[set], tag);
  switch (cc) {
    case 0:
      misses++;
      break;
    case 1:
      misses++;
      evictions++;
      break;
    case 2:
      hits++;
      break;
  }
  return 1;
}

int store_and_load_from_cache(Cache* c, long addr, int block_size) {
  int set;
  long tag;
  get_set_tag(addr, c->sets, c->blocks, &set, &tag);
  int cc = lru_read(c->cache_set[set], tag);
  switch (cc) {
    case 0:
      misses++;
      hits++;
      break;
    case 1:
      misses++;
      hits++;
      evictions++;
      break;
    case 2:
      hits++;
      hits++;
      break;
  }
  return 1;
}

int main(int argc, char* argv[]) {
    int i;
    char* endptr;
    int sets = 0, lines = 0, blocks = 0, set_bits = 0, block_bits = 0;
    // int hits = 0, misses = 0;
    char* tf;
    for (i = 0; i < argc; i++) {
      char prefix = *argv[i];
      char suffix = *(argv[i]+1);
      if (prefix == '-' && suffix == 'E') {
	lines = strtol(argv[i+1], &endptr, 10);
      }

      if (prefix == '-' && suffix == 's') {
	set_bits = strtol(argv[i+1], &endptr, 10);
      }

      if (prefix == '-' && suffix == 'b') {
	block_bits = strtol(argv[i+1], &endptr, 10);
      }

      if (prefix == '-' && suffix == 't') {
	tf = argv[i+1];
      }
    }
    if (tf == NULL) return 0;

    sets = 1 << set_bits;
    blocks = 1 << block_bits;

    LL* cache_set[sets];

    for (i = 0; i < sets; i++){
      cache_set[i] = init_ll(lines);
    }

    struct Cache cache =  { sets, blocks, cache_set };

    FILE* file = fopen(tf, "r");
    if (file == NULL) {
      printf("Unable to open file at path: %s \n", tf);
    }

    char buffer[100];
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
      int i;
      for (i = 0; i < 100; i++) {
	char *e;
	long addr;
	int block_size;
	// null terminated string
	if (buffer[i] == 0 && buffer[i+1] == 0) break;
	switch (buffer[i]) {
	  // ignore empty space
	  case 32:
	    if (buffer[i] == 32) continue;
	    break;
	  // L
	  case 76:
	    addr = strtol(&buffer[i+2], &e, 16);
	    block_size = strtol(++e, &e, 10);
	    read_from_cache(&cache, addr, block_size);
	    break;
	  // M
	  case 77:
	    addr = strtol(&buffer[i+2], &e, 16);
	    block_size = strtol(++e, &e, 10);
	    store_and_load_from_cache(&cache, addr, block_size);
	    break;
	  // S
	  case 83:
	    addr = strtol(&buffer[i+2], &e, 16);
	    block_size = strtol(++e, &e, 10);
	    store_in_cache(&cache, addr, block_size);
	    break;
	}
	break;
      }
    }

    fclose(file);

    printSummary(hits, misses, evictions);
    return 1;
}
