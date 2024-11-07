#ifndef POLL_H_
#define POLL_H_

#include <poll.h>

void add_to_pfds(struct pollfd *pfds[], int newfd, int *fd_count, int *fd_size);

void del_from_pfds(struct pollfd pfds[], int i, int *fd_count);

#endif
