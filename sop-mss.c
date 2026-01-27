#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define DECK_SIZE (4 * 13)
#define HAND_SIZE (7)

volatile sig_atomic_t new_player = 0;
volatile sig_atomic_t game_over = 0;

typedef struct {
    int id;
    int cards[HAND_SIZE];
    int active;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready_to_end;
} player_t;

typedef struct {
    int max_players;
    int current_players;
    player_t *players;
    int deck[DECK_SIZE];
    pthread_mutex_t game_mutex;
    pthread_cond_t game_cond;
    int game_started;
    int winner_id;
} game_t;

game_t game;

// Obsługa sygnału SIGUSR1 - nowy gracz chce dołączyć
void sigusr1_handler(int sig) {
    (void)sig;
    new_player = 1;
}

// Obsługa sygnału SIGINT - kończy grę
void sigint_handler(int sig) {
    (void)sig;
    game_over = 1;
}

// Ustawia handler dla określonego sygnału
int set_handler(void (*f)(int), int sigNo) {
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (sigaction(sigNo, &act, NULL) == -1)
        return -1;
    return 0;
}

// Wyświetla informacje o poprawnym użyciu programu i kończy działanie
void usage(const char *program_name)
{
    fprintf(stderr, "USAGE: %s n\n", program_name);
    fprintf(stderr, "  n - maximum number of players (1-10)\n");
    fprintf(stderr, "\nGame description:\n");
    fprintf(stderr, "  - Send SIGUSR1 to add new player\n");
    fprintf(stderr, "  - Send SIGINT to end the game\n");
    fprintf(stderr, "  - Players try to collect ships (same suit cards)\n");
    exit(EXIT_FAILURE);
}

// Tasuje tablicę liczb całkowitych używając algorytmu Fisher-Yates
void shuffle(int *array, size_t n)
{
    if (n > 1)
    {
        size_t i;
        for (i = 0; i < n - 1; i++)
        {
            size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
            int t = array[j];
            array[j] = array[i];
            array[i] = t;
        }
    }
}

// Sprawdza czy gracz ma komplet kart tego samego koloru (warunek wygranej)
int check_winning_condition(player_t *player) {
    int suits[4] = {0}; // Hearts, Diamonds, Clubs, Spades
    int total_cards = 0;
    
    for (int i = 0; i < HAND_SIZE; i++) {
        if (player->cards[i] >= 0) {
            suits[player->cards[i] % 4]++;
            total_cards++;
        }
    }
    
    // Sprawdź czy którykolwiek kolor ma co najmniej 5 kart (większość)
    for (int i = 0; i < 4; i++) {
        if (suits[i] >= 5 && total_cards == HAND_SIZE) {
            return 1;
        }
    }
    return 0;
}

// Wypisuje karty gracza w czytelnym formacie
void print_player_cards(player_t *player) {
    const char *suits[] = {" of Hearts", " of Diamonds", " of Clubs", " of Spades"};
    const char *values[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "Jack", "Queen", "King", "Ace"};
    
    printf("Player %d cards: [", player->id);
    for (int i = 0; i < HAND_SIZE; i++) {
        if (player->cards[i] >= 0) {
            int suit = player->cards[i] % 4;
            int value = player->cards[i] / 4;
            printf("%s%s", values[value], suits[suit]);
            if (i < HAND_SIZE - 1) printf(", ");
        }
    }
    printf("]\n");
    fflush(stdout);
}

// Funkcja wątku gracza - główna logika gry
void* player_thread(void* arg) {
    player_t *player = (player_t*)arg;
    
    // Wypisz karty gracza
    print_player_cards(player);
    
    // Czekaj na rozpoczęcie gry
    pthread_mutex_lock(&game.game_mutex);
    while (!game.game_started && !game_over) {
        pthread_cond_wait(&game.game_cond, &game.game_mutex);
    }
    pthread_mutex_unlock(&game.game_mutex);
    
    // Główna pętla gry
    int move_count = 0;
    while (!game_over && game.winner_id == -1) {
        move_count++;
        
        // Sprawdź warunek wygranej
        if (check_winning_condition(player)) {
            pthread_mutex_lock(&game.game_mutex);
            if (game.winner_id == -1) {
                game.winner_id = player->id;
                printf("\n*** WINNER! ***\n");
                printf("My ships sails! Player %d wins after %d moves!\n", player->id, move_count);
                print_player_cards(player);
                
                // Powiadom pozostałych graczy o zakończeniu gry
                pthread_cond_broadcast(&game.game_cond);
            }
            pthread_mutex_unlock(&game.game_mutex);
            break;
        }
        
        // Przekaż losową kartę następnemu graczowi (po prawej)
        pthread_mutex_lock(&game.game_mutex);
        int next_player_id = (player->id + 1) % game.current_players;
        pthread_mutex_unlock(&game.game_mutex);
        
        if (next_player_id >= game.current_players) continue;
        
        player_t *next_player = &game.players[next_player_id];
        
        pthread_mutex_lock(&player->mutex);
        pthread_mutex_lock(&next_player->mutex);
        
        if (!game_over && game.winner_id == -1) {
            // Wybierz losową kartę
            int attempts = 0;
            int card_to_give = rand() % HAND_SIZE;
            while (player->cards[card_to_give] == -1 && attempts < HAND_SIZE) {
                card_to_give = (card_to_give + 1) % HAND_SIZE;
                attempts++;
            }
            
            // Jeśli nie ma kart do oddania, przejdź dalej
            if (attempts >= HAND_SIZE) {
                pthread_mutex_unlock(&next_player->mutex);
                pthread_mutex_unlock(&player->mutex);
                continue;
            }
            
            // Znajdź wolne miejsce u następnego gracza
            int empty_slot = -1;
            for (int i = 0; i < HAND_SIZE; i++) {
                if (next_player->cards[i] == -1) {
                    empty_slot = i;
                    break;
                }
            }
            
            if (empty_slot != -1) {
                next_player->cards[empty_slot] = player->cards[card_to_give];
                player->cards[card_to_give] = -1;
                printf("Player %d gave card to Player %d (move %d)\n", player->id, next_player_id, move_count);
            }
        }
        
        pthread_mutex_unlock(&next_player->mutex);
        pthread_mutex_unlock(&player->mutex);
        
        usleep(50000); // 50ms opóźnienie (szybsza gra)
    }
    
    pthread_mutex_lock(&player->mutex);
    player->ready_to_end = 1;
    pthread_mutex_unlock(&player->mutex);
    
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        usage(argv[0]);
    }
    
    int max_players = atoi(argv[1]);
    if (max_players <= 0 || max_players > 10) {
        fprintf(stderr, "Invalid number of players. Must be between 1 and 10\n");
        usage(argv[0]);
    }
    
    // Inicjalizacja gry
    game.max_players = max_players;
    game.current_players = 0;
    game.game_started = 0;
    game.winner_id = -1;
    game.players = calloc(max_players, sizeof(player_t));
    
    if (!game.players) ERR("calloc");
    
    // Inicjalizacja mutex i zmiennych warunkowych
    if (pthread_mutex_init(&game.game_mutex, NULL) != 0) ERR("pthread_mutex_init");
    if (pthread_cond_init(&game.game_cond, NULL) != 0) ERR("pthread_cond_init");
    
    for (int i = 0; i < max_players; i++) {
        game.players[i].id = i;
        game.players[i].active = 0;
        game.players[i].ready_to_end = 0;
        for (int j = 0; j < HAND_SIZE; j++) {
            game.players[i].cards[j] = -1;
        }
        if (pthread_mutex_init(&game.players[i].mutex, NULL) != 0) ERR("pthread_mutex_init");
        if (pthread_cond_init(&game.players[i].cond, NULL) != 0) ERR("pthread_cond_init");
    }
    
    // Ustawienie handlerów sygnałów
    if (set_handler(sigusr1_handler, SIGUSR1) == -1) ERR("set_handler SIGUSR1");
    if (set_handler(sigint_handler, SIGINT) == -1) ERR("set_handler SIGINT");
    
    printf("Game server started. PID: %d\n", getpid());
    printf("Send SIGUSR1 to add players, SIGINT to quit\n");
    
    srand(time(NULL));
    
    // Główna pętla serwera
    while (!game_over) {
        // Czekaj na sygnały
        pause();
        
        if (game_over) break;
        
        if (new_player) {
            new_player = 0;
            
            pthread_mutex_lock(&game.game_mutex);
            
            if (game.current_players < game.max_players && !game.game_started) {
                // Dodaj gracza
                player_t *player = &game.players[game.current_players];
                player->active = 1;
                
                // Inicjalizuj talię jeśli to pierwszy gracz
                if (game.current_players == 0) {
                    for (int i = 0; i < DECK_SIZE; i++) {
                        game.deck[i] = i;
                    }
                    shuffle(game.deck, DECK_SIZE);
                }
                
                // Rozdaj karty graczowi
                for (int i = 0; i < HAND_SIZE; i++) {
                    int card_index = game.current_players * HAND_SIZE + i;
                    if (card_index < DECK_SIZE) {
                        player->cards[i] = game.deck[card_index];
                    } else {
                        player->cards[i] = -1;
                    }
                }
                
                game.current_players++;
                printf("Player %d joined the game\n", player->id);
                
                // Utwórz wątek gracza
                if (pthread_create(&player->thread, NULL, player_thread, player) != 0) {
                    ERR("pthread_create");
                }
                
                // Jeśli wszyscy gracze dołączyli, rozpocznij grę
                if (game.current_players == game.max_players) {
                    game.game_started = 1;
                    printf("Game started with %d players!\n", game.current_players);
                    pthread_cond_broadcast(&game.game_cond);
                }
            } else {
                if (game.game_started) {
                    fprintf(stderr, "Game already started, cannot add more players\n");
                } else {
                    fprintf(stderr, "Table full, cannot add more players\n");
                }
            }
            
            pthread_mutex_unlock(&game.game_mutex);
        }
    }
    
    // Kończy grę - powiadom wszystkich graczy
    pthread_mutex_lock(&game.game_mutex);
    game_over = 1;
    pthread_cond_broadcast(&game.game_cond);
    pthread_mutex_unlock(&game.game_mutex);
    
    // Czekaj na zakończenie wątków graczy
    for (int i = 0; i < game.current_players; i++) {
        if (game.players[i].active) {
            pthread_join(game.players[i].thread, NULL);
        }
    }
    
    printf("Game ended.\n");
    
    // Zwolnij zasoby
    pthread_mutex_destroy(&game.game_mutex);
    pthread_cond_destroy(&game.game_cond);
    for (int i = 0; i < max_players; i++) {
        pthread_mutex_destroy(&game.players[i].mutex);
        pthread_cond_destroy(&game.players[i].cond);
    }
    free(game.players);
    
    return 0;
}
