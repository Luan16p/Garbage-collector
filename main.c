#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <windows.h> // Importando windows.h até achar algo que substitui a nojeira que é usar GetCurrentProcess e  VirtualAlloc.
#include <psapi.h>
#include <assert.h>

#define TAMANHO_ALOCACAO 4096
#define UNTAG(p) (((uintptr_t) (p)) & 0xfffffffc)

struct header {
    unsigned int tamanho;
    struct header *prox;
};

static struct header base;
static struct header *freec = &base;
static struct header *used = NULL;

void init(void);

static void free_list(struct header *usd) {
    struct header *pointer;

    for (pointer = freec; !(usd > pointer && usd < pointer->prox); pointer = pointer->prox)
        if (pointer >= pointer->prox && (usd > pointer || usd < pointer->prox))
            break;
    
    if (usd + usd->tamanho == pointer->prox) {
        usd->tamanho += pointer->prox->tamanho;
        usd->prox = pointer->prox->prox;
    } else {
        usd->prox = pointer->prox;
    }

    if (pointer + pointer->tamanho == usd) {
        pointer->tamanho += usd->tamanho;
        pointer->prox = usd->prox;
    } else {
        pointer->prox = usd;
    }

    freec = pointer;
}

static struct header *morecore(size_t n) {
    void *voidPointer;
    struct header *uPointer;

    if (n > TAMANHO_ALOCACAO)
        n = TAMANHO_ALOCACAO / sizeof(struct header);
    
    voidPointer = VirtualAlloc(NULL, n * sizeof(struct header), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    
    if (voidPointer == NULL) {
        return NULL;
    }

    uPointer = (struct header *) voidPointer;
    uPointer->tamanho = n;
    free_list(uPointer);

    printf("Alocado mais memória: %zu blocos.\n", n);

    return freec;
}

void *myMalloc(size_t salloc) {
    size_t num_blocks;
    struct header *p, *prev;

    num_blocks = (salloc + sizeof(struct header) - 1) / sizeof(struct header) + 1;
    prev = freec;

    for (p = prev->prox;; prev = p, p = p->prox) {
        if (p->tamanho >= num_blocks) {
            if (p->tamanho > num_blocks) {
                struct header *newBlock = (struct header *)((char *)p + num_blocks * sizeof(struct header));
                newBlock->tamanho = p->tamanho - num_blocks;
                newBlock->prox = p->prox;
                p->tamanho = num_blocks;
                p->prox = newBlock;
            } else {
                prev->prox = p->prox;
            }

            if (used == NULL) {
                used = p;
                used->prox = used;
            } else {
                p->prox = used->prox;
                used->prox = p;
                used = p;
            }

            printf("Alocado %zu bytes em %p.\n", salloc, (void *)(p + 1));
            return (void *)(p + 1);
        }

        if (p == freec) {
            p = morecore(num_blocks);

            if (p == NULL) {
                printf("Falha ao alocar memória.\n");
                return NULL;
            }
        }
    }
}

static void scanner(uintptr_t *start, uintptr_t *end) {
    struct header *bp;

    for (; start < end; start++) {
        uintptr_t v = *start;
        bp = used;
        
        do {
            if (bp + 1 <= (void *)v && (void *)(bp + 1 + bp->tamanho) > (void *)v) {
                bp->prox = (struct header *)(((uintptr_t)bp->prox) | 1);
                break;
            }
        } while ((bp = UNTAG(bp->prox)) != used);
    }
}

static void scanner_heap(void) {
    uintptr_t *vp;
    struct header *bp, *up;

    for (bp = UNTAG(used->prox); bp != used; bp = UNTAG(bp->prox)) {
        if (!((uintptr_t)bp->prox & 1)) continue;

        for (vp = (uintptr_t *)(bp + 1); vp < (uintptr_t *)(bp + bp->tamanho + 1); vp++) {
            uintptr_t v = *vp;
            up = UNTAG(bp->tamanho);

            do {
                if (up != bp && up + 1 <= (void *)v && (void *)(up + 1 + up->tamanho) > (void *)v) {
                    up->prox = (struct header *)(((uintptr_t)up->prox) | 1);
                    break;
                }
            } while ((up = UNTAG(up->prox)) != bp);
        }
    }
}

void init(void) {
    static int initted;
    if (initted) return;

    initted = 1;

    HANDLE process = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS pmc;

    if (GetProcessMemoryInfo(process, &pmc, sizeof(pmc))) {
        SIZE_T pagefileUsage = pmc.PagefileUsage;
        SIZE_T peakPagefileUsage = pmc.PeakPagefileUsage;
    } else {
        assert(0);
    }

    used = NULL;
    base.prox = freec = &base;
    base.tamanho = 0;

    printf("Memória inicializada.\n");
}

void collect(void) {
    struct header *p, *prevp, *tp;
    uintptr_t stack_top;
    SYSTEM_INFO sys_info;
    extern char end, etext;

    if (used == NULL) {
        printf("Nada para coletar.\n");
        return;
    }

    printf("Iniciando coleta de memória...\n");

    scanner((uintptr_t *)&etext, (uintptr_t *)&end);

    GetSystemInfo(&sys_info);
    stack_top = (uintptr_t)sys_info.lpMaximumApplicationAddress;

    scanner((uintptr_t *)&stack_top, (uintptr_t)&base);

    scanner_heap();

    for (prevp = used, p = UNTAG(used->prox); ; prevp = p, p = UNTAG(p->prox)) {
        next_chunk:
            if (!((uintptr_t)p->prox & 1)) {
                tp = p;
                p = UNTAG(p->prox);
                free_list(tp);

                if (used == tp) {
                    used = NULL;
                    printf("Memória usada liberada completamente.\n");
                    break;
                }

                prevp->prox = (struct header *)(((uintptr_t)p) | ((uintptr_t)prevp->prox & 1));
                goto next_chunk;
            }

            p->prox = (struct header *)(((uintptr_t)p->prox) & ~1);

            if (p == used)
                break;
    }

    printf("Coleta de memória concluída.\n");
}


int main() {
    init();
    void *ptr1 = myMalloc(100);
    void *ptr2 = myMalloc(200);

    collect();
    
    printf("Coleta de memória concluída.\n");
    scanf("%s");

    return 0;
}
