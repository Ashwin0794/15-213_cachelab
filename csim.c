/* 
  Name: Ashwin Meena Meiyappan
  hardware cache simulator uses least recently used (LRU) replacement policy
  when choosing which cache line to evict.  
  course 15-213 Computer Systems -CacheLab
*/

#include "cachelab.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>

typedef struct _cache_line {
  unsigned valid_bit:1;
  unsigned long tag;
  struct _cache_line* next;
  struct _cache_line* prev;
} cache_line;

typedef struct _cache_set {
  cache_line* head;
  int free_lines;
} cache_set;

typedef struct _cache_args {
  int set_bits;
  int E_lines;
  int bit_offset_bits;
  int sets;
  unsigned set_mask;
  unsigned long tag_mask;
  int verbose;
} cache_args;

typedef struct _file_entry {
  char* filename;
  char op;
  unsigned long hex_mem_address;
  int byte_size;
  int set;
  unsigned long tag;
} file_entry;

typedef struct _cache_results {
  int total_hit;
  int total_miss;
  int total_eviction;
  char hit_miss_evict_result; /* which can only hold H,M or E values*/
} cache_results;

void calculate_set_tag_mask(cache_args* cache_args_obj) {
  cache_args_obj->set_mask = (unsigned) ((cache_args_obj->sets - 1) << 
                                            cache_args_obj->bit_offset_bits);
  cache_args_obj->tag_mask = (unsigned long)((0x1 << 
                                          (cache_args_obj->set_bits +
                                            cache_args_obj->bit_offset_bits))
                                              - 1); 
  cache_args_obj->tag_mask = ~cache_args_obj->tag_mask;
}

int find_target_set(cache_args* cache_args_obj, file_entry* file_entry_obj) {
  int target_set;
  target_set = file_entry_obj->hex_mem_address & cache_args_obj->set_mask;
  target_set = target_set >> cache_args_obj->bit_offset_bits;
  return target_set;
}

unsigned long find_tag(cache_args* cache_args_obj, file_entry* file_entry_obj) {
  unsigned long tag;
  tag = file_entry_obj->hex_mem_address & cache_args_obj->tag_mask;
  return tag;
}

/* allocating memory for cache sets */
cache_set* allocate_memory_for_cache(cache_args* cache_args_obj) {
  cache_set* cache_set_obj = (cache_set*)malloc(sizeof(cache_set) * 
                                                  cache_args_obj->sets);
  if (cache_set_obj == NULL) {
    fprintf(stderr, "Malloc error\n");
    exit(EXIT_FAILURE);
  }

  for (int set = 0; set < cache_args_obj->sets; set++) {
    cache_set_obj[set].free_lines = cache_args_obj->E_lines;
    cache_set_obj[set].head = NULL; 
  }
  return cache_set_obj;
}

/* allocating memory for single cache line */
cache_line* get_cache_line() {
  cache_line* cache_line_obj = (cache_line*)malloc(sizeof(cache_line));
  if (cache_line_obj) {
    cache_line_obj->valid_bit = 0;
    cache_line_obj->tag = 0;
    cache_line_obj->next = NULL;
    cache_line_obj->prev = NULL;
    return cache_line_obj;
  }
  fprintf(stderr, "Malloc error");
  exit(EXIT_FAILURE);
}

/* searching line in the target set */
bool search_for_line(cache_set* cache_set_obj, file_entry* file_entry_obj) {
  cache_line* curr_line;
  int target_set = file_entry_obj->set;
  if (cache_set_obj && file_entry_obj) {
    if (cache_set_obj[target_set].head) {
      curr_line = cache_set_obj[target_set].head;
      while (curr_line != NULL) {
        if (curr_line->valid_bit && (curr_line->tag == file_entry_obj->tag)) {
          return true;
        }
        curr_line = curr_line->next;
      }
    }
    return false;
  }
    return false;
}

bool insert_line_at_head(cache_set* cache_set_obj, 
                         cache_line* new_line,
                         int target_set) {

  if (cache_set_obj && new_line) {
    cache_line* line_head = cache_set_obj[target_set].head;
    new_line->next = line_head;
    cache_set_obj[target_set].head = new_line;
    line_head->prev = cache_set_obj[target_set].head;
    return true;
  }
  return false;
}

bool insert_line(cache_set* cache_set_obj, file_entry* file_entry_obj) {
  cache_line* new_line = get_cache_line();
  if (new_line && cache_set_obj && file_entry_obj) {

    /* insert line when already one or more cache line exists in target set */
    if (cache_set_obj[file_entry_obj->set].head) {
      new_line->valid_bit = 1;
      new_line->tag = file_entry_obj->tag;
      return insert_line_at_head(cache_set_obj, new_line, file_entry_obj->set);
    }
    
    /* insert line when target set is empty */
    cache_set_obj[file_entry_obj->set].head = new_line;
    cache_set_obj[file_entry_obj->set].head->valid_bit = 1;
    cache_set_obj[file_entry_obj->set].head->tag = file_entry_obj->tag;
    return true;
  }
  return false;
}

cache_line* remove_cache_line(cache_set* cache_set_obj, 
                              cache_line* cache_line_obj) {
  cache_line* removed_cache_line;
  
  /* remove cache line when it is at the end of the list */
  if (cache_line_obj->next == NULL) {
    removed_cache_line = cache_line_obj;
    cache_line_obj->prev->next = NULL;
  }
  /* remove cache_line when it is in the middle of the list */
  else {
    removed_cache_line = cache_line_obj;
    cache_line_obj->prev->next = cache_line_obj->next;
    removed_cache_line->next->prev = removed_cache_line->prev;
    removed_cache_line->next = NULL;
  }
  removed_cache_line->prev = NULL;
  return removed_cache_line;
}

void cache_simulator(cache_set* cache_set_obj, file_entry* file_entry_obj, 
                     cache_results* cache_results_obj, 
                     cache_args* cache_args_obj) {
  bool match, insert_successful;
  int target_set = file_entry_obj->set;
  cache_line* curr_cache_line;
  cache_line* recently_used_cache_line;
  
  match = search_for_line(cache_set_obj, file_entry_obj);
  if (match) {
    /* hit */
    curr_cache_line = cache_set_obj[target_set].head;
    while (curr_cache_line != NULL) {
      if (curr_cache_line->valid_bit && 
         (curr_cache_line->tag == file_entry_obj->tag)) {
        break;
      }
      curr_cache_line = curr_cache_line->next;
    }
    /* if the cache_line is head */
    if (curr_cache_line->prev == NULL) {
      cache_results_obj->total_hit+= 1;
      cache_results_obj->hit_miss_evict_result = 'H';
      return;
    }
    recently_used_cache_line = 
            remove_cache_line(cache_set_obj, curr_cache_line);
    insert_successful = insert_line_at_head(cache_set_obj, 
                              recently_used_cache_line, target_set);
    if (insert_successful) {
      cache_results_obj->total_hit+= 1;
      cache_results_obj->hit_miss_evict_result = 'H';
    }
  }
  else {
    /* miss */
    if (cache_set_obj[target_set].free_lines) {
      insert_successful = insert_line(cache_set_obj, file_entry_obj);
      if (insert_successful) {
        cache_set_obj[target_set].free_lines-= 1;
        cache_results_obj->total_miss+= 1;
        cache_results_obj->hit_miss_evict_result = 'M';
      }
    }
    else {
      /* miss and evict case. 
         evict according to LRU cache_line replacement policy */
      
      /* direct mapped cache */
      if (cache_args_obj->E_lines == 1) {
        cache_set_obj[target_set].head->tag = file_entry_obj->tag;
        cache_results_obj->total_miss+= 1;
        cache_results_obj->total_eviction+= 1;
        cache_results_obj->hit_miss_evict_result = 'E';
        return ;
      }
      curr_cache_line = cache_set_obj[target_set].head;
      while (curr_cache_line->next != NULL) {
        curr_cache_line = curr_cache_line->next;
      }
      recently_used_cache_line = remove_cache_line(cache_set_obj, 
                                                curr_cache_line);
      recently_used_cache_line->valid_bit = 1;
      recently_used_cache_line->tag = file_entry_obj->tag;
      insert_successful = insert_line_at_head(cache_set_obj, 
                                  recently_used_cache_line, target_set);
      if (insert_successful) {
        cache_results_obj->total_miss+= 1;
        cache_results_obj->total_eviction+= 1;
        cache_results_obj->hit_miss_evict_result = 'E';
      }
    }
  }
}

void free_memory(cache_set* cache_set_obj, cache_args* cache_args_obj) {
  cache_line* next_curr_cache_line;
  cache_line* curr_cache_line;
  for (int set = 0; set < cache_args_obj->sets; set++) {
    curr_cache_line = cache_set_obj[set].head;
    while (curr_cache_line != NULL) {
      next_curr_cache_line = curr_cache_line->next;
      free(curr_cache_line);
      curr_cache_line = next_curr_cache_line;
    }
  }
  free(cache_set_obj);
}

void helper_function()
{
  fprintf(stderr,"run cache simulator as mentioned below\n");
  fprintf(stderr,"./csim -[hv] -s <arg> -E <arg> -b <arg> -t <arg>\n");
  fprintf(stderr,"-[hv] are optional arguments\n");
  fprintf(stderr,"-s <set bits for 2 to the power of s sets>\n");
  fprintf(stderr,"-E <total cache lines>\n");
  fprintf(stderr,"-b <bit offset bits for 2 to the power b Bytes >\n");
  fprintf(stderr,"-t <filename>\n");
}

void print_results(cache_results* cache_results_obj, 
                   cache_args* cache_args_obj, file_entry* file_entry_obj) {  
  if (cache_args_obj->verbose) {
    printf("%c, %lx, %d ",file_entry_obj->op, 
           file_entry_obj->hex_mem_address, file_entry_obj->byte_size);
  
    switch (cache_results_obj->hit_miss_evict_result) {
      case'H':
        printf("hit");
        break;
      case 'M':
        printf("miss");
        break;
      case 'E':
        printf("miss, eviction");
        break;
      default:
        printf("default case of print_results()");
    }
  }
}

int main (int argc, char* argv[]) {
  char curr_char; 
  int opt;
  FILE *fp;

  cache_args cache_args_obj;
  file_entry file_entry_obj;
  cache_set* cache_set_obj;
  cache_results cache_results_obj;

  if (argc < 2) {
    printf("./csim [hv] -s set_bits -E total_lines -b bit_offset_bits 
            -t file_name");
    printf("argc < 2");
    exit(EXIT_FAILURE);
  }

  /* parsing command line arguments */
  while ((opt = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
    switch (opt) {
      case 'h':
        helper_function();
        break;

      case 'v':
        cache_args_obj.verbose = 1;
        break;

      case 's': 
        cache_args_obj.set_bits = atoi(optarg);
        cache_args_obj.sets = 0x1 << cache_args_obj.set_bits;
        break;

      case 'E': 
       cache_args_obj.E_lines = atoi(optarg);
        break;
        
      case 'b': 
        cache_args_obj.bit_offset_bits = atoi(optarg);
        break;

      case 't':
        file_entry_obj.filename = optarg;
        break;
      
      default:
        printf("Please enter in the following format\n");
        printf("./csim [hv] -s <set bits> -E <total number of lines> 
                -b <bit offset bits> -t <filename>");
        exit(EXIT_FAILURE);
      }
  }
        
  /* Check for required mandatory options for cache simulator */
  if (file_entry_obj.filename == NULL || cache_args_obj.set_bits == 0 || 
      cache_args_obj.E_lines == 0 || cache_args_obj.bit_offset_bits == 0) {
    printf("didn't specify all the mandatory options for cache simulator\n");
    helper_function();
    return 0;
  }  
  
  /* Check invalid inputs for set bits, cache lines, bit offset bit values */
  if (cache_args_obj.set_bits < 1 || cache_args_obj.E_lines < 1 
      || cache_args_obj.bit_offset_bits < 1) {
    printf("set_bits < 1 or E_lines < 0 or bit_offset_bits < 1 are 
            not allowed\n");
    return 0;
  }
   
  cache_set_obj = allocate_memory_for_cache(&cache_args_obj);
  if (cache_set_obj == NULL) {
    printf("error\n");
    return 0;
  }
  
  /* Calculate set and tag identifier */
  calculate_set_tag_mask(&cache_args_obj);
  
  fp = fopen(file_entry_obj.filename, "r");
  if (fp == NULL) {
    printf("Something went wrong with file pointer during fopen\n");
    exit(1);
  }
  
  /* parsing trace entry */
  while (!(feof(fp))) {
    
    fscanf(fp, "%c", &curr_char);
    
    switch (curr_char) {
      case 'I': 
        fscanf(fp, " %lx,%d\n", &file_entry_obj.hex_mem_address, 
               &file_entry_obj.byte_size);
        break;

      case ' ':
        // If L,S or M is the first instruction in file  
        fscanf(fp, "%c %lx,%d\n", &file_entry_obj.op, 
               &file_entry_obj.hex_mem_address, &file_entry_obj.byte_size);
        
        file_entry_obj.set = find_target_set(&cache_args_obj, &file_entry_obj);
        file_entry_obj.tag = find_tag(&cache_args_obj, &file_entry_obj);
        
        cache_simulator(cache_set_obj, &file_entry_obj, &cache_results_obj, 
                        &cache_args_obj);
        print_results(&cache_results_obj, &cache_args_obj, &file_entry_obj);
        
        if (file_entry_obj.op == 'M') {
          cache_simulator(cache_set_obj, &file_entry_obj, &cache_results_obj, 
                          &cache_args_obj);
        }
        if (cache_args_obj.verbose && file_entry_obj.op == 'M') {
          printf(" hit\n");
          break;
        }
        printf("\n");
        break;

      case 'M':
      case 'L':
      case 'S':
        fscanf(fp, " %lx,%d\n", &file_entry_obj.hex_mem_address, 
                                &file_entry_obj.byte_size);
        
        file_entry_obj.op = curr_char;
        file_entry_obj.set = find_target_set(&cache_args_obj, &file_entry_obj);
        file_entry_obj.tag = find_tag(&cache_args_obj, &file_entry_obj);
        
        cache_simulator(cache_set_obj, &file_entry_obj, &cache_results_obj, 
                        &cache_args_obj);
        print_results(&cache_results_obj, &cache_args_obj, &file_entry_obj);
        
        if (file_entry_obj.op == 'M') {
          cache_simulator(cache_set_obj, &file_entry_obj, &cache_results_obj, 
                          &cache_args_obj);
        }
        if (cache_args_obj.verbose && file_entry_obj.op == 'M') {
          printf(" hit\n");
          break;
        }
        printf("\n");
        break;

      default : 
        printf("default case\n");
        exit(1);
    }
  } 
  fclose(fp);
  free_memory(cache_set_obj, &cache_args_obj);
  printSummary(cache_results_obj.total_hit, cache_results_obj.total_miss, 
               cache_results_obj.total_eviction);
  return 0;
}
