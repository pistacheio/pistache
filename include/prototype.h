/* 
   Mathieu Stefani, 28 janvier 2016
   
   Simple Prototype design pattern implement
*/

#pragma once

#include <type_traits>
#include <memory>

/* In a sense, a Prototype is just a class that provides a clone() method */
template<typename Class>
struct Prototype {
    virtual std::shared_ptr<Class> clone() const = 0;
};

template<typename Base, typename Class>
struct Prototypable {

};

#define PROTOTYPE_OF(Base, Class)                                        \
private:                                                                 \
    std::shared_ptr<Base> clone() const {                                \
        return std::make_shared<Class>();                                \
    }                                                                    \
public:

#define HTTP_PROTOTYPE(Class) \
    PROTOTYPE_OF(Net::Tcp::Handler, Class)
