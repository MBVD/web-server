#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

typedef struct User {
    char* username;
    char* password;
    struct User* next;
} User;

User* users = NULL;

User* create_user(const char* username, const char* password){
    User* tmp = malloc(sizeof(User));
    tmp->username = malloc(strlen(username) + 1);
    tmp->password = malloc(strlen(password) + 1);
    tmp->next = NULL;
    strcpy(tmp->username, username);
    strcpy(tmp->password, password);
    return tmp;
}

User* find_user(const char* username){
    User* tmp = users;
    while (tmp != NULL){
        if (!strcmp(tmp->username, username)){
            return tmp;
        }
        tmp = tmp->next;
    }
    return NULL;
}

int user_count(){
    User* tmp = users;
    int count = 0;
    while(tmp != NULL){
        tmp = tmp -> next;
        count++;
    }
    return count;
}

int add_user(const char* username, const char* password){
    if (find_user(username) != NULL){
        return -1;
    }
    User* new_user = create_user(username, password);
    if (users == NULL){
        users = new_user;
    } else {
        User* tmp = users;
        while (tmp->next != NULL){
            tmp = tmp->next;
        }
        tmp->next = new_user;
    }
    printf("NEW USER - USER COUNT = %d \n", user_count());
    return 0;
}

void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void parse_post_data(const char *data, char *username, char *password) {
    char *username_start = strstr(data, "username=");
    char *password_start = strstr(data, "password=");

    if (!username_start || !password_start) {
        username[0] = '\0';
        password[0] = '\0';
        return;
    }

    username_start += strlen("username=");
    char *username_end = strchr(username_start, '&');
    if (username_end) {
        strncpy(username, username_start, username_end - username_start);
        username[username_end - username_start] = '\0';
    } else {
        strcpy(username, username_start);
    }

    password_start += strlen("password=");
    char *password_end = strchr(password_start, '&');
    if (password_end) {
        strncpy(password, password_start, password_end - password_start);
        password[password_end - password_start] = '\0';
    } else {
        strcpy(password, password_start);
    }
    
    username[strcspn(username, "\r\n")] = '\0';
    password[strcspn(password, "\r\n")] = '\0';

    url_decode(username, username);
    url_decode(password, password);
}

int send_file(int client_socket, const char* filename){
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("cant search file");
        return -1;
    }
    char buffer[BUFFER_SIZE];
    const char *headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "\r\n";
    send(client_socket, headers, strlen(headers), 0);
    while (fgets(buffer, sizeof(buffer), file)) {
        send(client_socket, buffer, strlen(buffer), 0);
    }
    fclose(file);
    return 0;
}

void* handle_client(void* arg) {
    int client_socket = *(int*)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    read(client_socket, buffer, BUFFER_SIZE);
    printf("Received request:\n%s\n", buffer);

    if (strstr(buffer, "GET /main") != NULL) {
        send_file(client_socket, "views/main.html");
    } else if (strstr(buffer, "GET /register") != NULL) {
        send_file(client_socket, "views/register.html");
    } else if (strstr(buffer, "POST /register")) {
        const char *data = strstr(buffer, "\r\n\r\n") + 4;
        char username[256], password[256] = {0};
        parse_post_data(data, username, password);
        printf("Register - Username: %s, Password: %s\n", username, password);
        int flag;
        if (!(flag = add_user(username, password))){
            printf("User added successfully\n");
            send_file(client_socket, "views/main.html");
        } else {
            printf("User already exists\n");
            send_file(client_socket, "views/login.html");
        }   
    } else if (strstr(buffer, "GET /login") != NULL) {
        send_file(client_socket, "views/login.html");
    } else if (strstr(buffer, "POST /login") != NULL) {
        const char *data = strstr(buffer, "\r\n\r\n") + 4;
        char username[256], password[256] = {0};
        parse_post_data(data, username, password);
        printf("Login - Username: %s, Password: %s\n", username, password);
        User* user = find_user(username);
        if (user != NULL) {
            printf("Stored Password: %s\n", user->password);
        }
        if (user != NULL && strstr(password, user->password) != NULL){
            printf("User authentication successful\n");
            send_file(client_socket, "views/main.html");
        } else {
            printf("Invalid password or login\n");
            send_file(client_socket, "views/login.html");
        }
    } else {
        send_file(client_socket, "views/404.html");
    }
    close(client_socket);
    return NULL;
}

int main() {
    int server_fd, *client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %d\n", PORT);

    while (1) {
        client_socket = malloc(sizeof(int));
        if ((*client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept failed");
            free(client_socket);
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_socket) != 0) {
            perror("pthread_create failed");
            free(client_socket);
        }
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}