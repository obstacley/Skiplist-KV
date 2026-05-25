#ifndef SHARDED_SKIPLIST_H
#define SHARDED_SKIPLIST_H

#include "skiplist.h"
#include <functional>   
#include <vector>       
#include <string>       
#include <sstream>
#include <optional>
#include <utility>

namespace skv{

template<typename K,typename V>
class shardedskiplist{
    private:
    std::vector<std::unique_ptr<skiplist<K,V>>> shards;

    static constexpr size_t SHARD_COUNT = 16; // 分片数量，必须为2的幂次方以优化哈希计算
    size_t shard_index(const K& key) const{
        return std::hash<K>{}(key) & (SHARD_COUNT - 1);
    }

    static std::string shard_filename(size_t index){
        std::ostringstream oss;
        oss<<"list_data_"<<index<<".rbd";
        return oss.str();
    }

    public:
    shardedskiplist();
    
    std::optional<V> search(const K& key) const;
    bool insert(K key,V val);
    bool delete_node(const K& key);

    int get_size() const;
    void show() const;
    void dump_file() const ;
};

template<typename K,typename V>
shardedskiplist<K,V>::shardedskiplist()
{
    shards.reserve(SHARD_COUNT);
    for(size_t i = 0 ; i < SHARD_COUNT ; ++i)
    {
        shards.emplace_back(std::make_unique<skiplist<K,V>>(shard_filename(i)));
    }
}

template<typename K,typename V>
std::optional<V> shardedskiplist<K,V>::search(const K& key) const
{
    size_t index = shard_index(key);
    return shards[index]->search(key);
}

template<typename K,typename V>
bool shardedskiplist<K,V>::insert(K key,V val)
{
    size_t index = shard_index(key);
    return shards[index]->insert(std::move(key),std::move(val));
}

template<typename K,typename V>
bool shardedskiplist<K,V>::delete_node(const K& key)
{
    return shards[shard_index(key)]->delete_node(key);
}

template<typename K,typename V>
int shardedskiplist<K,V>::get_size() const
{
    int total_size = 0;
    for(const auto& shard : shards)
    {
        total_size += shard->get_size();
    }
    return total_size;
}

template<typename K,typename V>
void shardedskiplist<K,V>::show() const
{
    for(size_t i = 0 ; i < SHARD_COUNT ; ++i)
    {
        std::cout<<"-------Shard "<<i<<":--------\n";
        shards[i]->show();
    }
}

template<typename K,typename V>
void shardedskiplist<K,V>::dump_file() const
{
    for(size_t i = 0 ; i < SHARD_COUNT ; ++i)
    {
        shards[i]->dump_file();
    }
}
}
#endif