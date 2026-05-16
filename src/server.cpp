#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <thread>

constexpr int PORT = 9090;
constexpr int BUFFER_SIZE = 1024;

int main()
{
    //获取socket
    int serverfd = socket(AF_INET,SOCK_STREAM,0);
    if(serverfd == -1)
    {
        std::cerr<<"创建socket失败！"<<'\n';
        return -1;
    }
    //配置socket
    int opt = 1;
    if(setsockopt(serverfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))<0)
    {
        std::cerr<<"设置socket选项失败！"<<'\n';
        close(serverfd);
        return -1;
    }

    struct sockaddr_in address;
    memset(&address,0,sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if(bind(serverfd,(struct sockaddr*)&address,sizeof(address))<0)
    {
        std::cerr<<"绑定socket失败！"<<'\n';
        close(serverfd);
        return -1;
    }

    if(listen(serverfd,5)<0)
    {
        std::cerr<<"监听socket失败！"<<'\n';
        close(serverfd);
        return -1;
    }

    while(true)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        std::cout<<"等待客户端连接...\n";

        int clientfd = accept(serverfd,(struct sockaddr*)&client_addr,&client_len);
        if(clientfd == -1 )
        {
            std::cout<<"接受客户端连接失败！"<<'\n';
            continue;
        }

        std::cout<<"客户端连接成功！\n";
    }

}