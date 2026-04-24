#include <iostream>
#include <string>
#include <chrono>
#include <random>
#include <thread>
#include "skiplist.h" 

int main() {
    skiplist<int , std::string> test_list(18);

    std::random_device rd;
    // std::mt19937 rng(rd());
    // std::uniform_int_distribution<int> dist(1, 1000000);
    // const int test_count = 100000;

    const int THREAD_COUNT = 10 ;
    const int TEST_PRE_THREAD = 10000;
    const int test_count = THREAD_COUNT * TEST_PRE_THREAD;

    std::vector<std::thread> threads;

    std::cout<<"进行多线程测试,线程数量为:"<<THREAD_COUNT<<"每个线程测试数据量为:"<<TEST_PRE_THREAD<<"总测试数据量为:"<<test_count<<std::endl;
    std::cout<<"----------开始压入内存----------"<<std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    for(int i = 0 ; i < THREAD_COUNT  ; ++i)
    {
        threads.emplace_back([&test_list,TEST_PRE_THREAD](){
            std::random_device rd;
            std::mt19937 rng(rd());
            std::uniform_int_distribution<int> dist(1,1000000);
            for(int j = 0 ; j < TEST_PRE_THREAD ; ++j)
            {
                int random_key = dist(rng);
                test_list.insert(random_key,"test_value");
            }
        });
    }

    for(auto &t : threads)
    {
        if(t.joinable())
        {
            t.join();
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end_time - start_time);

    std::cout<<"----------压入内存完成----------"<<std::endl;
    std::cout<<"此次测试的数据大小为:"<<test_count<<"条"<<std::endl;;
    std::cout<<"耗时"<<duration.count()<<"秒"<<std::endl;
    std::cout << "多线程QPS: " << (test_count / duration.count()) << " 次/秒" << std::endl;
    return 0;
}
