/* flags.h
   Mathieu Stefani, 18 August 2015
   
   Make it easy to have bitwise operators for scoped or unscoped enumerations
*/

#pragma once

#include <type_traits>
#include <bitset>
#include <climits>
#include <iostream>

namespace Pistache {

template < typename T, typename = void >
class Flags;

template < typename T >
class Flags<T, typename std::enable_if<std::is_enum<T>::value>::type > {
public:
    constexpr Flags() = default;
    Flags( const Flags& other ) = default;
    Flags& operator= ( const Flags& other ) = default;

    constexpr Flags( T value ) :
        _mask(1ULL<<static_cast<integral_type>(value))
    {
    }

    Flags( std::initializer_list<T> values ) :
        _mask()
    {
        for( T value : values ) {
            _mask.set(static_cast<integral_type>(value));
        }
    }

    bool operator[]( T position ) const {
        return _mask[static_cast<integral_type>(position)];
    }

    void set( T position, bool value = true ) {
        _mask.set( static_cast<integral_type>(position), value );
    }

    void reset( T position ) {
        _mask.reset( static_cast<integral_type>(position) );
    }

    void flip( T position ) {
        _mask.flip( static_cast<integral_type>(position) );
    }

    bool all() const { return _mask.all(); }
    bool any() const { return _mask.any(); }
    bool none() const { return _mask.none(); }

    std::string to_string() const {
        return _mask.to_string();
    }

    Flags operator& ( const Flags& other ) { return Flags(_mask & other._mask); }
    Flags operator| ( const Flags& other ) { return Flags(_mask & other._mask); }
    Flags operator^ ( const Flags& other ) { return Flags(_mask & other._mask); }
    Flags operator~ () const { return Flags(~_mask); }

    Flags& operator&= ( const Flags& other ) { _mask &= other._mask; return *this; }
    Flags& operator|= ( const Flags& other ) { _mask &= other._mask; return *this; }
    Flags& operator^= ( const Flags& other ) { _mask &= other._mask; return *this; }

private:
    typedef typename std::underlying_type<T>::type integral_type;
    typedef std::bitset<CHAR_BIT*sizeof(integral_type)> mask_type;

    Flags( const mask_type mask ) :
        _mask(mask)
    {
    }

    mask_type _mask;
};

template < typename T >
Flags<T> make_flags( std::initializer_list<T> values ) {
    return Flags<T>(values);
}

template < typename T >
std::ostream& operator<<(std::ostream& os, const Flags<T> flags) {
    os << flags.to_string();
    return os;
}

} // namespace Pistache

