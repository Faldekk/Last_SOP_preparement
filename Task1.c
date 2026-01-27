#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

/* ===================== STRUKTURY ===================== */

typedef struct {
    off_t start;
    size_t size;
} fragment_t;

typedef struct node {
    char *line;
    struct node *prev, *next;
} node_t;

typedef struct {
    node_t *head, *tail;
} list_t;

/* ===================== GLOBALNE ===================== */

fragment_t *tasks;
int task_count;
int next_task = 0;

pthread_mutex_t task_mutex = PTHREAD_MUTEX_INITIALIZER;

volatile int error_flag = 0;
long error_line = -1;

pthread_barrier_t error_barrier;

int active_threads;
pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;

const char *path;

/* ===================== LISTA ===================== */

// Inicjalizuje pustą listę dwukierunkową
void list_init(list_t *l) {
    l->head = l->tail = NULL;
}

// Dodaje nową linię na koniec listy (kopiuje string)
void list_push(list_t *l, const char *line) {
    node_t *n = malloc(sizeof(node_t));
    n->line = strdup(line);
    n->next = NULL;
    n->prev = l->tail;
    if (l->tail) l->tail->next = n;
    else l->head = n;
    l->tail = n;
}

// Łączy dwie listy - dodaje całą listę src na koniec dst
void list_append(list_t *dst, list_t *src) {
    if (!src->head) return;
    if (!dst->head) {
        *dst = *src;
    } else {
        dst->tail->next = src->head;
        src->head->prev = dst->tail;
        dst->tail = src->tail;
    }
}

/* ===================== CSV ===================== */

// Sprawdza czy linia to poprawny format CSV (dokładnie jeden przecinek)
int valid_csv_line(const char *line) {
    int commas = 0;
    for (const char *p = line; *p; p++)
        if (*p == ',') commas++;
    return commas == 1;
}

/* ===================== WORKER ===================== */

// Funkcja wątku roboczego - przetwarza fragmenty pliku CSV
// Sprawdza poprawność linii i dodaje je do lokalnej listy
void *worker(void *arg) {
    list_t *local = arg;

    while (1) {
        fragment_t frag;

        pthread_mutex_lock(&task_mutex);
        if (next_task >= task_count || error_flag) {
            pthread_mutex_unlock(&task_mutex);
            break;
        }
        frag = tasks[next_task++];
        pthread_mutex_unlock(&task_mutex);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        fseeko(f, frag.start, SEEK_SET);

        char *line = NULL;
        size_t len = 0;
        ssize_t r;
        long line_no = 0;
        size_t read_bytes = 0;

        while ((r = getline(&line, &len, f)) != -1) {
            read_bytes += r;
            line_no++;

            if (!valid_csv_line(line)) {
                pthread_mutex_lock(&active_mutex);
                error_flag = 1;
                error_line = line_no;
                pthread_mutex_unlock(&active_mutex);
                break;
            }
            list_push(local, line);
            if (read_bytes >= frag.size) break;
        }

        free(line);
        fclose(f);

        if (error_flag) break;
    }

    pthread_mutex_lock(&active_mutex);
    active_threads--;
    pthread_mutex_unlock(&active_mutex);

    if (error_flag)
        pthread_barrier_wait(&error_barrier);

    return NULL;
}

/* ===================== MAIN ===================== */

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s n m path\n", argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);
    int m = atoi(argv[2]);
    path = argv[3];

    FILE *f = fopen(path, "r");
    if (!f) {
        perror("fopen");
        return 1;
    }

    /* nagłówek */
    char *header = NULL;
    size_t hlen = 0;
    getline(&header, &hlen, f);
    off_t data_start = ftello(f);

    struct stat st;
    stat(path, &st);
    off_t data_size = st.st_size - data_start;

    /* fragmenty */
    tasks = calloc(m, sizeof(fragment_t));
    size_t chunk = data_size / m;
    for (int i = 0; i < m; i++) {
        tasks[i].start = data_start + i * chunk;
        tasks[i].size  = (i == m - 1) ? data_size - i * chunk : chunk;
    }
    task_count = m;

    fclose(f);
    free(header);

    pthread_barrier_init(&error_barrier, NULL, n);
    active_threads = n;

    pthread_t threads[n];
    list_t lists[n];

    for (int i = 0; i < n; i++) {
        list_init(&lists[i]);
        pthread_create(&threads[i], NULL, worker, &lists[i]);
    }

    for (int i = 0; i < n; i++)
        pthread_join(threads[i], NULL);

    if (error_flag) {
        fprintf(stderr, "CSV error at line %ld\n", error_line);
        return 1;
    }

    /* łączenie list */
    list_t result;
    list_init(&result);
    for (int i = 0; i < n; i++)
        list_append(&result, &lists[i]);

    /* druk */
    for (node_t *p = result.head; p; p = p->next)
        printf("%s", p->line);

    return 0;
}
