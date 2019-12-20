#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

const char* nil_object = "nil";
const int32_t base_constant = 4096;
const char* localhost = "127.0.0.1";
const int32_t default_redis_port = 6379;

//переписывает клиентский запрос в соответсвии с протоколом Redis, вычленяя значения localhost и port,
//которые могут быть переданы по флагам
int32_t parse_request(int num, char** args, char* result, char* host, int32_t* redis_port) {
    int32_t current_size = base_constant;
    int32_t counter = 0;
    int32_t host_index = num;
    int32_t port_index = num;
    int32_t num_to_write = num; //число элементов Array может отличаться от переданного на число флагов


    memset(host, 0, 15);
    memset(result, 0, base_constant);

    for (int32_t i = 0; i < num; ++i) {
        if (sscanf(args[i], "-address=%s", host) == 1) {
            host_index = i;
            num_to_write--;
        }
        if (sscanf(args[i], "-port=%d", redis_port) == 1) {
            port_index = i;
            num_to_write--;
        }
    }
    if (strlen(host) == 0) {
        strncpy(host, localhost, strlen(localhost));
    }

    counter += sprintf(result, "*%d\r\n", num_to_write);
    for (int i = 0; i < num; ++i) {
        if (i != host_index && i != port_index) {
            if (current_size - counter <= strlen(args[i]) + 15) {
                result = realloc(result, current_size + base_constant);
                if (result == NULL) {
                    perror("realloc failed");
                    return 0;
                }
                current_size += base_constant;
            }
            counter += sprintf(result + counter, "$%lu\r\n%s\r\n", strlen(args[i]), args[i]);
        }
    }

    return counter;
}

//копирует строку из src в dest, удаляя последние \r\n
void write_string(const char* src, char* dest) {
    uint64_t len = strlen(src);
    strncpy(dest, src, len);
    dest[len - 2] = 0;
    dest[len - 1] = 0;
}

//записывает RESP Array в строку в человекочитаемом виде
uint64_t write_array(const char* src, char* result) {
    uint64_t counter_write = 1;
    uint64_t counter_read = 0;
    char elem_type;
    int32_t size; //может быть -1
    int32_t buf_int;
    uint64_t current_len;
    char buf_str[15];

    memset(buf_str, 0, 15);
    sscanf(src + counter_read, "*%d\r\n", &size);
    sprintf(buf_str, "*%d\r\n", size);
    counter_read += strlen(buf_str);

    if (size == -1) {
        strncpy(result, nil_object, strlen(nil_object));
        return counter_read;
    }

    result[0] = '[';

    for (int32_t i = 0; i < size; ++i) {
        elem_type = src[counter_read];
        switch (elem_type) {
            case '+':
                result[counter_write++] = '"';
                sscanf(src + counter_read, "+%s\r\n", result + counter_write);
                current_len = strlen(result);
                counter_read += current_len - counter_write + 2;
                counter_write = current_len;
                result[counter_write++] = '"';
                break;
            case '-':
                sscanf(src + counter_read, "-%s\r\n", result + counter_write);
                current_len = strlen(result);
                counter_read += current_len - counter_write + 2;
                counter_write = current_len;
                break;
            case ':':
                sscanf(src + counter_read, ":%d\r\n", &buf_int);
                sprintf(result + counter_write, "%d", buf_int);
                current_len = strlen(result);
                counter_read += current_len - counter_write + 2;
                counter_write = current_len;
                break;
            case '$':
                memset(buf_str, 0, 15);
                sscanf(src + counter_read, "$%d\r\n", &buf_int);
                sprintf(buf_str, "%%%ds", buf_int);
                counter_read += strlen(buf_str) + 1;

                result[counter_write++] = '"';
                sscanf(src + counter_read, buf_str, result + counter_write);
                current_len = strlen(result);
                counter_read += current_len - counter_write + 2;
                counter_write = current_len;
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

//преобразует ответ сервера в человекочитаемую строку
void parse_answer(const char* answer, char* result) {
    char type = answer[0];

    int32_t size; //может быть -1
    uint64_t size_len;
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
                strncpy(result, nil_object, strlen(nil_object));
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
    char* str_request = (char*) malloc(base_constant);
    char* str_answer = (char*) malloc(base_constant);
    char* client_answer = (char*) malloc(base_constant);
    char* host = (char*) malloc(15);
    if (str_answer == NULL || client_answer == NULL || str_request == NULL || host == NULL) {
        printf("%s\n", "malloc failed");
        goto end;
    }
    int32_t redis_port = default_redis_port;

    int32_t len_of_str_request = parse_request(argc - 1, argv + 1, str_request, host ,&redis_port);

    in_addr_t address = inet_addr(host);
    in_port_t port = htons(redis_port);

    struct sockaddr_in full_address;
    full_address.sin_family = AF_INET;
    full_address.sin_addr.s_addr = address;
    full_address.sin_port = port;

    int32_t sock = socket(AF_INET, SOCK_STREAM, 0); //создаём клиентский сокет
    int32_t con_ret_value = connect(sock, (struct sockaddr*) &full_address, sizeof(full_address));
    if (con_ret_value == -1) {
        perror("connection failed");
        goto end;
    }

    write(sock, str_request, len_of_str_request); //отправляем запрос

    memset(str_answer, 0, base_constant);
    int32_t answer_size = base_constant;
    int32_t read_bytes;
    //читаем ответ
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
    if (read_bytes == -1) {
        perror("read failed");
        goto end;
    }

    client_answer = realloc(client_answer, answer_size);
    if (client_answer == NULL) {
        perror("realloc failed");
        goto end;
    }
    parse_answer(str_answer, client_answer); //приводим результат к человекочитемому виду и выводим его
    printf("%s", client_answer);

end:
    close(sock);
    free(host);
    free(client_answer);
    free(str_request);
    free(str_answer);
    return 0;
}
