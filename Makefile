$(CC) = gcc


final:
	$(CC) -Wall main.c `pkg-config fuse3 --cflags --libs` -o mifuse

Clean:
	rm *.o mifuse

