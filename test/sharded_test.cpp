#include <iostream>
#include <cassert>
#include "sharded_skiplist.h"

int main() {
    // 1. 构造（16 个分片各自从文件加载）
    skv::shardedskiplist<int, std::string> list;

    // 2. insert
    assert(list.insert(1, "one"));
    assert(list.insert(2, "two"));
    assert(list.insert(100, "hundred"));

    // 3. insert 重复 key — 应更新值并返回 false
    assert(!list.insert(1, "ONE"));

    // 4. search
    auto v = list.search(1);
    assert(v.has_value() && v.value() == "ONE");

    v = list.search(2);
    assert(v.has_value() && v.value() == "two");

    v = list.search(999);
    assert(!v.has_value());

    // 5. delete
    assert(list.delete_node(100));
    assert(!list.search(100).has_value());
    assert(!list.delete_node(999));  // 不存在

    // 6. get_size（近似值，这里精确验证）
    int sz = list.get_size();
    assert(sz == 2);
    std::cout << "size = " << sz << " (expected 2)" << std::endl;

    // 7. show & dump
    list.show();
    list.dump_file();

    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}
