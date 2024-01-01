
#define FD_SETSIZE 2048

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

#include <sys/param.h>

#include <stdio.h>

#define BLOCK_SIZE    1024

void print_hex(const u_char * buf, size_t len) {

    for (int i = 0; i < len; i++) {
        if (i % 32 == 31) {
            fprintf(stdout, "\n");
        }
        char c = (u_char) buf[i];
        if (c < 32 || c > 126) {
            c = '.';
        }

        fprintf(stdout, "%c", c);
    }

    for (int i = 0; i < len; i++) {
        if (i % 16 == 0) {
            fprintf(stdout, "\n");
        }

        fprintf(stdout, "%02x ", buf[i]);
    }

    fprintf(stdout, "\n");
}


void send_file (struct sockaddr_in dst_addr, const char *path, size_t total_retransmits, size_t range_size) {

    unsigned long long block_id = 0;

    int sock;
    if ( (sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed"); 
        exit(EXIT_FAILURE); 
    }


    //newbuf[0] = tag;
    //newbuf[1] = proto;

    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("could not open file");
        exit(EXIT_FAILURE);
    }

    bool completed = false;

    while (1) {
        for (size_t transmit_id = 0; transmit_id < total_retransmits + 1; transmit_id++) {

            u_char *range = malloc(range_size * BLOCK_SIZE);

            // read into blocks
            size_t current_range_start = lseek(fd, 0, SEEK_CUR);
            size_t current_range_bytes = read(fd, (void *)range, range_size * BLOCK_SIZE);

            // no more blocks remaining - range is at end of the file
            if (current_range_bytes == 0) {
                completed = true;
                break;
            }

            // TODO should not send empty block (before ending block) when file is exactly block size

            for (size_t range_block_id = 0; range_block_id < range_size; range_block_id++) {
                u_char newbuf[BLOCK_SIZE+4];

                // starting position for this iteration within the range
                size_t startpos = range_block_id * BLOCK_SIZE;

                // for a "truncated" range (at end of file) - we have reached the end of the file
                // retransmits will continue
                if (startpos > current_range_bytes) {
                    break;
                }

                size_t block_id = current_range_start / 1024 + range_block_id;

                for (int i = 0; i < 4; i++) {
                    newbuf[i] = (block_id >> (i*8)) & 0xFF;
                }

                memcpy(newbuf+4, (const void *)(range + range_block_id * BLOCK_SIZE), BLOCK_SIZE);


                size_t block_len = startpos + BLOCK_SIZE > current_range_bytes 
                    ? current_range_bytes - startpos : BLOCK_SIZE;

                //print_hex(newbuf, block_len + 4);

                int n = sendto(sock, newbuf, block_len + 4, 0, 
                    (const struct sockaddr *)&dst_addr, 
                    sizeof(dst_addr)
                );
                if (n < 0) {
                    perror("send");
                    exit(EXIT_FAILURE);
                }

                fprintf(stdout, "transmit_id=%lu block_id=%lu sent %d bytes\n", transmit_id, block_id, n);

                usleep(20000);
            }

            // only call seek when needed - when we have finished a range of blocks and 
            // still have at least one retransmission remaining
            if (transmit_id != total_retransmits) {
                lseek(fd, current_range_start, SEEK_SET);
            }

            free(range);
        }

        if (completed == true) {

            int n = sendto(sock, NULL, 0, 0, 
                (const struct sockaddr *)&dst_addr, 
                sizeof(dst_addr)
            );

            if (n < 0) {
                perror("send");
                exit(EXIT_FAILURE);
            }

            fprintf(stdout, "completed; sent final empty packet\n");

            break;
        }
    }


}


void recv_file(struct sockaddr_in bind_to, const char *path) {

    int fd = open(path, O_WRONLY|O_CREAT, 0770);
    if (fd == -1) {
        perror("could not open file");
        exit(EXIT_FAILURE);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket creation failed"); 
        exit(EXIT_FAILURE); 
    }

    socklen_t portlen = sizeof(struct sockaddr_in);
    int ret = bind(sock, (const struct sockaddr *)&bind_to, sizeof(struct sockaddr_in));

    if (ret != 0) {
        close(fd);
        fprintf(stderr, "bind: %s", strerror(ret));
        exit(2);
    }

    fprintf(stdout, "listening on port %d\n", ntohs(bind_to.sin_port));

    u_char buf[1032];
    u_char block[1024];

    while (1) {

        int len = recvfrom(sock, (void *)buf, 1032, 0,
            (struct sockaddr *)&bind_to, &portlen);

        // record of zero length means transfer is completed
        if (len == 0) {
            fprintf(stdout, "received zero-length datagram, exiting\n");
            break;
        }

        unsigned long block_id = 0;
        for (int i = 0; i < 4; i++) {
            block_id += buf[i] << (i*8);
        }

        if (len < 4) {
            // TODO
            break;
        }

        size_t block_len = len - 4;

        print_hex(buf, len);


        memcpy(block, buf+4, block_len);

        fprintf(stdout, "block_id=%lu block_len=%lu\n", block_id, block_len);

        lseek(fd, block_id * BLOCK_SIZE, SEEK_SET);
        write(fd, (const void *)(buf+4), block_len);


        // optionally, skip blocks already written (do later)

    }

}

enum mode { SEND, RECEIVE };
 

int main(int argc, char **argv)
{
    enum mode m = SEND;
    int portnum = 0;
    char ip_str[15];
    ip_str[0] = '\0';
    in_addr_t ip_addr;
    char *path_str = NULL;

    int total_retransmits = -1;
    int range_size = -1;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--receive") == 0) {
            m = RECEIVE;
        }

        if (strcmp(argv[i], "--port") == 0 && i < argc - 1) {
            portnum = strtol(argv[i+1], NULL, 10);
        }

        if (strcmp(argv[i], "--total-retransmits") == 0 && i < argc - 1) {
            total_retransmits = strtol(argv[i+1], NULL, 10);
        }

        if (strcmp(argv[i], "--range-size") == 0 && i < argc - 1) {
            range_size = strtol(argv[i+1], NULL, 10);
        }

        if (strcmp(argv[i], "--address") == 0 && i < argc - 1) {
            strcpy(ip_str, argv[i+1]);
        }

        if (strcmp(argv[i], "--path") == 0 && i < argc - 1) {
            int len = strlen(argv[i+1]);
            path_str = malloc(len);
            strcpy(path_str, argv[i+1]);
        }
    }

    if (strlen(ip_str) != 0) {
        ip_addr = inet_addr((const char *) ip_str);
    } else {

        if (m == SEND) {
            perror("invalid IP address");
            exit(EXIT_FAILURE);
        }

        ip_addr = INADDR_ANY;
    }

    if (portnum == 0) {
        perror("invalid port");
        exit(EXIT_FAILURE);
    }

    if (path_str == NULL) {
        perror("invalid path");
        exit(EXIT_FAILURE);
    }

    if (m == SEND) {
        if (total_retransmits == -1) {
            exit(EXIT_FAILURE);
        }

        if (range_size == -1) {
            exit(EXIT_FAILURE);
        }
    }


    char action_str[20];
    if (m == SEND)
        strcpy(action_str, "sending to");
    else 
        strcpy(action_str, "listening on");

    fprintf(stdout, "%s %s:%u\n", action_str, (const char *)ip_str, portnum);

    struct sockaddr_in dst_addr;
    memset((void *)&dst_addr, 0, sizeof(struct sockaddr_in));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_addr.s_addr = ip_addr;
    dst_addr.sin_port = htons(portnum);


    struct sockaddr_in recv_addr;
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(portnum);
    recv_addr.sin_addr.s_addr = ip_addr;

    if (m == SEND) {
        send_file (dst_addr, path_str, total_retransmits, range_size);
    } else {
        recv_file (recv_addr, path_str);
    }



    return 0;
}
