#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

const int redis_port = 6379;
const char* localhost = "127.0.0.1";
const char* nil_object = "nil";
const int base_constant = 4096;


int parse_request(int num, char** args, char* result) {
    int current_size = base_constant;
    int counter = 0;
    memset(result, 0, base_constant);

    counter += sprintf(result, "*%d\r\n", num);
    for (int i = 0; i < num; ++i) {
        if (current_size - counter <= strlen(args[i]) + 15) {
            result = realloc(result, current_size + base_constant);
            if (result == NULL) {
                printf("%s\n", "realloc failed");
                return 0;
            }
            current_size += base_constant;
        }

        counter += sprintf(result + counter, "$%lu\r\n%s\r\n", strlen(args[i]), args[i]);
    }

    return counter;
}

void write_string(const char* src, char* dest) {
    strcpy(dest, src);
    unsigned long len = strlen(src);
    dest[len - 2] = 0;
    dest[len - 1] = 0; //remove \r\n
}

unsigned long write_array(const char* src, char* result) {
    unsigned long counter_write = 1;
    unsigned long counter_read = 0;
    char elem_type;
    int size;
    int buf_int;
    char buf_str[15];

    memset(buf_str, 0, 15);
    sscanf(src + counter_read, "*%d\r\n", &size);
    sprintf(buf_str, "*%d\r\n", size);
    counter_read += strlen(buf_str);

    if (size == -1) {
        strcpy(result, nil_object);
        return counter_read;
    }

    result[0] = '[';

    for (int i = 0; i < size; ++i) {
        elem_type = src[counter_read];
        switch (elem_type) {
            case '+':
                result[counter_write++] = '"';
                sscanf(src + counter_read, "+%s\r\n", result + counter_write);
                counter_read += strlen(result) - counter_write + 2;
                counter_write = strlen(result);
                result[counter_write++] = '"';
                break;
            case '-':
                sscanf(src + counter_read, "-%s\r\n", result + counter_write);
                counter_read += strlen(result) - counter_write + 2;
                counter_write = strlen(result);
                break;
            case ':':
                sscanf(src + counter_read, ":%d\r\n", &buf_int);
                sprintf(result + counter_write, "%d", buf_int);
                counter_read += strlen(result) - counter_write + 2;
                counter_write = strlen(result);
                break;
            case '$':
                memset(buf_str, 0, 15);
                sscanf(src + counter_read, "$%d\r\n", &buf_int);
                sprintf(buf_str, "%%%ds", buf_int);
                counter_read += strlen(buf_str) + 1;

                result[counter_write++] = '"';
                sscanf(src + counter_read, buf_str, result + counter_write);
                counter_read += strlen(result) - counter_write + 2;
                counter_write = strlen(result);
                result[counter_write++] = '"';
                break;
            case '*':
                counter_read += write_array(src + counter_read, result + counter_write);
                counter_write = strlen(result);
                break;
        }
        result[counter_write++] = ',';
        result[counter_write++] = ' ';
    }
    if (size > 0) {
        result[counter_write--] = 0;
        result[counter_write--] = 0;
    }
    result[counter_write] = ']';
    return counter_read;
}

void parse_answer(const char* answer, char* result) {
    char type = answer[0];

    int size;
    unsigned long size_len;
    char str_size[10];
    memset(str_size, 0, 10);


    switch (type) {
        case '+':
            write_string(answer + 1, result);
            break;
        case '-':
            write_string(answer + 1, result); //return error message
            break;
        case ':':
            write_string(answer + 1, result);
            break;
        case '$':
            sscanf(answer, "$%d\r\n", &size);
            if (size == -1) {
                strcpy(result, nil_object);
            }
            else {
                sprintf(str_size, "%d\r\n", size);
                size_len = strlen(str_size);
                write_string(answer + size_len + 1, result);
            }
            break;
        case '*':
            write_array(answer, result);
            break;
    }
}

int main(int argc, char *argv[]) {
    in_addr_t address = inet_addr(localhost);
    in_port_t port = htons(redis_port);

    struct sockaddr_in full_address;
    full_address.sin_family = AF_INET;
    full_address.sin_addr.s_addr = address;
    full_address.sin_port = port;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    connect(sock, (struct sockaddr*) &full_address, sizeof(full_address));

    char* str_request = (char*) malloc(base_constant);
    int len_of_str_request = parse_request(argc - 1, argv + 1, str_request);
    write(sock, str_request, len_of_str_request);

    char* str_answer = (char*) malloc(base_constant);
    memset(str_answer, 0, base_constant);
    int answer_size = base_constant;
    int read_bytes;
    while ((read_bytes = read(sock, str_answer + (answer_size - base_constant), base_constant)) > 0) {
        if (read_bytes < base_constant) break;
        answer_size += base_constant;
        str_answer = realloc(str_answer, answer_size);
        if (str_answer == NULL) {
            printf("%s\n", "realloc failed");
            goto end;
        }
        memset(str_answer + (answer_size - base_constant),0,  base_constant);
    }

    char* client_answer = malloc(answer_size);
    parse_answer(str_answer, client_answer);
    printf("%s", client_answer);

end:
    close(sock);
    free(client_answer);
    free(str_request);
    free(str_answer);
    return 0;
}