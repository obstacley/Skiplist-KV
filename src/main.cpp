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

    const int per_thread = 10000 ;
    const int thread_count = 10 ;
    
    for(int i = 0 ; i < thread_count * per_thread ; ++i)
    {
        list_test.insert(dist(rng),"test_value");
    }

    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();
    std::cout<<"------进行查询操作-------"<<std::endl;

    for(int i = 0 ; i < thread_count ; ++i)
    {
        threads.emplace_back([&list_test,per_thread](){
            std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_int_distribution<int> dist(1,1000000);
            for(int j = 0 ; j < per_thread ; ++j)
            {
                int key = dist(rng);
                list_test.search(key);
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
    std::cout<<"------查询结束------"<<std::endl;
    std::cout<<"查询总耗时:"<<duration.count()<<"秒"<<std::endl;
    std::cout<<"多线程QPS:"<<(thread_count*per_thread)/duration.count()<<"次/秒"<<std::endl;
}
