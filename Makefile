CC 		= gcc
CFLAGS 	= -lrt -lpthread
DEPS 	= lib.h 

OBJ_MAITRE 	= main_maitre.o source_maitre.o
OBJ_ESCLAV	= main_esclave.o source_esclave.o
OBJ_ESCLAV2	= main_esclave2.o source_esclave.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

maitre: $(OBJ_MAITRE)
	$(CC) -o $@ $^ $(CFLAGS)
	
esclave: $(OBJ_ESCLAV)
	$(CC) -o $@ $^ $(CFLAGS)

esclave2: $(OBJ_ESCLAV2)
	$(CC) -o $@ $^ $(CFLAGS)
		

clean:
	rm -f *.o maitre esclave esclave2
