/*************************************************************************
	> File Name: httpd.c
	> Author: 
	> Mail: 
	> Created Time: 2016年09月06日 星期二 21时20分42秒
 ************************************************************************/

#include<stdio.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<ctype.h>
#include<strings.h>
#include<string.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include<pthread.h>
#include<stdlib.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN 0
#define STDOUT 1
#define STDERR 2

void accept_request(void*);
void bad_request(int);
void cat(int, FILE*);
void cannot_execute(int);
void err_sys(cosnt char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char*);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);




void accept_request(void *arg)
{
    int client = *(int*)arg;
    char buf[1024];
    size_t numchars;
    char method[1024];
    char url[1024];
    char path[512];
    size_t i, j;
    struct stat st;   //文件属性结构
    int cgi = 0;   //become true if server decides this is a CGI program
    char *query_string = NULL;

    numchars = get_line(client, buf, sizeof(buf));
    i = 0; j = 0;
    while(!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j = i;
    method[i] = '\0';

    if(strcasecmp(method, "GET") && strcasecmp(method, "POST"))  // 忽略大小写比较，若相同，返回0
    {
        unimplemented(client);
        return ;
    }

    if(strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0;
    while(ISspace(buf[j]) && j < numchars)
        j++;
    while(!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    if(strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if(*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }
    
    sprintf(path, "htdocs%s", url);
    if(path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    if(stat(path, &st) == -1)
    {
        while((numchars > 0) && strcmp("\n", buf))
            numchars = get_line(client, buf, sizeof(buf));

        not_found(client);
    }
    else{
        if((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
        if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) 
           || (st.st_mode & S_IXOTH))
            cgi = 1;

        if(!cgi)
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }

}

void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);

    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);

    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);

    sprintf(buf, "<P>Your browser sent a bad request");
    send(client, buf, sizeof(buf), 0);

    sprintf(buf, "Such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

void cat(int client, FILE *resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while(!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

void err_sys(const char *name)
{
    perror(name);
    exit(1);
}

void execute_cgi(int client, const char *path,
                const char* method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; buf[1] = '\0';
    if(strcasecmp(method, "GET") == 0)
        while((numchars > 0) && strcmp("\n", buf))
            numchars = get_line(client, buf, sizeof(buf));
    else if(strcasecmp(method, "POST") == 0)
    {
        numchars = get_line(client, buf, sizeof(buf));
        while((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if(strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&buf[16]);
            numchars = get_line(client, buf, sizeof(buf));
        }
        if(content_length == -1)
        {
            bad_request(client);
            return;
        }
    }
}
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);

        if(n > 0)
        {
            if(c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);  //MSG_PEEK 返回报文内容而不取走真正的报文

                if((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else 
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';
    return i;
}

void headers(int client, const char* filename)
{
    char buf[1024];
    (void)filename;

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "<BODY><P>The serverr could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A'; buf[1] = '\0';
    while(numchars > 0 && strcmp("\n", buf))
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if(resource == NULL)
        not_found(client);
    else{
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

int startup(u_short *port)
{
    int htttpd = 0;
    struct sockaddr_in name;

    httpd = socket(PF_INET, SOCK_STREAM, 0);  //PF_INET同AF_INET, IPv4网域。　
                                              //SOCK_STREAM 有序，可靠，双向的面向连接字节流
    if(httpd == -1)
         err_sys("socket error");

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);  //默认网卡
    if(bind(httpd, (struct sockaddr *)&name, &namelen) == -1)
        err_sys("bind error");

    if(*port == 0)  //动态分配一个端口
    {
        socklen_t namelen = sizeof(name);
        if(getsockname(httpd, (struct sockaddr*)&name, &namelen) == -1)
            err_sys("getsockname error");
        *port = ntohs(name.sin_port);
    }
    if(listen(httpd, 5) < 0)
        err_sys("listen error");

    return httpd;
}

void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

int main()
{
    int server_sock = -1;   //服务器套接字
    u_short port = 4000;   //端口号
    int client_sock = -1;  //客户端端口号
    struct sockaddr_in client_name;  //IPv4 网域中，套接字地址结构
    socklen_t client_name_len = sizeof(cilent_name);
    //pthread_t newthread;

    server_sock = startup(&port);
    printf("httpd running on port: %d\n", port);

    while(1)
    {
        /*返回的是调用connect的客户端的套接字描述符*/
        client_sock = accept(server_sock,
                                (struct sockaddr *)&client_name,
                                &client_name_len);
        if(client_sock == -1)
            err_sys("accept error");
        
        accept_request(client_sock);
        
    }
}

