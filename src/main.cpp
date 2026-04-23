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
    return 0;
}
