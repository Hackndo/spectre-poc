#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <x86intrin.h>

#define GREEN   "\x1b[32m"
#define RESET   "\x1b[0m"

#define PAGE_SIZE 512
#define COUNT 100

volatile char paged_buffer[256 * PAGE_SIZE];
volatile uint32_t paged_buffer_sz = 256 * PAGE_SIZE;

void access_value(uint32_t x) {
    // Wrapper to access value, in case of optimisations
    (void)x;
}

uint32_t get_index_access_time(int value) {
    uint32_t cycle_difference = 0;
    uint32_t access_time = 0;
    uint32_t in_ram = 0;
    uint32_t in_cache = 0;

    /* Récupère l'index de la page à laquelle on accède */
    value *= PAGE_SIZE;
    
    /* Boucle pour faire une moyenne sur COUNT accès */
    for(int i = 0; i < COUNT; i++) {

        /* Vidage du cache */
        for(int j = 0; j < 256; j++) {
            // Evicts address from all levels of cache
            _mm_clflush((void*)(paged_buffer + j * PAGE_SIZE));
        }

        int before, after;

        before = __rdtsc(); // Donne le nombre de cycle d'horloge actuel
        access_value(paged_buffer[value]); // Accès à la page
        _mm_lfence(); // Permet d'éviter que 'after' soit récupéré avant que 'access_value' ne termine
        after = __rdtsc(); // Donne le nombre de cycle d'horloge actuel

        uint32_t diff = (uint32_t)(after-before); // Nombre de cycles pour l'accès à la zone mémoire

        access_time += diff;
        
        /*
         * Si le temps d'accès était supérieur à 80 cycles, alors on considère que la plage mémoire
         * étant dans la RAM
         * Sinon, elle était probablement dans le cache
         */
        if (diff > 80) {
            in_ram++;
        } else {
            in_cache++;
        }
    }

    /* S'il y a plus eu de cas en cache qu'en RAM, on ajoute une asterisque verte */
    if(in_cache > in_ram)
        printf("[" GREEN "*" RESET "] ");
    else
        printf("[ ] ");
    printf("% 4i % 4i % 5i - ", in_cache, in_ram, access_time / COUNT );
    if(in_cache > in_ram) {
        return 1;
    }
    return 0;
}


void get_all_access_time() {
    /*
     * Pour toutes les pages du buffer, on calcule le temps d'accès
     * CACHE : Nombre de fois où le nombre de cycle d'horloge était < à 80
     * MEM : Nombre de fois où le nombre de cycle d'horloge était > à 80
     * CYCLES : Moyenne du nombre de cycle d'horloge pour l'accès
     * HIT : Indique si, en moyenne, on a trouvé que la variable était en cache
     */
    printf(
        "    CACHE MEM CYCLES    HIT\n"
        "---------------------------\n");
    for(int i = 0; i < 256; i++) {
        printf("%c: %u\n",i, get_index_access_time(i));
    }

}

int main(void) {
    for (int i = 0; i < sizeof(paged_buffer); i++) {
        paged_buffer[i] = 1; /* Permet d'éviter une optimisation appelée copy-on-write ou COW */
    }

    get_all_access_time();
    
    return 0;
}