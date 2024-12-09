
// Inclusion des bibliothèques standard
#include <stdio.h>      // Pour les fonctions d'entrée/sortie (printf, scanf, etc.)
#include <stdlib.h>     // Pour les fonctions générales (malloc, free, exit, etc.)

#include <string.h>     // Pour les fonctions de manipulation de chaînes (strcpy, strlen, etc.)
#include <unistd.h>     // Pour les appels système (fork, pipe, close, etc.)
#include <netinet/in.h> // Pour les structures et constantes liées au réseau (sockaddr_in, htonl, etc.)
#include <arpa/inet.h>  // Pour les fonctions liées aux adresses IP (inet_aton, inet_ntoa, etc.)
#include <getopt.h>     // Pour le traitement des options en ligne de commande

#include <fcntl.h>      // Pour les opérations sur descripteurs de fichiers (open, O_RDWR, etc.)
#include <sys/stat.h>   // Pour les informations sur les fichiers (stat, chmod, etc.)

#include <regex.h>      // Pour la manipulation des expressions régulières (regex_t, regexec, etc.)
#include <signal.h>     // Pour la gestion des signaux (signal, sigaction, etc.)
#include <time.h>       // Pour la manipulation de la date et de l'heure (time, strftime, etc.)
#include <sys/types.h>  // Définitions de types pour les appels système (pid_t, size_t, etc.)
#include <sys/wait.h>   // Pour gérer les processus fils (wait, waitpid, etc.)

#include <limits.h>     // Pour les constantes liées aux limites du système (PATH_MAX, INT_MAX, etc.)
#include <errno.h>      // Pour gérer les erreurs (errno, strerror, etc.)
// Définit _XOPEN_SOURCE 700 pour activer certaines fonctionnalités POSIX avancées
#define _XOPEN_SOURCE 700

// Vérifie si la constante PATH_MAX n'est pas déjà définie
#ifndef PATH_MAX
#define PATH_MAX 4096 
#endif

// Définit des constantes utilisées dans le programme
#define BUFFER_SIZE 1024 // Taille du tampon pour la lecture/écriture de données
#define DEFAULT_PORT 42064 // Port par défaut pour le serveur HTTP
#define DEFAULT_ROOT "/home/2024/a2-bin/al231388/opt/binhttpd/srv/http" 
// Répertoire racine par défaut pour le serveur (où se trouvent les fichiers à servir)

#define DEFAULT_INDEX "index.html" 

static int debug = 0;  

static int secure = 0; 

void print_usage(const char *progname)
{
    printf("Usage: %s [-p port] [-d] [-c config] [-s]\n", progname);
    printf("Options:\n");
    printf("  -p <port>   Specify port number (default: %d)\n", DEFAULT_PORT);
    printf("  -d          Enable debug mode\n");
    printf("  -c <file>   Specify configuration file path\n");
    printf("  -s          Enable secure (TLS) mode\n");
}

void send_error_response(int client_socket, int status_code, const char *status_text)
{
    char date_buffer[128];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now); 
    strftime(date_buffer, sizeof(date_buffer), "%a, %d %b %Y %H:%M:%S", tm_info);

    char body[BUFFER_SIZE];
    snprintf(body, sizeof(body),
             "<html>\n"
             "<head><title>%d %s</title></head>\n"
             "<body>\n"
             "<center><h1>%d %s</h1></center>\n"
             "<hr><center>binhttpd</center>\n"
             "</body></html>\n",
             status_code, status_text,
             status_code, status_text);


    char response[BUFFER_SIZE * 2];
    snprintf(response, sizeof(response),
             "HTTP/1.0 %d %s\r\n"
             "Date: %s\r\n"
             "Server: binhttpd\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             status_code, status_text,
             date_buffer,
             (long)strlen(body),
             body);


    write(client_socket, response, strlen(response));
}

void serve_file(int client_socket, const char *request_path)
{
    char root[] = DEFAULT_ROOT;
    char path[PATH_MAX + 128];

    if (strcmp(request_path, "/") == 0)
    {
        snprintf(path, sizeof(path), "%s/%s", root, DEFAULT_INDEX);
    }
    else
    {
        snprintf(path, sizeof(path), "%s%s", root, request_path);
    }

    char real_root[PATH_MAX];
    if (realpath(root, real_root) == NULL)
    {
        perror("realpath");
        send_error_response(client_socket, 500, "Internal Server Error");
        return;
    }

    char real_path[PATH_MAX];
    if (realpath(path, real_path) == NULL)
    {
        // File does not exist, send 404
        send_error_response(client_socket, 404, "Not Found");
        return;
    }

    if (strncmp(real_path, real_root, strlen(real_root)) != 0)
    {
        send_error_response(client_socket, 403, "Forbidden");
        return;
    }

    struct stat file_stat;
    if (stat(real_path, &file_stat) < 0)
    {
        send_error_response(client_socket, 404, "Not Found");
        return;
    }

if (S_ISDIR(file_stat.st_mode)) // Vérifie si le chemin correspond à un répertoire
{
    // Construit un chemin vers le fichier index par défaut (exemple : "index.html")
    snprintf(path, sizeof(path), "%s/%s", real_path, DEFAULT_INDEX);

    // Vérifie si le fichier index existe dans le répertoire
    if (stat(path, &file_stat) < 0)
    {
        // Si le fichier index est introuvable, envoie une erreur 404 au client
        send_error_response(client_socket, 404, "Not Found");
        return;
    }

    // Copie le chemin du fichier index dans la variable `real_path`
    strncpy(real_path, path, sizeof(real_path) - 1);

    // Ajoute une terminaison nulle à la fin pour éviter les dépassements de tampon
    real_path[sizeof(real_path) - 1] = '\0';

    // Vérifie à nouveau l'existence et les permissions du fichier index
    if (stat(real_path, &file_stat) < 0)
    {
        // Si le fichier n'existe pas ou pose un problème d'accès, renvoie une erreur 404
        send_error_response(client_socket, 404, "Not Found");
        return;
    }
}
    else if (!S_ISREG(file_stat.st_mode))
    {
        send_error_response(client_socket, 404, "Not Found");
        return;
    }

    int file_fd = open(real_path, O_RDONLY);
    if (file_fd < 0)
    {
        send_error_response(client_socket, 404, "Not Found");
        return;
    }

    if (fstat(file_fd, &file_stat) < 0)
    {
        close(file_fd);
        send_error_response(client_socket, 500, "Internal Server Error");
        return;
    }

    char date_buffer[128];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(date_buffer, sizeof(date_buffer), "%a, %d %b %Y %H:%M:%S", tm_info);

    char response_header[BUFFER_SIZE];
    snprintf(response_header, sizeof(response_header),
             "HTTP/1.0 200 OK\r\n"
             "Date: %s\r\n"
             "Server: binhttpd\r\n"
             "Content-Length: %ld\r\n"
             "Content-Type: text/html\r\n"
             "Connection: close\r\n"
             "\r\n",
             date_buffer, (long)file_stat.st_size);

    write(client_socket, response_header, strlen(response_header));

    char file_buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, file_buffer, BUFFER_SIZE)) > 0)
    {
        write(client_socket, file_buffer, bytes_read);
    }

    close(file_fd);
}

//utilisé pour éviter les processus zombies
void sigchld_handler(int signum)
{
    (void)signum;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}



void handle_client(int client_socket)
{
     // Si le mode debug est activé, affiche un message avec l'ID du processus qui gère la requête.
    if (debug)
    {
        fprintf(stderr, "[binhttpd] Subrocess %d handling request\n", (int)getpid());
    }

    char buffer[BUFFER_SIZE];
    ssize_t total_read = 0;
    int found_newline = 0;

    // Read until newline "\n" or client closes
    while (total_read < BUFFER_SIZE - 1)
    {
        ssize_t n = read(client_socket, buffer + total_read, BUFFER_SIZE - 1 - total_read);
        if (n < 0)
        {
            perror("read");
            close(client_socket);
            exit(EXIT_FAILURE);
        }
        if (n == 0)
        {
            // Client closed without sending full request line
            close(client_socket);
            exit(EXIT_SUCCESS);
        }
        total_read += n;
        buffer[total_read] = '\0';
        if (strchr(buffer, '\n') != NULL)
        {
            found_newline = 1;
            break;
        }
    }

    if (!found_newline)
    {
        close(client_socket);
        exit(EXIT_SUCCESS);
    }

    // Allow CRLF or just LF: use \r?\n in regex
    regex_t regex;
    regmatch_t matches[3];
    const char *pattern = "^(GET|HEAD|POST) (/[^ ]*) HTTP/1\\.0\r?\n";

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0)
    {
        fprintf(stderr, "Failed to compile regex\n");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

   
   
   


   
   
   
    int reti = regexec(&regex, buffer, 3, matches, 0);
    if (reti == REG_NOMATCH)
    {
        send_error_response(client_socket, 400, "Bad Request");
    }
    else if (reti == 0)
    {
        char method[16];

        char path[256];

        int method_len = matches[1].rm_eo - matches[1].rm_so;
        int path_len = matches[2].rm_eo - matches[2].rm_so;

        strncpy(method, buffer + matches[1].rm_so, method_len);
        method[method_len] = '\0';

        strncpy(path, buffer + matches[2].rm_so, path_len);
        path[path_len] = '\0';

        if (strcmp(method, "GET") == 0)
        {
            serve_file(client_socket, path);
        }
        else
        {
            send_error_response(client_socket, 501, "Not Implemented");
        }
    }
    else
    {
        fprintf(stderr, "Regex match failed\n");
        close(client_socket);
        regfree(&regex);
        exit(EXIT_FAILURE);
    }

    regfree(&regex);
    close(client_socket);

    exit(EXIT_SUCCESS);
}




int main(int argc, char *argv[])
{
    int opt;
    int port = DEFAULT_PORT; // Port par défaut utilisé par le serveur.
    char *config_file = NULL;
// Boucle pour analyser les arguments de la ligne de commande avec getopt.

    while ((opt = getopt(argc, argv, "p:dc:s")) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            char *endptr;
            long val = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' || val < 1 || val > 65535)
            {
                fprintf(stderr, "Invalid argument\n");
                exit(EXIT_FAILURE);
            }
            port = (int)val;
            break;
        }
        case 'd':
            debug = 1;
            break;
        case 'c':
            config_file = optarg;
            break;
        case 's':
            secure = 1;
            break;
        default:
            print_usage(argv[0]);

            exit(EXIT_FAILURE);
        }
    }


    if (config_file && debug)
    {
        fprintf(stderr, "[binhttpd] Using configuration file: %s\n", config_file);
    }

    if (secure && debug)
    {
        fprintf(stderr, "[binhttpd] TLS connection enabled\n");
    }

    printf("Starting server on port %d\n", port);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt_reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_reuse, sizeof(opt_reuse)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address;

    socklen_t address_len = sizeof(address);
    memset(&address, 0, address_len);
    address.sin_family = AF_INET;

    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);


    if (debug)
    {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, ip_str, sizeof(ip_str));
        fprintf(stderr, "[binhttpd] Trying to bind to %s:%d\n", ip_str, port);
    }

    if (bind(server_fd, (struct sockaddr *)&address, address_len) < 0)
    {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (debug)
    {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, ip_str, sizeof(ip_str));
        fprintf(stderr, "[binhttpd] Socket successfully bound to %s:%d\n", ip_str, port);
    }

    if (listen(server_fd, 10) < 0)
    {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (debug)
    {
        fprintf(stderr, "[binhttpd] Listening on socket %d\n", server_fd);
    }

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");

        close(server_fd);
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        if (debug)
        {
            fprintf(stderr, "[binhttpd] Ready to accept connection\n");
        }

        int new_socket = accept(server_fd, NULL, NULL);
        if (new_socket < 0)
        {
            perror("accept");
            continue;
        }

        if (debug)
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            getpeername(new_socket, (struct sockaddr *)&client_addr, &client_len);
            char client_ip_str[INET_ADDRSTRLEN];

            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str));
            fprintf(stderr, "[binhttpd] Connection accepted from %s:%d\n",
                    client_ip_str, ntohs(client_addr.sin_port));
        }

        pid_t pid = fork();
        if (pid < 0)
        {

            perror("fork");
            close(new_socket);
            continue;
        }
        if (pid == 0)
        {
            close(server_fd);
            handle_client(new_socket);
            exit(EXIT_SUCCESS);
        }
        else
        {
            close(new_socket);
        }
    }

    close(server_fd);
    return 0;

}
