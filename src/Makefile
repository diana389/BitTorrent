build:
	mpicc -o tema3 tema3.c -pthread -Wall

run:
	mpirun --oversubscribe -np 6 ./tema3
	
clean:
	rm -rf tema3
	rm -r client*
