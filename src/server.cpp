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
    int server_fd = soket(AF_INET,SOCK_STREAM,0) ;
    if(server_fd == -1)
    {
        std::cout<<"socket failed"<<std::endl;
        return -1;
    }

    int opt = 1 ;
    if(setsocketopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt) < 0))
    {
        std::cerr<<"Warning: 端口复用设置失败" <<std::endl;
    }

    struct sockaddr_in address;
    memset(&address,0,sizeof(address));
    address.sin_famliy = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if(bind(server_fd,(struct sockaddr*)&address,sizeof(address))<0)
    {
        std::serr<<"error:端口绑定失败"<<PORT<<std::endl;
        close(server_fd);
        return -1;
    }

    while(true)
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof( client_addr );

        std::cout<<"等待新的客户端连接中:........."<<std::endl;

        
    }
}