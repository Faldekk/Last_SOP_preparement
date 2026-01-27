override CFLAGS=-Wall -Wextra -fanalyzer -g -O0 -fsanitize=address,undefined

ifdef CI
override CFLAGS=-Wall -Wextra -Werror
endif

.PHONY: clean all

all: sop-mss clock-sync dice-sync task1 thread-pool-sync

sop-mss: sop-mss.c
	gcc $(CFLAGS) -o sop-mss sop-mss.c

clock-sync: Clock-sync.c
	gcc $(CFLAGS) -lpthread -o clock-sync Clock-sync.c

dice-sync: Dice-sync.c
	gcc $(CFLAGS) -lpthread -o dice-sync Dice-sync.c

task1: Task1.c
	gcc $(CFLAGS) -lpthread -o task1 Task1.c

thread-pool-sync: Thread-pool-sync.c
	gcc $(CFLAGS) -lpthread -o thread-pool-sync Thread-pool-sync.c

clean:
	rm -f sop-mss clock-sync dice-sync task1 thread-pool-sync
