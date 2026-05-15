#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <iostream>
#include <cstdlib> 
#include <cmath>
#include <cstring>
#include <vector>
#include <memory>
#include <shared_mutex>
#include <mutex>
#include <fstream>
#include <sstream>      // 用于字符串流转换
#include <type_traits>  // 用于类型判断
#include <fcntl.h>        // 用于文件锁
#include <unistd.h>       // 用于文件锁

template<typename K, typename V>
class Node{
    public:
    K key;
    V val;
    int node_level;

   //std::vector<std::shared_ptr<Node<K,V>>> forward;
   Node<K,V>** forward;

    Node(const K& k,const V& v,int level)
    :key(k),val(v),node_level(level)
    {
        forward = new Node<K,V>*[level+1]();
    }

    Node(K&& k,V&& v,int level)
    :key(std::move(k)),val(std::move(v)),node_level(level)
    {
        forward = new Node<K,V>*[level+1]();
    }

    ~Node()
    {
        delete[] forward;
    }
};

template<typename K,typename V>
using NodePtr = Node<K,V>*;

using ReadLock = std::shared_lock<std::shared_mutex>;
using WriteLock = std::unique_lock<std::shared_mutex>;


template<typename K,typename V>
class skiplist{
    private:
    int max_level;
    int curr_level;

    //std::shared_ptr<Node<K,V>> header;
    Node<K,V>* header;

    int element_count;
    mutable std::shared_mutex _mtx;
    std::string filename = "list_data.rbd";
    
    public:
    skiplist(int max_level);
    ~skiplist();

    int get_size() const{
        return element_count;
    }
    int get_random_level();
    void search(const K& key) const;
    void show() const ;
    void delete_node(const K& key);
    void insert(const K& key,const V& val);
    void insert(K&& key,V&& val);
    void dump_file() const;
    void load_file();
};

//构造函数
template<typename K,typename V>
skiplist<K,V>::skiplist(int max_level)
:max_level(max_level),curr_level(0),element_count(0)
{
    K k{};V v{};
    header = new Node<K,V>(k,v,max_level);
    load_file();
}

//析构函数
template<typename K,typename V>
skiplist<K,V>::~skiplist()
{
    NodePtr<K,V> current = header;
    while(current != nullptr)
    {
        header = header->forward[0];
        delete current;
        current = header;
    }
}

//随机生成一个层数
template<typename K,typename V>
int skiplist<K,V>::get_random_level()
{
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double>dist(0.0,1.0);
    int level = 1;
    while(dist(rng) < 0.25 && level < max_level)
    {
        ++level;
    }
    return level;
}

//查询节点k
template<typename K,typename V>
void skiplist<K,V>::search(const K& key) const
{
    ReadLock lock(_mtx);
    auto current = header;
    for(int i = curr_level; i >= 0; --i)
    {
       while(current->forward[i] !=nullptr && current->forward[i]->key < key)
       {
            current=current->forward[i];
       }
    }
    current=current->forward[0];
    // if(current != nullptr && current->key == key)
    // {
    //     std::cout<<"I got it!!!";
    //     std::cout<<"key: "<<current->key<<std::endl;
    //     std::cout<<"val: "<<current->val<<std::endl;
    // }
    // else
    // {
    //     std::cout<<"I don't have it!!!";
    // }
}

//展示内部结构
template<typename K,typename V>
void skiplist<K,V>::show() const
{
    for(int i=0;i<=curr_level;++i)
    {
        auto current = header->forward[i];
        std::cout<<"Level "<<i<<": ";
        while(current != nullptr)
        {
            std::cout<<"("<<current->key<<","<<current->val<<")"<<std::endl;
            current = current->forward[i];
        }
        std::cout<<std::endl;
    }
}

//删除节点k
template<typename K,typename V>
void skiplist<K,V>::delete_node(const K& key)
{
    WriteLock lock(_mtx);
    NodePtr<K,V> update [max_level+1];
    memset(update,0,sizeof(NodePtr<K,V>)*(max_level+1));
    auto current = header;
    for( int i = curr_level; i >= 0 ; --i)
    {
        while(current -> forward[i] != nullptr && current-> forward[i] -> key < key)
        {
            current = current ->forward[i];
        }
        update[i]=current;
    }
    current = current->forward[0];
    if(current != nullptr && current->key == key)
    {
        std::cout<<"before delete: "<<std::endl;
        show();
        std::cout<<std::endl;
        for(int i = 0 ; i <= curr_level ; ++i)
        {
            if(update[i]->forward[i] != current)
            break;
            update[i]->forward[i] = current -> forward[i];
        }
        delete current;
        while(curr_level > 0 && header->forward[curr_level] == nullptr)
        {
            --curr_level;
        }
        --element_count;
        std::cout<<"after delete: "<<std::endl;
        show();
        std::cout<<std::endl;
    }
}


//插入节点
template<typename K,typename V>
void skiplist<K,V>::insert(const K& key,const V& val)
{
    WriteLock lock(_mtx);
    NodePtr<K,V> update [max_level+1];
    auto current = header;
    memset(update,0,sizeof(NodePtr<K,V>)*(max_level+1));
    for( int i = curr_level; i >= 0 ; --i)
    {
        while(current -> forward[i] != nullptr && current-> forward[i] -> key < key)
        {
            current = current ->forward[i];
        }
        update[i]=current;
    }
    current = current->forward[0];

    if(current != nullptr && current->key == key)
    {
        current->val = val;
    }
    else
    {
        int new_level = get_random_level();
        if(new_level > curr_level)
        {
            for(int i = curr_level + 1; i <= new_level; ++i)
            {
                update[i] = header;
            }
            curr_level = new_level;
        }
        auto new_node = new Node<K,V>(key,val,new_level);
        for(int i = 0 ; i <= new_level ; ++i)
        {
            new_node->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = new_node;
        }
        ++element_count;
    }
}

template<typename K,typename V>
void skiplist<K,V>::insert(K&& key,V&& val)
{
    WriteLock lock(_mtx);
    NodePtr<K,V> update [max_level+1];
    auto current = header;
    memset(update,0,sizeof(NodePtr<K,V>)*(max_level+1));
    for( int i = curr_level; i >= 0 ; --i)
    {
        while(current -> forward[i] != nullptr && current-> forward[i] -> key < key)
        {
            current = current ->forward[i];
        }
        update[i]=current;
    }
    current = current->forward[0];

    if(current != nullptr && current->key == key)
    {
        current->val = std::move(val);
    }
    else
    {
        int new_level = get_random_level();
        if(new_level > curr_level)
        {
            for(int i = curr_level + 1; i <= new_level; ++i)
            {
                update[i] = header;
            }
            curr_level = new_level;
        }
        auto new_node = new Node<K,V>(std::move(key),std::move(val),new_level);
        for(int i = 0 ; i <= new_level ; ++i)
        {
            new_node->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = new_node;
        }
        ++element_count;
    }
}

//将跳表数据写入文件
template<typename K,typename V>
void skiplist<K,V>::dump_file() const
{
    ReadLock lock(_mtx);

    int fd = open("list_data.rbd", O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if(fd == -1)
    {
        std::cout<<"[Error] 无法打开文件 :"<<"list_data.rbd"<<std::endl;
        return ;
    }

    auto current = header->forward[0];
    std::string per_data;
    while(current != nullptr)
    {
        std::stringstream ss;
        ss << current->key<<":"<<current->val<<'\n';
        per_data = ss.str();
        write(fd,per_data.c_str(),per_data.size());
        current = current -> forward[0];
    }

    close(fd);
    std::cout<<"文件写入成功!"<<"共保存:"<<element_count<<"个元素"<<std::endl;
    return ;
    // std::ofstream out_file(filename);
    // if(!out_file.is_open())
    // {
    //     std::cout<<"[Error] 无法打开文件 :"<<filename<<std::endl;
    //     return;
    // }

    // auto current = header->forward[0];

    // while(current != nullptr)
    // {
    //     out_file<<current->key<<":"<<current->val<<'\n';
    //     current = current -> forward[0];
    // }

    // out_file.close();
    // std::cout<<"文件写入成功!"<<std::endl;
    // return ;
}


//读入文件历史数据
template<typename K,typename V>
void skiplist<K,V>::load_file()
{
    std::ifstream in_file(filename);
    if(!in_file.is_open())
    {
        std::cout<<"无法打开文件 : "<<filename<<'\n';
        return;
    }

    std::string line;
    while(getline(in_file,line))
    {
        if(line.empty())
        {
            std::cout<<"文件内容为空"<<std::endl;
            return;
        }

        int pos = line.find(":");
        std::string key_str = line.substr(0,pos);
        std::string val_str = line.substr(pos+1);
        K key;
        V val;

        if constexpr (std::is_same_v<K,std::string>)
        {
            key = key_str;
        }
        else{
            std::istringstream key_stream(key_str);
            key_stream >> key;
        }

        if constexpr (std::is_same_v<V,std::string>)
        {
            val = val_str;
        
        }
        else{
            std::istringstream val_stream(val_str);
            val_stream >> val;
        }

        insert(std::move(key),std::move(val));
    }
    in_file.close();
    std::cout<<"文件加载成功!"<<filename<<std::endl;
    return ;
}
#endif