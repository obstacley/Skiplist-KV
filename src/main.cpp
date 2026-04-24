#include <iostream>
#include <string>
#include <chrono>
#include <random>
#include <thread>
#include "skiplist.h" 

int main() {
    skiplist<int , std::string> test_list(18);

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dist(1, 1000000);
    const int test_count = 100000;

    std::cout<<"----------开始压入内存----------"<<std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    for(int i = 0 ; i < test_count ; ++i)
    {
        int random_key = dist(rng);
        test_list.insert(random_key,"test_value");
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end_time - start_time);
    std::cout<<"----------压入内存完成----------"<<std::endl;
    std::cout<<"此次测试的数据大小为:"<<test_count<<"条"<<std::endl;;
    std::cout<<"耗时"<<duration.count()<<"秒"<<std::endl;
    std::cout << "插入 QPS: " << (test_count / duration.count()) << " 次/秒" << std::endl;
    return 0;
}
