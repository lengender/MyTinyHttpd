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

void* accept_request(void*);
void bad_request(int);
void cat(int, FILE*);
void cannot_execute(int);
void err_sys(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char*);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);



/*
 * HTTP协议规定，请求从客户端发出，最后服务端响应请求并返回
 * 这是目前HTTP协议的规定，服务器不支持主动响应，所以目前的HTTP
 * 协议版本都是基于客户端请求，然后响应的这种模型
 * accept_request函数解析客户端请求，判断是静态文件还是cgi代码
 * (通过请求类型以及参数来判定),如果是静态文件则将文件输出给客户端，
 * 如果是cgi则进入cgi处理函数
 */
void* accept_request(void *arg)
{
    int client = *(int*)arg;
    char buf[1024];
    size_t numchars;
    char method[1024];  //请求方法GET or POST
    char url[1024];    //请求的文件路径
    char path[512];    //文件的相对路径
    size_t i, j;
    struct stat st;   //文件属性结构
    int cgi = 0;   //become true if server decides this is a CGI program
    char *query_string = NULL;

    numchars = get_line(client, buf, sizeof(buf)); //读取http报文的第一行，请求行
    i = 0; j = 0;

    //根据报文格式,首先得到请求方法
    while(!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j = i;
    method[i] = '\0';

    //忽略大小写比较字符串，用于判断是哪种方法
    if(strcasecmp(method, "GET") && strcasecmp(method, "POST"))  // 忽略大小写比较，若相同，返回0
    {
        unimplemented(client);  //两种都不是，告知客户段所请求的方法未能实现
        return NULL;
    }

    if(strcasecmp(method, "POST") == 0) //POST类型
        cgi = 1;

    i = 0;
    while(ISspace(buf[j]) && j < numchars) //忽略空格，空格后面是URL
        j++;

    //将buf中的url字段提取出来,遇到空格或满退出
    while(!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    //GET方法
    if(strcasecmp(method, "GET") == 0)
    {
        query_string = url; //请求信息
        while((*query_string != '?') && (*query_string != '\0')) //提取？前面的字符
            query_string++;    //问号前面是路径，后面是参数
        if(*query_string == '?')  // 有问号,表明是动态请求
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }
    
    //下面是项目文件中htdocs文件下的文件
    sprintf(path, "htdocs%s", url); //获取文件请求路径
    if(path[strlen(path) - 1] == '/') //如果文件类型是目录(/),则加上index.html
        strcat(path, "index.html");

    //根据路径找文件，并获取path文件信息保存到结构体st中    
    if(stat(path, &st) == -1) //文件未找到
    {
        while((numchars > 0) && strcmp("\n", buf)) //read & discard headers
            numchars = get_line(client, buf, sizeof(buf));

        not_found(client);   //回应客户端找不到
    }
    else{
        if((st.st_mode & S_IFMT) == S_IFDIR) //如果是个目录，则默认使用该目录下 index.html文件
            strcat(path, "/index.html");

        //判断是否是执行权限，即是否需要执行cgi程序
        if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) 
           || (st.st_mode & S_IXOTH))
            cgi = 1;

        if(!cgi)  //静态页面请求
            serve_file(client, path); //直接返回文件信息给客户端，静态页面返回
        else
            execute_cgi(client, path, method, query_string); //执行cgi脚本
    }
    return NULL;
}


/*
 * 请求出错情况处理,告知客户段该请求有错误　400
 */
void bad_request(int client)
{
    char buf[1024];

    //将字符串存入缓冲区，再通过send函数发送给客户端
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

/*
 * 读取文件中的数据到client,将文件结构指针resource中的数据发送到client
 */
void cat(int client, FILE *resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);  //从文件resource中读取数据，保存到buf中

    //处理文件流中剩下的字符
    while(!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/*
 * 通知客户端CGI脚本不能被执行 500
 */
void cannot_execute(int client)
{
    char buf[1024];

    //回馈出错信息
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}


/*
 * 打印出错信息
 */
void err_sys(const char *name)
{
    perror(name);
    exit(1);
}


/*
 * 执行CGI(公共网卡接口)脚本，需要设置合适的环境变量
 * execute_cgi函数负责将请求传递给cgi程序处理
 * 服务器与cgi之间通过管道pipe通信，实现初始化两个管道，并创建子进程去执行cgi函数
 * 子进程执行cgi程序，获取cgi的标准输出通过管道传给父进程，由父进程发送给客户端
 */
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
    if(strcasecmp(method, "GET") == 0) //GET方法，一般用于获取/查询资源信息
        while((numchars > 0) && strcmp("\n", buf))  //read　＆　discard headers 读取并丢弃http请求
            numchars = get_line(client, buf, sizeof(buf));
    else if(strcasecmp(method, "POST") == 0) //POST　一般用于更新资源信息
    {
        numchars = get_line(client, buf, sizeof(buf));

        //获取HTTP消息实体的传输长度
        while((numchars > 0) && strcmp("\n", buf))  //不为空且不为换行符
        {
            buf[15] = '\0';
            if(strcasecmp(buf, "Content-Length:") == 0) //是否是Content-Length字段
                content_length = atoi(&buf[16]);        //Content_Length用于描述Http消息实体的传输长度
            numchars = get_line(client, buf, sizeof(buf));
        }
        if(content_length == -1)
        {
            bad_request(client); //请求的页面数据为空，没有数据，就是我们打开网页经常出现的空白页面
            return;
        }
    }
    else  //Head or other
    {
    }

    //建立管道，两个通道　cgi_output[0]读端，cgi_output[1]写端
    if(pipe(cgi_output) < 0)
    {
        cannot_execute(client); //管道建立失败，打印出错信息
        return;
    }
    //管道只能具有公共祖先的进程之间进行，这里是父子进程
    //同理，建立管道
    if(pipe(cgi_input) < 0)
    {
        cannot_execute(client);
        return;
    }
 
    //fork 子进程，这就创建了父子进程间的IPC通道
    if((pid = fork()) < 0)
    {
        cannot_execute(client);
        return;
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    //实现初始化两个管道通信机制
    //子进程继承了父进程的pipe
    if(pid== 0)  // child: CGI script
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];
        
        //复制文件描述句柄，重定向进程的标准输入和输出
        dup2(cgi_output[1], STDOUT); //标准输出重定向到output管道的写入端
        dup2(cgi_input[0], STDIN);  //标注输入重定向到input的读取端
        close(cgi_output[1]); //关闭管道
        close(cgi_input[0]);
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env); //用来改变或者增加环境变量的内容
        
        if(strcasecmp(method, "GET") == 0)
        {
            //设置query_string的环境变量
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else
        { //POST
            //设置content_length的环境变量
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, path, NULL); //exec函数簇，执行cgi脚本，获取cgi的标准输出作为
                                //相应内容发送给客户端
                                //通过dup2重定向，标准输出内容进入管道output的输入端
        exit(0);
    }
    else  //parent
    {
        close(cgi_output[1]);
        close(cgi_input[0]);
        //通过关闭对应管道的通道，然后重定向子进程的管道某端，这样就在父子进程之间建立一条单双工通道
        //如果不重定向，将是一条典型的全双工管道通信进制
        
        int i;
        if(strcasecmp(method, "POST") == 0) //POST方式，将指定好的传送长度字符发送
        {
            for(i = 0; i < content_length; i++)
            {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
                //数据传输过程: input[1](父进程)　——> input[0](子进程)[执行cgi函数]　——> STDIN
                //——> STDOUT　——>output[1](子进程) ——>output[0](父进程)[将结果发送给客户端]
            }
        }

        while(read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);   //等待子进程终止
    }
}

/*
 *get_line 从客户端读取一行数据，存放在buf中，以\r或\r\n为行结束符
 */
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    //至多读取size-1个字符,最后一个字符置'\0'
    while((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);  //recv　从sock 中读取长度为１的数据存放到c中，０表示读取时的行为，一般为０

        if(n > 0)
        {
            if(c == '\r')  //如果是回车符
            {
                n = recv(sock, &c, 1, MSG_PEEK);  //MSG_PEEK 返回报文内容而不取走真正的报文

                if((n > 0) && (c == '\n'))  //如果是回车换行结束
                    recv(sock, &c, 1, 0); //将前面那个读过的字符,正式读取出来
                else 
                c = '\n';    //如果是以回车符结束的行，将其替换为\n,buf中统一用\n
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';
    return i;    //返回读取到的字符的个数
}


/*
 * 返回文件头部信息
 */
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

/*
 * 返回客户端404错误信息
 */
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


/*
 * 返回文件数据，用于静态页面返回
 */
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A'; buf[1] = '\0';
    while(numchars > 0 && strcmp("\n", buf))
        numchars = get_line(client, buf, sizeof(buf));  //read & discard(抛弃)头部

    resource = fopen(filename, "r");  //以只读方式打开文件
    if(resource == NULL)    //如果文件不存在，返回404错误
        not_found(client);
    else{
        headers(client, filename);  //先返回文件头部信息
        cat(client, resource); //将resource描述符指定的文件数据发送到client
    }
    fclose(resource);
}


/*
 * 服务器端套接字初始化
 */
int startup(u_short *port)
{
    int httpd = 0;
    struct sockaddr_in name;

    httpd = socket(PF_INET, SOCK_STREAM, 0);  //PF_INET同AF_INET, IPv4网域。　
                                              //SOCK_STREAM 有序，可靠，双向的面向连接字节流
    if(httpd == -1)
         err_sys("socket error");

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;  //地址簇
    name.sin_port = htons(*port); //指定端口
    name.sin_addr.s_addr = htonl(INADDR_ANY);  //通配地址
    if(bind(httpd, (struct sockaddr *)&name, sizeof(name)) == -1)  //绑定到指定地址和端口
        err_sys("bind error");

    if(*port == 0)  //动态分配一个端口
    {
        socklen_t namelen = sizeof(name);

        //在以端口号０调用bind后，getsockname用于返回内核赋予的本地端口号
        if(getsockname(httpd, (struct sockaddr*)&name, &namelen) == -1)
            err_sys("getsockname error");
        *port = ntohs(name.sin_port); //网络字节顺序转换为主机字节顺序，返回主机字节顺序表示的数
    }
    if(listen(httpd, 5) < 0)   //服务器监听客户段请求，套接字排队的最大连接数是５
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
    socklen_t client_name_len = sizeof(client_name);
    pthread_t newthread;

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
        
        //accept_request(client_sock);
        if(pthread_create(&newthread, NULL, accept_request, (void*)&client_sock) != 0)
            perror(pthread_create);

    }
    close(server_sock);

    return 0;
}

