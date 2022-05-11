/*
** server.c -- a stream socket server demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <malloc.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "Process.h"


#define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10     // how many pending connections queue will hold

#define NUM_CLIENTS 10 // max of clients

pStack head; // Stack
struct flock fl = {F_UNLCK, SEEK_SET, 0, 1024, 0};
int fd;
char s[1024];

void sigchld_handler(int s) {
    (void) s; // quiet unused variable warning

    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *) sa)->sin6_addr);
}


void push(char *str, pStack head) {
    fl.l_type = F_WRLCK;
    fcntl(fd, F_SETLKW, &fl); //wait for that lock to be released
    if (head->count == 1024) {
        printf("ERROR: Stack full\n");
        return;
    }
    for (int i = 0; i < strlen(str); ++i) {
        head->stack[head->count] = str[i]; //input data
        head->count++;
    }
    head->stack[head->count] = '\0'; //end of bullet
    printf("'%s' pushed to stack\n", str);
    head->count++;
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLKW, &fl);
}

void pop(pStack head) {
    bzero(s, 1024);
    fl.l_type = F_WRLCK;
    fcntl(fd, F_SETLKW, &fl); //wait for that lock to be released
    if (head->count <= 1) {
        printf("ERROR: Stack empty\n");
        fl.l_type = F_UNLCK;
        fcntl(fd, F_SETLKW, &fl);
        return;
    }
    int i = head->count;
    i-=2;
    while (head->stack[i] != '\0') {
        i--;
    }
    int tmp = i;
    i++;
    for (int j = 0; j < head->count; ++j) {
        s[j] = head->stack[i];
        i++;
    }
    printf("'%s' poped\n", s);
    head->count = tmp;
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLKW, &fl);
}

void top(pStack head) {
    bzero(s, 1024);
    fl.l_type = F_WRLCK;
    fcntl(fd, F_SETLKW, &fl); //wait for that lock to be released
    if (head->count <= 1) {
        printf("ERROR: Stack empty\n");
        fl.l_type = F_UNLCK;
        fcntl(fd, F_SETLKW, &fl);
        return;
    }
    printf("OUTPUT: ");
    int i = head->count;
    i-=2;
    while (head->stack[i] != '\0') {
        i--;
    }
    i++;
    for (int j = 0; j < head->count; ++j) {
        s[j] = head->stack[i];
        i++;
    }
    printf("%s\n", s);
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLKW, &fl);
}

int checkSUB(char e[], char s[]) {
    if (strlen(s) < strlen(e))
        return 0;
    for (int i = 0; i < strlen(e); ++i) {
        if (s[i] != e[i])
            return 0;
    }
    return 1;
}

void *myThreadFun(void *arg) {
    int new_fd = *(int *) arg;
    char text[1024];
    while (1) {
        char str[1024];
        bzero(str, 1024);
        bzero(text, 1024);
        int msglen;
        if ((msglen = recv(new_fd, text, 1024 - 1, 0)) == -1) {
            perror("recv error");
            exit(1);
        }
        if (!msglen) {
            printf("Client disconnect\n");
            close(new_fd);
            return NULL;
        }
        text[msglen] = '\0';
        printf("Received: '%s'\n", text);
        if (checkSUB("STOP", text)) {
            printf("See Ya");
            close(new_fd);
            close(fd);
            break;
        } else if (checkSUB("PUSH ", text)) {
            for (int i = 5; i < strlen(text); ++i) {
                str[i - 5] = text[i];
            }
            push(str, head);
        } //POP
        else if (checkSUB("POP", text)) {
            pop(head);
        } //TOP
        else if (checkSUB("TOP", text)) {
            top(head);
        } //STOP
    }
}


int main(void) {
    if ((fd = open("any.txt", O_WRONLY | O_CREAT)) == -1) {
        perror("Error opening file");
        exit(1);
    }
    memset(&fl, 0, sizeof(fl));
    head = (pStack) mmap(0, 4092, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANON, -1, 0);
    head->stack[0] = '\0';
    head->count++;
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");
    int n = 0;
    pthread_t thread[NUM_CLIENTS];
    while (1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *) &their_addr),
                  s, sizeof s);
        printf("server: got connection\n");
        int f = fork();
        if (!f) {
            myThreadFun(&new_fd);
            close(new_fd);
            exit(0);
        }
    }

    return 0;
}
