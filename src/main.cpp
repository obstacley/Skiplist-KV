#include <iostream>
#include <string>
#include <chrono>
#include <random>
#include <thread>
#include "skiplist.h" 

int main() {
    skiplist<int ,std::string> list_test(18);

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dist(1,1000000);

    const int per_thread = 100000 ;
    const int thread_count = 10 ;
    const int total_count = 100000;
    
    for(int i = 0 ; i < total_count  ; ++i)
    {
        list_test.insert(dist(rng),"test_value");
    }

    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();
    std::cout<<"------进行读写并发操作-------"<<std::endl;

    for(int i = 0 ; i < thread_count ; ++i)
    {
        threads.emplace_back([&list_test,i,per_thread](){
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_int_distribution<int> dist(1,1000000);
        if(i<8)
        {
            for(int j = 0 ; j < per_thread ; ++j)
            {
                int key = dist(rng);
                list_test.search(key);
            }
        }
        else{
            for(int j = 0; j < per_thread ; ++j)
            {
                list_test.insert(dist(rng),"test_value");
            }
        }    
        });
    }
    for(auto & t : threads)
    {
        if(t.joinable())
        {
            t.join();
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end-start);
    std::cout<<"------读写并发结束------"<<std::endl;
    std::cout<<"表内总数据量:"<<list_test.get_size()<<std::endl;
    std::cout<<"读写并发总耗时:"<<duration.count()<<"秒"<<std::endl;
    std::cout<<"多线程QPS:"<<(thread_count*per_thread)/duration.count()<<"次/秒"<<std::endl;

    list_test.dump_file();
    return 0;
}
