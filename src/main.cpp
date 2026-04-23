#include <iostream>
#include <string>
#include "skiplist.h" 

int main() {
    skiplist<int,std::string> sl(4);
    sl.insert(1,"one");
    sl.insert(2,"two");
    sl.insert(3,"three");
    sl.insert(4,"four");
    sl.show();
    sl.delete_node(2);
    sl.show();
    sl.dump_file();
    return 0;
}
