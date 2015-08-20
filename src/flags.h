/* flags.h
   Mathieu Stefani, 18 August 2015
   
   Make it easy to have bitwise operators for scoped or unscoped enumerations
*/

#pragma once
#include <type_traits>
#include <iostream>

// Looks like gcc 4.6 does not implement std::underlying_type
namespace detail {
    template<size_t N> struct TypeStorage;

    template<> struct TypeStorage<sizeof(uint8_t)> {
        typedef uint8_t Type;
    };
    template<> struct TypeStorage<sizeof(uint16_t)> {
        typedef uint16_t Type;
    };
    template<> struct TypeStorage<sizeof(uint32_t)> {
        typedef uint32_t Type;
    };
    template<> struct TypeStorage<sizeof(uint64_t)> {
        typedef uint64_t Type;
    };

    template<typename T> struct UnderlyingType {
        typedef typename TypeStorage<sizeof(T)>::Type Type;
    };
}

template<typename T>
class Flags {
public:
    static_assert(std::is_enum<T>::value, "Flags only works with enumerations");
    typedef typename detail::UnderlyingType<T>::Type Type;

    Flags() { }

    Flags(T val) : val(val)
    {
    }

#define DEFINE_BITWISE_OP_CONST(Op) \
    Flags<T> operator Op (T rhs) const { \
        return Flags<T>( \
            static_cast<T>(static_cast<Type>(val) Op static_cast<Type>(rhs)) \
        ); \
    } \
    \
    Flags<T> operator Op (Flags<T> rhs) const { \
        return Flags<T>( \
            static_cast<T>(static_cast<Type>(val) Op static_cast<Type>(rhs.val)) \
        ); \
    }
    
    DEFINE_BITWISE_OP_CONST(|)
    DEFINE_BITWISE_OP_CONST(&)
    DEFINE_BITWISE_OP_CONST(^)

#undef DEFINE_BITWISE_OP_CONST

#define DEFINE_BITWISE_OP(Op) \
    Flags<T>& operator Op##=(T rhs) { \
        val = static_cast<T>( \
                  static_cast<Type>(val) Op static_cast<Type>(rhs) \
              ); \
        return *this; \
    } \
    \
    Flags<T>& operator Op##=(Flags<T> rhs) { \
        val = static_cast<T>( \
                  static_cast<Type>(val) Op static_cast<Type>(rhs.val) \
              ); \
        return *this; \
    }

    DEFINE_BITWISE_OP(|)
    DEFINE_BITWISE_OP(&)
    DEFINE_BITWISE_OP(^)

#undef DEFINE_BITWISE_OP

    bool hasFlag(T flag) const {
        return static_cast<T>(
                    static_cast<Type>(val) & static_cast<Type>(flag)
               ) == flag;
    }

    Flags<T>& setFlag(T flag) {
        return *this &= flag;
    }

    Flags<T>& toggleFlag(T flag) {
        return *this ^= flag;
    }

    operator T() const {
        return val;
    }

private:
    T val;
};

#define DEFINE_BITWISE_OP(Op, T) \
    inline T operator Op (T lhs, T rhs)  { \
        typedef detail::UnderlyingType<T>::Type UnderlyingType; \
        return static_cast<T>( \
                    static_cast<UnderlyingType>(lhs) Op static_cast<UnderlyingType>(rhs) \
                ); \
    }

#define DECLARE_FLAGS_OPERATORS(T) \
    DEFINE_BITWISE_OP(&, T) \
    DEFINE_BITWISE_OP(|, T)
