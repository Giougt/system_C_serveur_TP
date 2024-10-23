//network 
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>


#include <signal.h>
#include <sys/wait.h>
#include <netdb.h>

//lib and sockets 
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <regex.h>
#include <errno.h>

//manage file 
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>


#include <string.h>
#include <fcntl.h>


#define BACKLOG 10

#define BUFFER_SIZE 1024

char *config_file = NULL;
char *log_file = NULL;

int debug_mode = 0;
int port = 5000; 
FILE *log_fp = NULL;


char root_dir[512] = "";
char index_file[64] = "index.html";

void p_help()
{
    printf("Usage: binhttpd  [OPTIONS]\n");
    printf("Options :\n");
    printf("  -c <file>   Chemin vers le fichier de configuration\n");
    printf("  -d             Activer le mode de débogage\n");
    printf("  -h          Afficher ce message d'aide et quitter\n");
    printf("  -l <file>    Chemin vers le fichier de logs\n");
    printf("  -p <port>   Spécifier le port d'écoute\n");
}


void function_config(const char *file)
{
    FILE *fp = fopen(file, "r");
    if (!fp)
    {
        perror("Erreur lors de l'ouverture du fichier de configuration");
        exit(EXIT_FAILURE);
    }

    char line[256];
    
    char current_section[64] = "";

    while (fgets(line, sizeof(line), fp))
    {
        if (line[0] == '\n' || line[0] == '#' || line[0] == ';')
        {
            continue;
        }
        if (line[0] == '[')
        {
            sscanf(line, "[%[^]]", current_section);
            continue;
        }

        
        char key[64], value[256];
        if (sscanf(line, "%63[^=]= \"%255[^\"]\"", key, value) == 2 ||
            sscanf(line, "%63[^=]= '%255[^\']'", key, value) == 2 ||
            sscanf(line, "%63[^=]= %255s", key, value) == 2)
        {

            
            char *k = key ;

            while (*k == ' ' || *k == '\t')
                k++;
            memmove(key, k, strlen(k) + 1);
            k = key + strlen(key) - 1 ;
            while (k > key && (*k == ' ' || *k == '\t'))
                *k-- = '\0';


            if (strcmp(current_section, "binhttpd") == 0)
            {
                if (strcmp(key, "port") == 0)
                {
                    port = atoi(value);
                }
                else if (strcmp(key, "debug") == 0)
                {
                    if (strcmp(value, "on") == 0)
                    {
                        debug_mode = 1;
                    }
                    else
                    {
                        debug_mode = 0;
                    }
                }
                else if (strcmp(key, "logfile") == 0)
                {
                    log_file = strdup(value);
                }
            }


            else if (strcmp(current_section, "default") == 0)
            {
                if (strcmp(key, "root") == 0)
                {
                    strcpy(root_dir, value);
                }
                else if (strcmp(key, "index") == 0)
                {
                    strcpy(index_file, value);
                }
            }
        }
    }

    fclose(fp);


    if (strlen(root_dir) == 0)
    {
        fprintf(stderr, "Le répertoire racine 'root' n'est pas défini dans le fichier de configuration.\n") ;
        exit(EXIT_FAILURE);
    }
}


void s_handler(int s)
{
    (void)s; 
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}


void handle_request(int client_fd)
{
    char buffer[BUFFER_SIZE];
    int bytes_received;
    regex_t regex;
    regmatch_t matches[4];
    const char *pattern = "^(GET|HEAD|POST) (/[^ ]*) HTTP/([0-9]\\.[0-9])";
    int reti;

    
    bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received < 0)
    {
        perror("Erreur de réception") ;
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    buffer[bytes_received] = '\0';

    reti = regcomp(&regex, pattern, REG_EXTENDED);
    if (reti)
    {
        fprintf(stderr, "Impossible de compiler l'expression régulière\n");
        close(client_fd);
        exit(EXIT_FAILURE);
    }


    reti = regexec(&regex, buffer, 4, matches, 0);
    if (!reti)
    {
        
        char method[10];
        char path[512];
        char http_version[16];

        
        int method_len = matches[1].rm_eo - matches[1].rm_so;

        strncpy(method, buffer + matches[1].rm_so, method_len);
        method[method_len] = '\0';

        // calcul lenght of path 
        int path_len = matches[2].rm_eo - matches[2].rm_so;
        strncpy(path, buffer + matches[2].rm_so, path_len);
        path[path_len] = '\0';

        
        int version_len = matches[3].rm_eo - matches[3].rm_so;

        strncpy(http_version, buffer + matches[3].rm_so, version_len);
        http_version[version_len] = '\0';

        // Concatenate the root directory and the path
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s%s", root_dir, path);

        
        struct stat st;
        if (stat(full_path, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
            {
                
                if (full_path[strlen(full_path) - 1] != '/')
                {
                    strcat(full_path, "/") ;
                }
                strcat(full_path, index_file);
                if (stat(full_path, &st) != 0)
                {
                    
                    char response[BUFFER_SIZE];
                    snprintf(response, sizeof(response), "HTTP/%s 404 Not Found\r\n\r\n", http_version);
                    send(client_fd, response, strlen(response), 0);
                    close(client_fd);
                    exit(EXIT_SUCCESS);
                }
            }

            if (S_ISREG(st.st_mode))
            {
                
                int file_fd = open(full_path, O_RDONLY);
                if (file_fd < 0)
                {
                    
                    char response[BUFFER_SIZE];
                    snprintf(response, sizeof(response), "HTTP/%s 404 Not Found\r\n\r\n", http_version);
                    send(client_fd, response, strlen(response), 0);
                }
                else
                {
                    char file_buffer[BUFFER_SIZE];
                    int bytes_read;

                    char header[BUFFER_SIZE];
                    snprintf(header, sizeof(header),
                             "HTTP/%s 200 OK\r\n"
                             "Content-Length: %ld\r\n"
                             "Content-Type: text/html\r\n\r\n",
                             http_version,
                             st.st_size);
                    send(client_fd, header, strlen(header), 0);

                    while ((bytes_read = read(file_fd, file_buffer, BUFFER_SIZE)) > 0)
                    {
                        send(client_fd, file_buffer, bytes_read, 0);
                    }
                    close(file_fd);
                }
            }
            else
            {
                char response[BUFFER_SIZE];
                snprintf(response, sizeof(response), "HTTP/%s 404 Not Found\r\n\r\n", http_version);
                send(client_fd, response, strlen(response), 0);
            }
        }
        else
        {
            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response), "HTTP/%s 404 Not Found\r\n\r\n", http_version);
            send(client_fd, response, strlen(response), 0);
        }
    }
    else
    {
        char response[] = "HTTP/1.0 400 Bad Request\r\n\r\n";
        send(client_fd, response, strlen(response), 0);
    }

    regfree(&regex);
    close(client_fd);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    int opt;

    char default_config_file[256];
    snprintf(default_config_file, sizeof(default_config_file), "%s/opt/binhttpd/etc/binhttpd.conf", getenv("HOME"));

    while ((opt = getopt(argc, argv, "c:dhl:p:")) != -1)
    {
        switch (opt)
        {
        case 'c':
            config_file = optarg;
            break;
        case 'd':
            debug_mode = 1;
            break;
        case 'h':
            p_help();
            exit(EXIT_SUCCESS);
            break;
        case 'l':
            log_file = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            if (port <= 0 || port > 65535)
            {
                fprintf(stderr, "Invalid argument\n");
                exit(EXIT_FAILURE);
            }
            break;
        default:
            fprintf(stderr, "Invalid argument\n");
            exit(EXIT_FAILURE);
        }
    }

    if (!config_file)
    {
        config_file = default_config_file;
    }

    function_config(config_file);

    if (log_file)
    {
        log_fp = fopen(log_file, "a");
        if (!log_fp)
        {
            perror("Erreur lors de l'ouverture du fichier de logs");
            exit(EXIT_FAILURE);
        }
    }

    int sockfd, new_fd ;

    struct sockaddr_in server_addr, client_addr;
    socklen_t sin_size;

    struct sigaction sa;
    sa.sa_handler = s_handler;
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART ;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Erreur de création du socket");
        exit(EXIT_FAILURE);
    }

    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port) ;

    server_addr.sin_addr.s_addr = INADDR_ANY ;
    memset(&(server_addr.sin_zero), '\0', 8);

    if (debug_mode)
    {
        printf("[binhttpd] Trying to bind to 0.0.0.0:%d\n", port);
    }

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1)
    {
        perror("Erreur lors du bind");
        exit(EXIT_FAILURE);
    }

    if (debug_mode)
    {
        printf("[binhttpd] Socket successfully bound to 0.0.0.0:%d\n", port) ;
    }

    if (listen(sockfd, BACKLOG) == -1)
    {
        perror("Erreur lors du listen");
        exit(EXIT_FAILURE);
    }

    if (debug_mode)
    {
        printf("[binhttpd] Listening on socket %d\n", sockfd);
    }

    while (1)
    {
        sin_size = sizeof(struct sockaddr_in);
        if (debug_mode)
        {
            printf("[binhttpd] Ready to accept connection\n");
        }
        new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size);
        if (new_fd == -1)
        {
            perror("Erreur lors de l'accept");
            continue;
        }

        if (debug_mode)
        {
            printf("[binhttpd] Connection accepted from %s:%d\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        }

        pid_t pid = fork();
        if (pid == 0)
        {
            close(sockfd);
            handle_request(new_fd);
        }
        else if (pid > 0)
        {
            if (debug_mode)
            {
                printf("[binhttpd] Subprocess %d handling request\n", pid) ;
            }
            close(new_fd);
        }
        else
        {
            perror("Erreur lors du fork");
        }
    }

    close(sockfd);

    if (log_fp) // check open file log 
    {
        fclose(log_fp);
    }

    return 0;
}
