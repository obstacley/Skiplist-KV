#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <string>
#include <sstream>
#include "../include/skiplist.h"
#include <functional> //提供std::ref

constexpr int PORT = 9090;
constexpr int BUFFER_SIZE = 1024;

skiplist<std::string, std::string> kv;

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
            std::cerr<<"接受客户端连接失败！"<<'\n';
            continue;
        }

        std::cout<<"客户端连接成功！\n";
        std::string buffer;
        char temp[BUFFER_SIZE];
        while(true)
        {
            ssize_t bytes = read(clientfd,temp,BUFFER_SIZE - 1);
            if(bytes < 0)
            {
                std::cerr<<"读取客户端数据失败！"<<'\n';
                break;
            }
            else if(bytes == 0)
            {
                std::cout<<"客户端断开连接！\n";
                break;
            }
            buffer.append(temp, bytes);
            size_t pos;
            while((pos = buffer.find('\n')) != std::string::npos)
            {
                std::string line = buffer.substr(0,pos);
                buffer.erase(0,pos+1);
                // std::cout<<"收到客户端消息: "<<line<<'\n';
                if(!line.empty() && line.back() == '\r')
                    line.pop_back();
                std::istringstream iss(line);
                std::string cmd,key,val;
                iss>>cmd>>key>>val;
                std::string reponse;
                if(cmd == "SET")
                {
                    bool result =kv.insert(key,val);
                    if(result)
                    reponse = "OK(INSERT)\n";
                    else reponse = "OK(UPDATE)\n";
                }
                else if(cmd == "GET")
                {
                    auto result = kv.search(key);
                    if(result.has_value())
                        reponse = result.value() + "\n";
                    else reponse = "NOT_FOUND\n";
                }
                else if(cmd == "DEL") 
                {
                    bool result = kv.delete_node(key);
                    if(result)
                        reponse = "OK(DELETE)\n";
                    else reponse = "NOT_FOUND\n";
                }
                else if(cmd == "COUNT")
                {
                    response = std::to_string(kv.get_size()) + "\n";
                }
                else {
                    write(clientfd, "ERROR\n", 6);
                    continue;
                }
                write(clientfd, reponse.c_str(), reponse.size());
            }
        }
        close(clientfd);
    }

}