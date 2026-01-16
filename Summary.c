/*
 * Summary.c - Podsumowanie funkcji używających mutex, pthreads i semaforów
 * Zestaw funkcji z różnych plików demonstrujący synchronizację wątków
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

// ========================= SEMAPHORE FUNCTIONS (Clock-sync.c) =========================

/*
 * Funkcja wątku - używa semafora do ograniczenia liczby jednocześnie działających alarmów
 * Każdy wątek czeka określony czas, a następnie zwalnia semafor
 */
void *clock_thread_func(void *arg)
{
    struct clock_arguments {
        int32_t time;
        sem_t *semaphore;
    } *args = (struct clock_arguments *)arg;
    
    uint32_t tt;
    fprintf(stderr, "Will sleep for %d\n", args->time);
    
    // Czekaj określony czas
    for (tt = args->time; tt > 0; tt = sleep(tt))
        ;
    puts("Wake up");
    
    // Zwolnij semafor po zakończeniu działania
    if (sem_post(args->semaphore) == -1)
        ERR("sem_post");
    free(args);
    return NULL;
}

/*
 * Główna funkcja pracy z semaforami - tworzy wątki alarmów z ograniczeniem
 * Semafor ogranicza liczbę jednocześnie działających alarmów do FS_NUM
 */
void do_clock_work()
{
    #define FS_NUM 5
    #define MAX_INPUT 120
    
    int32_t time;
    pthread_t thread;
    char input[MAX_INPUT];
    struct clock_arguments {
        int32_t time;
        sem_t *semaphore;
    } *args;
    sem_t semaphore;
    
    // Inicjalizacja semafora z wartością FS_NUM (maksymalnie 5 alarmów)
    if (sem_init(&semaphore, 0, FS_NUM) != 0)
        ERR("sem_init");
    
    while (1) // work
    {
        puts("Please enter the number of seconds for the alarm delay:");
        if(fgets(input, MAX_INPUT, stdin) == NULL) {
            if (errno == EINTR)
                continue;
            ERR("fgets:");
        }
        
        time = atoi(input);
        if(time <= 0) {
            fputs("Incorrect time specified", stderr);
            continue;
        }
        
        // Próba zajęcia semafora (nieblokująco)
        if (TEMP_FAILURE_RETRY(sem_trywait(&semaphore)) == -1)
        {
            switch (errno)
            {
                case EAGAIN:
                    fprintf(stderr, "Only %d alarms can be set at the time.", FS_NUM);
                case EINTR:
                    continue;
            }
            ERR("sem_trywait");
        }
        
        // Tworzenie argumentów dla wątku
        if ((args = (struct clock_arguments *)malloc(sizeof(struct clock_arguments))) == NULL)
            ERR("malloc:");
        args->time = time;
        args->semaphore = &semaphore;
        
        // Tworzenie i odłączanie wątku
        if (pthread_create(&thread, NULL, clock_thread_func, (void *) args) != 0)
            ERR("pthread_create");
        if (pthread_detach(thread) != 0)
            ERR("pthread_detach");
    }
}

// ========================= PTHREAD BARRIER FUNCTIONS (Dice-sync.c) =========================

/*
 * Funkcja wątku gracza - używa pthread_barrier dla synchronizacji rund
 * Każdy gracz rzuca kością, czeka na pozostałych, a następnie jeden z nich przydziela punkty
 */
void* dice_thread_func(void *arg) {
    #define PLAYER_COUNT 4
    #define ROUNDS 10
    
    struct dice_arguments {
        int id;
        unsigned int seed;
        int* scores;
        int* rolls;
        pthread_barrier_t *barrier;
    } *args = (struct dice_arguments *)arg;
    
    for (int round = 0; round < ROUNDS; ++round) {
        // Każdy gracz rzuca kością
        args->rolls[args->id] = 1 + rand_r(&args->seed) % 6;
        printf("player %d: Rolled %d.\n", args->id, args->rolls[args->id]);
        
        // Czekaj na wszystkich graczy (bariera)
        int result = pthread_barrier_wait(args->barrier);
        
        // Tylko jeden wątek (SERIAL_THREAD) przydziela punkty
        if(result == PTHREAD_BARRIER_SERIAL_THREAD) {
            printf("player %d: Assigning scores.\n", args->id);
            int max = -1;
            for (int i = 0; i < PLAYER_COUNT; ++i) {
                int roll = args->rolls[i];
                if(roll > max) {
                    max = roll;
                }
            }
            for (int i = 0; i < PLAYER_COUNT; ++i) {
                int roll = args->rolls[i];
                if(roll == max) {
                    args->scores[i] = args->scores[i] + 1;
                    printf("player %d: Player %d got a point.\n", args->id, i);
                }
            }
        }
        // Kolejna bariera - czekaj aż punkty zostaną przydzielone
        pthread_barrier_wait(args->barrier);
    }
    
    return NULL;
}

/*
 * Tworzenie wątków graczy z barierą synchronizacyjną
 * Każdy wątek otrzymuje unikalny seed dla generatora liczb losowych
 */
void create_dice_threads(pthread_t *thread, struct dice_arguments *targ, pthread_barrier_t *barrier, int *scores, int* rolls)
{
    #define PLAYER_COUNT 4
    
    struct dice_arguments {
        int id;
        unsigned int seed;
        int* scores;
        int* rolls;
        pthread_barrier_t *barrier;
    };
    
    srand(time(NULL));
    int i;
    for (i = 0; i < PLAYER_COUNT; i++)
    {
        targ[i].id = i;
        targ[i].seed = rand();
        targ[i].scores = scores;
        targ[i].rolls = rolls;
        targ[i].barrier = barrier;
        if (pthread_create(&thread[i], NULL, dice_thread_func, (void *)&targ[i]) != 0)
            ERR("pthread_create");
    }
}

// ========================= MUTEX + CONDITION VARIABLE FUNCTIONS (Thread-pool-sync.c) =========================

/*
 * Funkcja cleanup - odblokowanie mutex w przypadku anulowania wątku
 * Używana z pthread_cleanup_push/pop dla bezpiecznego zarządzania zasobami
 */
void cleanup(void *arg) { 
    pthread_mutex_unlock((pthread_mutex_t *)arg); 
}

/*
 * Funkcja wątku w puli wątków - używa mutex i zmiennej warunku
 * Wątki czekają na zadania, używając pthread_cond_wait
 */
void *threadpool_thread_func(void *arg)
{
    typedef struct {
        int id;
        int *idlethreads;
        int *condition;
        pthread_cond_t *cond;
        pthread_mutex_t *mutex;
    } thread_arg;
    
    thread_arg targ;
    memcpy(&targ, arg, sizeof(targ));
    
    while (1)
    {
        // Rejestracja funkcji cleanup w przypadku anulowania wątku
        pthread_cleanup_push(cleanup, (void *)targ.mutex);
        
        // Zablokuj mutex przed dostępem do współdzielonych danych
        if (pthread_mutex_lock(targ.mutex) != 0)
            ERR("pthread_mutex_lock");
        
        // Zwiększ licznik bezczynnych wątków
        (*targ.idlethreads)++;
        
        // Czekaj na sygnał warunku lub zakończenie pracy
        while (!*targ.condition && 1 /* work */)
            if (pthread_cond_wait(targ.cond, targ.mutex) != 0)
                ERR("pthread_cond_wait");
        
        // Reset warunku
        *targ.condition = 0;
        if (!1 /* work */)
            pthread_exit(NULL);
            
        // Zmniejsz licznik bezczynnych wątków
        (*targ.idlethreads)--;
        
        // Odblokuj mutex i wywołaj cleanup
        pthread_cleanup_pop(1);
        
        // Wykonaj pracę (tu: read_random)
        printf("Thread %d is working\n", targ.id);
    }
    return NULL;
}

/*
 * Inicjalizacja puli wątków - tworzy wątki robocze
 * Każdy wątek otrzymuje referencje do współdzielonych struktur synchronizacyjnych
 */
void init_threadpool(pthread_t *thread, void *targ, pthread_cond_t *cond, pthread_mutex_t *mutex, 
                    int *idlethreads, int *condition)
{
    #define THREAD_NUM 3
    
    typedef struct {
        int id;
        int *idlethreads;
        int *condition;
        pthread_cond_t *cond;
        pthread_mutex_t *mutex;
    } thread_arg;
    
    thread_arg *args = (thread_arg*)targ;
    int i;
    
    for (i = 0; i < THREAD_NUM; i++)
    {
        args[i].id = i + 1;
        args[i].cond = cond;
        args[i].mutex = mutex;
        args[i].idlethreads = idlethreads;
        args[i].condition = condition;
        if (pthread_create(&thread[i], NULL, threadpool_thread_func, (void *) &args[i]) != 0)
            ERR("pthread_create");
    }
}

/*
 * Główna funkcja pracy z pulą wątków - zarządza zadaniami
 * Używa mutex do synchronizacji dostępu do współdzielonych danych
 */
void do_threadpool_work(pthread_cond_t *cond, pthread_mutex_t *mutex, const int *idlethreads, int *condition)
{
    #define BUFFERSIZE 256
    char buffer[BUFFERSIZE];
    
    while (1 /* work */)
    {
        if (fgets(buffer, BUFFERSIZE, stdin) != NULL)
        {
            // Zablokuj mutex przed sprawdzeniem dostępności wątków
            if (pthread_mutex_lock(mutex) != 0)
                ERR("pthread_mutex_lock");
                
            if (*idlethreads == 0)
            {
                // Brak dostępnych wątków - odblokuj mutex
                if (pthread_mutex_unlock(mutex) != 0)
                    ERR("pthread_mutex_unlock");
                fputs("No threads available\n", stderr);
            }
            else
            {
                // Wątki dostępne - odblokuj mutex i wyślij sygnał
                if (pthread_mutex_unlock(mutex) != 0)
                    ERR("pthread_mutex_unlock");
                *condition = 1;
                if (pthread_cond_signal(cond) != 0)
                    ERR("pthread_cond_signal");
            }
        }
        else
        {
            if (EINTR == errno)
                continue;
            ERR("fgets");
        }
    }
}

// ========================= MAIN FUNCTION EXAMPLES =========================

/*
 * Przykład użycia puli wątków z mutex i zmiennymi warunków
 */
int threadpool_main_example()
{
    #define THREAD_NUM 3
    
    typedef struct {
        int id;
        int *idlethreads;
        int *condition;
        pthread_cond_t *cond;
        pthread_mutex_t *mutex;
    } thread_arg;
    
    int i, condition = 0, idlethreads = 0;
    pthread_t thread[THREAD_NUM];
    thread_arg targ[THREAD_NUM];
    
    // Inicjalizacja struktur synchronizacyjnych
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    
    // Inicjalizacja i uruchomienie wątków
    init_threadpool(thread, targ, &cond, &mutex, &idlethreads, &condition);
    do_threadpool_work(&cond, &mutex, &idlethreads, &condition);
    
    // Wybudzenie wszystkich wątków przed zakończeniem
    if (pthread_cond_broadcast(&cond) != 0)
        ERR("pthread_cond_broadcast");
        
    // Czekanie na zakończenie wszystkich wątków
    for (i = 0; i < THREAD_NUM; i++)
        if (pthread_join(thread[i], NULL) != 0)
            ERR("pthread_join");
            
    return EXIT_SUCCESS;
}

/*
 * Przykład użycia pthread_barrier w grze w kości
 */
int dice_game_main_example() 
{
    #define PLAYER_COUNT 4
    
    struct dice_arguments {
        int id;
        unsigned int seed;
        int* scores;
        int* rolls;
        pthread_barrier_t *barrier;
    };
    
    pthread_t threads[PLAYER_COUNT];
    struct dice_arguments targ[PLAYER_COUNT];
    int scores[PLAYER_COUNT] = {0};
    int rolls[PLAYER_COUNT];
    pthread_barrier_t barrier;
    
    // Inicjalizacja bariery dla wszystkich graczy
    pthread_barrier_init(&barrier, NULL, PLAYER_COUNT);
    
    // Tworzenie wątków graczy
    create_dice_threads(threads, targ, &barrier, scores, rolls);
    
    // Czekanie na zakończenie wszystkich graczy
    for (int i = 0; i < PLAYER_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Wyświetlenie wyników
    puts("Scores: ");
    for (int i = 0; i < PLAYER_COUNT; ++i) {
        printf("ID %d: %i\n", i, scores[i]);
    }
    
    // Zwolnienie zasobów bariery
    pthread_barrier_destroy(&barrier);
    
    return 0;
}

/*
 * PODSUMOWANIE ZASTOSOWANYCH MECHANIZMÓW SYNCHRONIZACJI:
 * 
 * 1. SEMAPHORE (sem_t):
 *    - sem_init(): inicjalizacja semafora z określoną wartością
 *    - sem_trywait(): nieblokująca próba zajęcia semafora
 *    - sem_post(): zwolnienie semafora
 *    - Zastosowanie: ograniczenie liczby jednocześnie działających zasobów
 * 
 * 2. PTHREAD BARRIER (pthread_barrier_t):
 *    - pthread_barrier_init(): inicjalizacja bariery dla N wątków
 *    - pthread_barrier_wait(): czekanie na wszystkie wątki w punkcie synchronizacji
 *    - pthread_barrier_destroy(): zwolnienie zasobów bariery
 *    - Zastosowanie: synchronizacja wątków w określonych punktach wykonania
 * 
 * 3. MUTEX + CONDITION VARIABLE (pthread_mutex_t + pthread_cond_t):
 *    - pthread_mutex_lock/unlock(): blokowanie/odblokowywanie krytycznej sekcji
 *    - pthread_cond_wait(): czekanie na sygnał warunku (automatycznie zwalnia mutex)
 *    - pthread_cond_signal/broadcast(): wysłanie sygnału do jednego/wszystkich czekających wątków
 *    - pthread_cleanup_push/pop(): bezpieczne zarządzanie zasobami przy anulowaniu wątku
 *    - Zastosowanie: implementacja wzorca producent-konsument, pule wątków
 */
