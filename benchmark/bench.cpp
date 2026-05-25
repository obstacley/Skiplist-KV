#include <iostream>
#include <string>
#include <chrono>
#include <random>
#include <thread>
#include <vector>
#include <iomanip>
#include <cmath>
#include <cstdio>
#include <fstream>
#include "skiplist.h"
#include "sharded_skiplist.h"
using namespace skv;

const int PRE_FILL = 100000;
const int THREADS = 10;
const int OPS_PER_THREAD = 100000;

template<typename List>
void prefill(List& list) {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dist(1, 1000000);
    for (int i = 0; i < PRE_FILL; ++i) {
        list.insert(dist(rng), "bench_value");
    }
}

template<typename List>
double bench(List& list, int readers, int writers) {
    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < THREADS; ++i) {
        bool is_reader = (i < readers);
        threads.emplace_back([&list, is_reader]() {
            std::random_device rd;
            std::mt19937 rng(rd());
            std::uniform_int_distribution<int> dist(1, 1000000);
            for (int j = 0; j < OPS_PER_THREAD; ++j) {
                if (is_reader) {
                    list.search(dist(rng));
                } else {
                    list.insert(dist(rng), "bench_value");
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(end - start).count();
    return (THREADS * OPS_PER_THREAD) / sec;
}

struct Result {
    double pure_read, r8w2, r5w5, pure_write;
};

template<typename List>
Result run_all(const char* name) {
    // 清理旧文件
    std::system("rm -f list_data*.rbd 2>/dev/null");

    std::cout << "\n===== " << name << " =====\n";
    std::cout << std::fixed << std::setprecision(2);
    Result r;

    {
        List list;
        prefill(list);
        r.pure_read = bench(list, 10, 0);
        std::cout << "纯读 10 线程:  " << std::setw(8) << r.pure_read/1e6 << " M QPS\n";
    }

    {
        List list;
        prefill(list);
        r.r8w2 = bench(list, 8, 2);
        std::cout << "8 读 2 写:     " << std::setw(8) << r.r8w2/1e6 << " M QPS\n";
    }

    {
        List list;
        prefill(list);
        r.r5w5 = bench(list, 5, 5);
        std::cout << "5 读 5 写:     " << std::setw(8) << r.r5w5/1e6 << " M QPS\n";
    }

    {
        List list;
        prefill(list);
        r.pure_write = bench(list, 0, 10);
        std::cout << "纯写 10 线程:  " << std::setw(8) << r.pure_write/1e6 << " M QPS\n";
    }

    return r;
}

int main() {
    // 重定向 file I/O 日志以免干扰输出
    auto* cerr_buf = std::cerr.rdbuf();
    std::ofstream null("/dev/null");
    std::cerr.rdbuf(null.rdbuf());
    std::streambuf* cout_buf = std::cout.rdbuf();

    Result old_r = run_all<skiplist<int, std::string>>("全局锁 skiplist");
    Result new_r = run_all<shardedskiplist<int, std::string>>("分段锁 sharded_skiplist (16 shards)");

    // 恢复 stderr
    std::cerr.rdbuf(cerr_buf);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n==========================================================\n";
    std::cout << "                    对比总表\n";
    std::cout << "==========================================================\n";
    std::cout << std::left
              << std::setw(16) << "场景"
              << std::setw(16) << "全局锁(本次)"
              << std::setw(16) << "全局锁(历史)"
              << std::setw(16) << "分段锁(本次)"
              << "提升\n";
    std::cout << std::string(78, '-') << "\n";

    auto print_row = [&](const char* scene, double old_qps, double hist_qps, double new_qps) {
        double speedup = new_qps / old_qps;
        std::cout << std::left
                  << std::setw(16) << scene
                  << std::setw(16) << (std::to_string((int)(old_qps/1e6*100)/100.0) + " M")
                  << std::setw(16) << (std::to_string(hist_qps) + " M")
                  << std::setw(16) << (std::to_string((int)(new_qps/1e6*100)/100.0) + " M")
                  << std::setprecision(1) << speedup << "x\n";
        std::cout << std::setprecision(2);
    };

    print_row("纯读 10 线程", old_r.pure_read,   4.60, new_r.pure_read);
    print_row("8 读 2 写",    old_r.r8w2,       1.00, new_r.r8w2);
    print_row("5 读 5 写",    old_r.r5w5,       0.27, new_r.r5w5);
    print_row("纯写 10 线程", old_r.pure_write, 0.34, new_r.pure_write);

    // 清理
    std::system("rm -f list_data*.rbd 2>/dev/null");
    return 0;
}
