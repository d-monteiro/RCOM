// Cliente FTP simples (RCOM Lab 2 - Parte 1).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define FTP_PORT   21
#define BUF_SIZE   4096
#define RESP_SIZE  2048

struct url {
    char user[256];
    char pass[256];
    char host[256];
    char path[512];
    char file[256];
};

static int parse_url(const char *raw, struct url *u);
static int resolve_host(const char *host, char *ip, size_t ipsize);
static int connect_tcp(const char *ip, int port);
static int send_cmd(int sock, const char *cmd);
static int read_response(int sock, char *out, size_t outsize);
static int parse_pasv(const char *resp, char *ip, int *port);
static int download_file(int data_sock, const char *filename);

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s ftp://[user:password@]host/path\n", argv[0]);
        return 1;
    }

    struct url u;
    if (parse_url(argv[1], &u) < 0) {
        fprintf(stderr, "URL invalida\n");
        return 1;
    }

    char ip[INET_ADDRSTRLEN];
    if (resolve_host(u.host, ip, sizeof(ip)) < 0) return 1;
    printf("Host %s -> %s\n", u.host, ip);

    int ctrl = connect_tcp(ip, FTP_PORT);
    char resp[RESP_SIZE];

    if (read_response(ctrl, resp, sizeof(resp)) != 220) {
        fprintf(stderr, "Servidor nao enviou 220\n%s", resp);
        return 1;
    }
    printf("%s", resp);

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "USER %s\r\n", u.user);
    send_cmd(ctrl, cmd);
    if (read_response(ctrl, resp, sizeof(resp)) != 331) {
        fprintf(stderr, "USER falhou\n%s", resp);
        return 1;
    }
    printf("%s", resp);

    snprintf(cmd, sizeof(cmd), "PASS %s\r\n", u.pass);
    send_cmd(ctrl, cmd);
    if (read_response(ctrl, resp, sizeof(resp)) != 230) {
        fprintf(stderr, "Login falhou\n%s", resp);
        return 1;
    }
    printf("%s", resp);

    if (strlen(u.path) > 0) {
        snprintf(cmd, sizeof(cmd), "CWD %s\r\n", u.path);
        send_cmd(ctrl, cmd);
        if (read_response(ctrl, resp, sizeof(resp)) != 250) {
            fprintf(stderr, "CWD falhou\n%s", resp);
            return 1;
        }
        printf("%s", resp);
    }

    send_cmd(ctrl, "TYPE I\r\n");
    if (read_response(ctrl, resp, sizeof(resp)) != 200) {
        fprintf(stderr, "TYPE I falhou\n%s", resp);
        return 1;
    }
    printf("%s", resp);

    send_cmd(ctrl, "PASV\r\n");
    if (read_response(ctrl, resp, sizeof(resp)) != 227) {
        fprintf(stderr, "PASV falhou\n%s", resp);
        return 1;
    }
    printf("%s", resp);

    char data_ip[INET_ADDRSTRLEN];
    int data_port;
    if (parse_pasv(resp, data_ip, &data_port) < 0) {
        fprintf(stderr, "Falha a parse da resposta PASV\n");
        return 1;
    }
    printf("Conexao de dados -> %s:%d\n", data_ip, data_port);

    int data = connect_tcp(data_ip, data_port);

    snprintf(cmd, sizeof(cmd), "RETR %s\r\n", u.file);
    send_cmd(ctrl, cmd);
    if (read_response(ctrl, resp, sizeof(resp)) != 150) {
        fprintf(stderr, "RETR falhou\n%s", resp);
        return 1;
    }
    printf("%s", resp);

    if (download_file(data, u.file) < 0) {
        fprintf(stderr, "Erro a gravar ficheiro\n");
        return 1;
    }
    close(data);

    if (read_response(ctrl, resp, sizeof(resp)) != 226) {
        fprintf(stderr, "Servidor nao enviou 226\n%s", resp);
        return 1;
    }
    printf("%s", resp);

    send_cmd(ctrl, "QUIT\r\n");
    read_response(ctrl, resp, sizeof(resp));
    printf("%s", resp);
    close(ctrl);

    printf("Download completo: %s\n", u.file);
    return 0;
}

static int parse_url(const char *raw, struct url *u) {
    if (strncmp(raw, "ftp://", 6) != 0) return -1;
    const char *p = raw + 6;

    // procurar '@' antes do primeiro '/'
    const char *slash = strchr(p, '/');
    const char *at = NULL;
    const char *limit = slash ? slash : p + strlen(p);
    for (const char *q = p; q < limit; q++) {
        if (*q == '@') { at = q; break; }
    }

    if (at != NULL) {
        const char *colon = NULL;
        for (const char *q = p; q < at; q++) {
            if (*q == ':') { colon = q; break; }
        }
        if (colon == NULL) return -1;
        size_t ulen = (size_t)(colon - p);
        size_t plen = (size_t)(at - colon - 1);
        if (ulen == 0 || ulen >= sizeof(u->user)) return -1;
        if (plen >= sizeof(u->pass)) return -1;
        memcpy(u->user, p, ulen);
        u->user[ulen] = '\0';
        memcpy(u->pass, colon + 1, plen);
        u->pass[plen] = '\0';
        p = at + 1;
    } else {
        strcpy(u->user, "anonymous");
        strcpy(u->pass, "anonymous@");
    }

    // host vai ate '/'
    slash = strchr(p, '/');
    if (slash == NULL) return -1;
    size_t hlen = (size_t)(slash - p);
    if (hlen == 0 || hlen >= sizeof(u->host)) return -1;
    memcpy(u->host, p, hlen);
    u->host[hlen] = '\0';

    // separar diretorio e nome do ficheiro
    const char *rest = slash + 1;
    const char *last = strrchr(rest, '/');
    if (last == NULL) {
        u->path[0] = '\0';
        if (strlen(rest) >= sizeof(u->file)) return -1;
        strcpy(u->file, rest);
    } else {
        size_t dlen = (size_t)(last - rest);
        if (dlen >= sizeof(u->path)) return -1;
        memcpy(u->path, rest, dlen);
        u->path[dlen] = '\0';
        if (strlen(last + 1) >= sizeof(u->file)) return -1;
        strcpy(u->file, last + 1);
    }
    if (u->file[0] == '\0') return -1;
    return 0;
}

static int resolve_host(const char *host, char *ip, size_t ipsize) {
    struct hostent *h = gethostbyname(host);
    if (h == NULL) {
        herror("gethostbyname");
        return -1;
    }
    if (inet_ntop(AF_INET, h->h_addr_list[0], ip, (socklen_t)ipsize) == NULL) {
        perror("inet_ntop");
        return -1;
    }
    return 0;
}

static int connect_tcp(const char *ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(EXIT_FAILURE); }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "IP invalido: %s\n", ip);
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    return sock;
}

static int send_cmd(int sock, const char *cmd) {
    return write(sock, cmd, strlen(cmd));
}

// Le uma resposta FTP (single ou multi-linha) e devolve o codigo numerico.
static int read_response(int sock, char *out, size_t outsize) {
    char line[1024];
    int code = -1;
    char code_str[4] = {0};
    size_t total = 0;

    while (1) {
        size_t i = 0;
        while (i < sizeof(line) - 1) {
            char c;
            ssize_t n = read(sock, &c, 1);
            if (n <= 0) return -1;
            line[i++] = c;
            if (c == '\n') break;
        }
        line[i] = '\0';

        if (total + i < outsize) {
            memcpy(out + total, line, i);
            total += i;
            out[total] = '\0';
        }

        if (code == -1 && i >= 4) {
            code_str[0] = line[0];
            code_str[1] = line[1];
            code_str[2] = line[2];
            code = atoi(code_str);
            if (line[3] == ' ') break;
        } else {
            if (i >= 4 &&
                line[0] == code_str[0] &&
                line[1] == code_str[1] &&
                line[2] == code_str[2] &&
                line[3] == ' ') {
                break;
            }
        }
    }
    return code;
}

static int parse_pasv(const char *resp, char *ip, int *port) {
    const char *paren = strchr(resp, '(');
    if (paren == NULL) return -1;
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(paren, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) != 6)
        return -1;
    snprintf(ip, INET_ADDRSTRLEN, "%d.%d.%d.%d", h1, h2, h3, h4);
    *port = p1 * 256 + p2;
    return 0;
}

static int download_file(int sock, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (f == NULL) { perror("fopen"); return -1; }
    char buf[BUF_SIZE];
    ssize_t n;
    while ((n = read(sock, buf, sizeof(buf))) > 0) {
        if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return (n < 0) ? -1 : 0;
}
