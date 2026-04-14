#ifndef HYLIAN_CORE_H
#define HYLIAN_CORE_H

#include <string>
#include <cstdlib>
#include <iostream>

// Error type
struct Error {
    std::string message;
    Error(const std::string& msg) : message(msg) {}
    
    std::string getMessage() const {
        return message;
    }
};

// Error constructors
inline Error* Err(const std::string& msg) {
    return new Error(msg);
}

// Panic function
inline void panic(const std::string& msg) {
    std::cerr << "PANIC: " << msg << std::endl;
    std::exit(1);
}

// Print function
inline void print(const std::string& msg) {
    std::cout << msg << std::endl;
}

// Reference counting helpers (for later)
template<typename T>
class Ref {
    T* ptr;
    int* refcount;
    
public:
    Ref(T* p) : ptr(p), refcount(new int(1)) {}
    
    Ref(const Ref& other) : ptr(other.ptr), refcount(other.refcount) {
        (*refcount)++;
    }
    
    ~Ref() {
        if (--(*refcount) == 0) {
            delete ptr;
            delete refcount;
        }
    }
    
    T* get() { return ptr; }
    T* operator->() { return ptr; }
};

#endif
