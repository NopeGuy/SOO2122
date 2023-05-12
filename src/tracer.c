#include <sys/stat.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include "tracer.h"

// funcoes auxiliares
#define MAX_STRING_LENGTH 20

void reverse(char s[])
{
    int i, j;
    char c;

    for (i = 0, j = strlen(s) - 1; i < j; i++, j--)
    {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

void itoa(int n, char s[])
{
    int i, sign;

    if ((sign = n) < 0) /* record sign */
        n = -n;         /* make n positive */
    i = 0;
    do
    { /* generate digits in reverse order */
        if (i >= MAX_STRING_LENGTH)
        {
            fprintf(stderr, "Error: string too long\n");
            fflush(stderr);
            exit(1);
        }
        s[i++] = n % 10 + '0'; /* get next digit */
    } while ((n /= 10) > 0);   /* delete it */
    if (sign < 0)
        s[i++] = '-';
    s[i] = '\0';
    reverse(s);
}

// funções primarias

// calcula o tempo de execucao do pedido (fazer isto com os valores diretamente ou atraves da struct?)
suseconds_t calcExec(pedido pedido, suseconds_t final)
{
    return (final - pedido.inicial) / 1000;
}

// copia apenas a parte do argc relevante para posteriormente acopular à struct pedido
char *extraiComandoString(int argc, char **argv)
{
    char *cmd = malloc(sizeof(char) * 1024); // Aloca um espaço inicial de 1024 bytes
    memset(cmd, 0, sizeof(char) * 1024);     // Preenche todo o espaço alocado com bytes nulos

    for (int i = 3; i < argc; i++)
    {
        strcat(cmd, argv[i]);
        strcat(cmd, " ");
    }
    return cmd;
}

void extraiComandoArray(char **str, int argc, char *argv[])
{
    int i = 0, j = 3;
    while (argv[j] && j <= argc)
    {
        str[i] = argv[j];
        i++;
        j++;
    }
    str[i] = '\0';
}

// Escreve para o stdout o pid que está a correr
void escrevePID(pid_t pid)
{
    char res[35] = "Running PID ";
    char sPID[20];
    itoa(pid, sPID);
    strcat(res, sPID);
    strcat(res, "\n");
    write(1, res, sizeof(res));
}

void execute(char *cmd, char **cmds)
{
    pedido pedido;
    pid_t pid;
    int pedido_pai[2], status = 0, bytes_written, main_fd;
    if (pipe(pedido_pai) == -1)
    {
        perror("pedido_pai");
        _exit(1);
    }

    if ((pid = fork()) == 0)
    {
        // fechar o apontadores de leitura
        close(pedido_pai[0]);

        escrevePID(getppid());

        // adiciona o tempo inicial e o pid à struct pedido
        struct timeval inicial;
        gettimeofday(&inicial, NULL);

        pedido.inicial = inicial.tv_usec;
        pedido.pid = getppid();
        write(pedido_pai[1], &pedido, sizeof(pedido));

        // fazer write do pedido semicompleto para o servidor

        main_fd = open("../fifos/main", O_WRONLY);
        if (main_fd < 0)
        {
            fprintf(stderr, "Erro ao abrir o main fifo!!!\n");
            fflush(stderr);
            _exit(1);
        }

        if ((bytes_written = write(main_fd, &pedido, sizeof(pedido))) == 0) // faco assim a verificacao?
        {
            fprintf(stderr, "erro escrita servidor!\n");
            fflush(stderr);
        }

        close(main_fd);

        execvp(cmds[0], cmds);

        fprintf(stderr, "Programa com o pid %d não concluído!\n", getppid());
        fflush(stderr);
        _exit(1);
    }
    else if (pid == -1)
        perror("fork");
    else
    {
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
        {
            close(pedido_pai[1]);
            int bytes_read = read(pedido_pai[0], &pedido, sizeof(pedido));
            if (bytes_read == 0)
            {
                fprintf(stderr, "erro a ler do pedido_pai");
                fflush(stderr);
                _exit(1);
            }
            close(pedido_pai[0]);

            // obter o tempo final de execução
            struct timeval final;
            gettimeofday(&final, NULL);

            // envia para o stdout o tempo de execução do pedido
            char resposta[25];
            suseconds_t tempExec = calcExec(pedido, final.tv_usec);
            snprintf(resposta, sizeof(resposta), "Ended in %ld ms\n", tempExec);
            write(STDOUT_FILENO, resposta, strlen(resposta));

            main_fd = open("../fifos/main", O_WRONLY);
            if (main_fd < 0)
            {
                fprintf(stderr, "Erro ao abrir o main fifo!!!\n");
                fflush(stderr);
                _exit(1);
            }

            // fazer write novamente do pedido para este ser eliminado
            if ((bytes_written = write(main_fd, &pedido, sizeof(pedido))) == 0) // faco assim a verificacao?
            {
                fprintf(stderr, "erro escrita servidor!\n");
                fflush(stderr);
            }
            close(main_fd);
            _exit(0);
        }
        else
        {
            fprintf(stderr, "Erro de espera pelo filho %d\n", pid);
            fflush(stderr);
            _exit(1);
        }
    }
}

int main(int argc, char *argv[])
{
    // caso do execute
    int main_fd, bytes_read, bytes_written;

    if (argc > 2 && strcmp("execute", argv[1]) == 0 && strcmp(argv[2], "-u") == 0)
    {
        main_fd = open("../fifos/main", O_RDWR);
        char *cmd;

        cmd = extraiComandoString(argc, argv); // retira o "execute -u" do argc inicial

        char **cmds = malloc(sizeof(char *) * (argc - 3));
        extraiComandoArray(cmds, argc, argv);

        execute(cmd, cmds);
    }

    // caso do status
    else if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
        // criação do pedido
        pedido pedido;
        pedido.commando = "";
        strcpy(pedido.commando, "status");
        pedido.pid = getpid();

        // envio do pedido para o servidor
        main_fd = open("../fifos/main", O_RDWR);
        if (main_fd < 0)
        {
            fprintf(stderr, "Erro ao abrir o main fifo!!!\n");
            fflush(stderr);
            return -1;
        }

        if ((bytes_written = write(main_fd, &pedido, sizeof(pedido))) == 0)
        {
            fprintf(stderr, "Erro ao escrever o pedido para o fifo!!!\n");
            fflush(stderr);
            _exit(1);
        }

        char buffer[100];

        while ((bytes_read = read(main_fd, &buffer, sizeof(buffer))) > 0)
        {
            write(STDOUT_FILENO, buffer, bytes_read);
        }
        close(main_fd);
    }
    // caso de comando inválido
    else
    {
        printf("\nComando inválido, por favor execute o cliente novamente com um dos seguintes comandos:\n 1) execute -u <argc>\n2) status\n");
        fflush(stdout);
    }

    return 0;
}