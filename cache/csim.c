#include "stdio.h"
#include "stdlib.h"
#include "cachelab.h"
#include <getopt.h>
#include <unistd.h>

typedef enum access_state{
    HIT = 0,
    MISS,
    EVICTION
} Access_state;

typedef struct cache_line{
    int valid;
    int tag;
    int timeStamp;
} Cache_line;

void initCache(int S, int E, Cache_line* cachePtr, Cache_line** setPtr){
    int size = S * E;
    for(int i = 0; i<size; i++){
        cachePtr[i].tag = -1;
        cachePtr[i].valid = 0;
        cachePtr[i].timeStamp = 0;
    }
    for(int j = 0; j < S; j++){
        setPtr[j] = &cachePtr[j * E];
    }
}

int power2(int s){
    int r = 1;
    for(int i = 0; i<s; i++){
        r *= 2;
    }
    return r;
}

unsigned mask(int s, int b){
    unsigned m = 0;
    for(int i = 0; i < s; i++){
        m += 1<<(b+i);
    }
    return m;
}

Access_state accessCache(Cache_line* setPtr, int tag, int E){
    Cache_line* emptySet = NULL;
    Cache_line* hitSet = NULL;
    Cache_line* evictSet = setPtr;

    for(int i = 0; i < E; i++){
        if(setPtr[i].tag == tag){
            hitSet = setPtr + i;
            hitSet->timeStamp = 0;
        }
        else if(setPtr[i].valid == 0){
            emptySet = setPtr + i;
        }
        else{
            setPtr[i].timeStamp += 1;
            if(evictSet->timeStamp < setPtr[i].timeStamp){
                evictSet = setPtr + i;
            }
        }
    }

    if(hitSet != NULL) return HIT;
    if(emptySet != NULL){
        emptySet->valid = 1;
        emptySet->tag = tag;
        emptySet->timeStamp = 0;
        return MISS;
    }
    evictSet->tag = tag;
    evictSet->timeStamp = 0;
    evictSet->valid = 1;
    return EVICTION;  
}

void printState(Access_state state){
    switch (state)
    {
    case HIT:
        printf("hit ");
        break;
    case MISS:
        printf("miss ");
        break;
    default:
        printf("miss eviction ");
        break;
    }
}

void addState(Access_state state, int* hit, int* miss, int* eviction){
    switch (state)
    {
    case HIT:
        *hit += 1;
        break;
    case MISS:
        *miss += 1;
        break;
    default:
        *miss += 1;
        *eviction += 1;
        break;
    }
}

int main(int argc, char** argv)
{
    int opt, E, s, b;
    char* t;
    int v = 0;
    while(-1 != (opt = getopt(argc, argv, "h:vs:E:b:t:"))){
        switch (opt)
        {
        case 'h':
            printf("help mode\n");
            return -1;
        case 'v':
            v = 1;
            printf("verbose mode %d\n", v);
            break;
        case 's':
            s = atoi(optarg);
            printf("s is %d\n", s);
            break;
        case 'E':
            E = atoi(optarg);
            printf("E is %d\n", E);
            break;
        case 'b':
            b = atoi(optarg);
            printf("b is %d\n", b);
            break;
        case 't':
            t = optarg;
            printf("t is %s\n", t);
            break;
        default:
            printf("unknown command.\n");
            return -1;
        }
    }

    int S = power2(s);

    Cache_line* cachePtr = malloc(sizeof(Cache_line)*S*E);
    printf("size of cache line is %ld * %d * %d\n", sizeof(Cache_line), S, E);

    Cache_line* setPtr[S];
    initCache(S, E, cachePtr, setPtr);

    int hits = 0, misses = 0, evictions = 0;

    FILE* pFile; //pointer to FILE object
    pFile = fopen(t,"r");
    char identifier;
    unsigned address;
    int size;
    unsigned m = mask(s, b);

    while(fscanf(pFile," %c %x,%d", &identifier, &address, &size)>0)
    {
        int set = (address & m)>>b;
        int tag = address >> (s + b);
        printf("address is %x, set is %x, tag is %x\n", address, set, tag);
        printf("identifier is %c\n", identifier);
        printf("size is %d\n", size);
        if(identifier == 'I') continue;
        if(identifier == 'M'){
            Access_state state1 = accessCache(setPtr[set], tag, E);
            Access_state state2 = accessCache(setPtr[set], tag, E);
            if(v == 1){
                printf("%c %x,%d ",identifier, address, size);
                printState(state1);
                printState(state2);
                printf("\n");
            } 
            addState(state1, &hits, &misses, &evictions);
            addState(state2, &hits, &misses, &evictions);
        }
        else{
            Access_state state1 = accessCache(setPtr[set], tag, E);
             if(v == 1){
                printf("%c %x,%d ",identifier, address, size);
                printState(state1);
                printf("\n");
            }
            addState(state1, &hits, &misses, &evictions);
        }
    }

    fclose(pFile);
    free(cachePtr);
    printSummary(hits, misses, evictions);
    return 0;
}
