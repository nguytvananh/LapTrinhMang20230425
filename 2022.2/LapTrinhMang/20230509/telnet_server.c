#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <unistd.h>

#define MAX_CLIENTS 10
#define MAX_MSG_LEN 1024

typedef struct client
{
    int sockfd;
    struct sockaddr_in addr;
    char username[MAX_MSG_LEN];
    char password[MAX_MSG_LEN];
} client_t;

int authenticate_user(char *username, char *password)
{
    FILE *fp = fopen("dtb.txt", "r");
    if (fp == NULL)
    {
        return -1;
    }

    char line[MAX_MSG_LEN];
    while (fgets(line, MAX_MSG_LEN, fp) != NULL)
    {
        line[strcspn(line, "\n")] = 0;

        //tách username và password
        char stored_username[MAX_MSG_LEN];
        char stored_password[MAX_MSG_LEN];
        sscanf(line, "%s %s", stored_username, stored_password);

        //so sánh file thông tin 
        if (strcmp(username, stored_username) == 0 && strcmp(password, stored_password) == 0)
        {
            fclose(fp);
            return 1; 
        }
    }
    fclose(fp);
    return 0; 
}

void handle_command(int sockfd, char *command)
{
    char cmd[MAX_MSG_LEN];
    sprintf(cmd, "%s > out.txt", command);

    int status = system(cmd);
    if (status == 0)
    {
        FILE *fp = fopen("out.txt", "r");
        if (fp == NULL)
        {
            return;
        }

        char line[MAX_MSG_LEN];
        while (fgets(line, MAX_MSG_LEN, fp) != NULL)
        {
            if (send(sockfd, line, strlen(line), 0) < 0)
            {
                perror("send() failed");
                continue;
            }
        }
        char *msg = "\nEnter your command: ";
        if (send(sockfd, msg, strlen(msg), 0) < 0)
        {
            perror("send() failed");
            return;
        }
        fclose(fp);
    }
    else
    {
        char *msg = "Command not found!\nEnter another command: ";
        if (send(sockfd, msg, strlen(msg), 0) < 0)
        {
            perror("send() failed");
            return;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket() failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(atoi(argv[1]));

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind() failed");
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, MAX_CLIENTS) < 0)
    {
        perror("listen() failed");
        exit(EXIT_FAILURE);
    }

    client_t clients[MAX_CLIENTS];
    int n_clients = 0;

    fd_set readfds;

    while (1)
    {
        FD_ZERO(&readfds);

        FD_SET(sockfd, &readfds);

        if (n_clients == 0)
        {
            printf("Waiting for clients on %s:%s\n",
                   inet_ntoa(server_addr.sin_addr), argv[1]);
        }
        else
        {
            for (int i = 0; i < n_clients; i++)
            {
                FD_SET(clients[i].sockfd, &readfds);
            }
        }

        if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL) < 0)
        {
            perror("select() failed");
            continue;
        }

        if (FD_ISSET(sockfd, &readfds))
        {
            struct sockaddr_in client_addr;
            memset(&client_addr, 0, sizeof(client_addr));
            socklen_t client_addr_len = sizeof(client_addr);
            int client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
            if (client_sockfd < 0)
            {
                perror("accept() failed");
                continue;
            }

            if (n_clients < MAX_CLIENTS)
            {
                clients[n_clients].sockfd = client_sockfd;
                clients[n_clients].addr = client_addr;
                strcpy(clients[n_clients].username, "");
                strcpy(clients[n_clients].password, "");
                n_clients++;
                printf("Client connected from %s:%d\n",
                       inet_ntoa(client_addr.sin_addr),
                       ntohs(client_addr.sin_port));

                //nhập username password
                char *msg = "Please enter your \"username password\": ";
                if (send(client_sockfd, msg, strlen(msg), 0) < 0)
                {
                    perror("send() failed");
                    continue;
                }
            }
            else
            {
                char *msg = "Too many clients\n";
                send(client_sockfd, msg, strlen(msg), 0);
                close(client_sockfd);
            }
        }

        for (int i = 0; i < n_clients; i++)
        {
            if (FD_ISSET(clients[i].sockfd, &readfds))
            {
                char msg[MAX_MSG_LEN];
                memset(msg, 0, MAX_MSG_LEN);
                int msg_len = recv(clients[i].sockfd, msg, MAX_MSG_LEN, 0);
                if (msg_len < 0)
                {
                    perror("recv() failed");
                    continue;
                }
                else if (msg_len == 0)
                {
                    printf("Client from %s:%d disconnected\n",
                           inet_ntoa(clients[i].addr.sin_addr),
                           ntohs(clients[i].addr.sin_port));
                    close(clients[i].sockfd);

                    clients[i] = clients[n_clients - 1];
                    n_clients--;

                    FD_CLR(clients[i].sockfd, &readfds);
                    continue;
                }
                else
                {
                    //check tồn tại client
                    if (strcmp(clients[i].username, "") == 0 && strcmp(clients[i].password, "") == 0)
                    {
                        char username[MAX_MSG_LEN];
                        char password[MAX_MSG_LEN];
                        char temp[MAX_MSG_LEN];
                        int ret = sscanf(msg, "%s %s %s", username, password, temp);
                        if (ret == 2)
                        {
                            int status = authenticate_user(username, password);
                            if (status == 1)
                            {
                                strcpy(clients[i].username, username);
                                strcpy(clients[i].password, password);

                                char *msg = "Login successful!\nEnter command: ";
                                if (send(clients[i].sockfd, msg, strlen(msg), 0) < 0)
                                {
                                    perror("send() failed");
                                    continue;
                                }
                            }
                            else if (status == 0)
                            {
                                char *msg = "Login failed! \nEnter  your \"username password\" again: ";
                                if (send(clients[i].sockfd, msg, strlen(msg), 0) < 0)
                                {
                                    perror("send() failed");
                                    continue;
                                }
                            }
                            else
                            {
                                char *msg = "Server error\nDatabase file not found!\nEnter  your \"username password\" again: ";
                                send(clients[i].sockfd, msg, strlen(msg), 0);
                            }
                        }
                        else
                        {
                            char *msg = "Invalid format!\nEnter your \"username password\" again : ";
                            send(clients[i].sockfd, msg, strlen(msg), 0);
                        }
                    }
                    else
                    {
                        if (strcmp(msg, "quit") == 0 || strcmp(msg, "exit") == 0)
                        {
                            char *msg = "See you again!\n";
                            if (send(clients[i].sockfd, msg, strlen(msg), 0) < 0)
                            {
                                perror("send() failed");
                                continue;
                            }

                            close(clients[i].sockfd);

                            clients[i] = clients[n_clients - 1];
                            n_clients--;

                            FD_CLR(clients[i].sockfd, &readfds);
                        }
                        else
                        {
                            handle_command(clients[i].sockfd, msg);
                        }
                    }
                }
            }
        }
    }

    close(sockfd);

    return 0;
}