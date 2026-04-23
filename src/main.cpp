#include <iostream>
#include <string>
#include "skiplist.h" 

int main() {
    skiplist<int,std::string> sl(4);
    sl.insert(1,"one and one");
    sl.insert(2,"two and two");
    sl.insert(3,"three");
    sl.insert(5,"five");
    sl.dump_file();
    return 0;
}
