#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <iostream>
#include <cstdlib> 
#include <cmath>
#include <cstring>
#include <vector>
#include <memory>
#include <mutex>

template<typename K, typename V>
class Node{
    public:
    K key;
    V val;
    int node_level;

    std::vector<std::shared_ptr<Node<K,V>>> forward;

    Node(K k,V v,int level)
    :key(k),val(v),node_level(level)
    {
        this->forward.resize(level+1,nullptr);
    }

    ~Node()
    {
        ;
    }
};

template<typename K,typename V>
class skiplist{
    private:
    int max_level;
    int curr_level;
    std::shared_ptr<Node<K,V>> header;
    int element_count;
    std::mutex _mtx;
    
    public:
    skiplist(int max_level);
    ~skiplist();
    
    int get_random_level();
    void search(K key);
    void show();
    void delete_node(K key);
    void insert(K key,V val);
};

//构造函数
template<typename K,typename V>
skiplist<K,V>::skiplist(int max_level)
:max_level(max_level),curr_level(0),element_count(0)
{
    K k;V v;
    header=std::make_shared<Node<K,V>>(k,v,max_level);
}

//析构函数
template<typename K,typename V>
skiplist<K,V>::~skiplist()
{
    ;
}

//随机生成一个层数
template<typename K,typename V>
int skiplist<K,V>::get_random_level()
{
    int k = 1;
    while( rand() % 2 == 0 && k < max_level )
    ++k;
    return k;

}

//查询节点k
template<typename K,typename V>
void skiplist<K,V>::search(K key)
{
    auto current = header.get();
    for(int i = curr_level; i >= 0; --i)
    {
       while(current->forward[i] !=nullptr && current->forward[i]->key < key)
       {
            current=current->forward[i].get();
       }
    }
    current=current->forward[0].get();
    if(current != nullptr && current->key == key)
    {
        std::cout<<"I got it!!!";
        std::cout<<"key: "<<current->key<<std::endl;
        std::cout<<"val: "<<current->val<<std::endl;
    }
    else
    {
        std::cout<<"I don't have it!!!";
    }
}

//展示内部结构
template<typename K,typename V>
void skiplist<K,V>::show()
{
    for(int i=0;i<=curr_level;++i)
    {
        auto current = header->forward[i].get();
        std::cout<<"Level "<<i<<": ";
        while(current != nullptr)
        {
            std::cout<<"("<<current->key<<","<<current->val<<")"<<std::endl;
            current = current->forward[i].get();
        }
        std::cout<<std::endl;
    }
}

//删除节点k
template<typename K,typename V>
void skiplist<K,V>::delete_node(K key)
{
    std::vector<Node<K,V>*> update(max_level+1,nullptr);
    auto current = header.get();
    for( int i = curr_level; i >= 0 ; --i)
    {
        while(current -> forward[i] != nullptr && current-> forward[i] -> key < key)
        {
            current = current ->forward[i].get();
        }
        update[i]=current;
    }
    current = current->forward[0].get();
    if(current != nullptr && current->key == key)
    {
        std::cout<<"before delete: "<<std::endl;
        show();
        std::cout<<std::endl;
        for(int i = 0 ; i <= curr_level ; ++i)
        {
            if(update[i]->forward[i]->key != key)
            break;
            update[i]->forward[i] = current -> forward[i];
        }

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
void skiplist<K,V>::insert(K key,V val)
{
    std::vector<Node<K,V>*> update(max_level+1,nullptr);
    auto current = header.get();
    for( int i = curr_level; i >= 0 ; --i)
    {
        while(current -> forward[i] != nullptr && current-> forward[i] -> key < key)
        {
            current = current ->forward[i].get();
        }
        update[i]=current;
    }
    current = current->forward[0].get();

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
                update[i] = header.get();
            }
            curr_level = new_level;
        }
        auto new_node = std::make_shared<Node<K,V>>(key,val,new_level);
        for(int i = 0 ; i <= new_level ; ++i)
        {
            new_node->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = new_node;
        }
        ++element_count;
    }
}
#endif