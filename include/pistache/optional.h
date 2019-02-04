/* optional.h
   Mathieu Stefani, 27 August 2015

   An algebraic data type that can either represent Some Value or None.
   This type is the equivalent of the Haskell's Maybe type
*/

#pragma once

#include <cstdlib>
#include <cstring>
#include <utility>
#include <iostream>
#include <tuple>
#include <functional>

namespace Pistache {

template<typename T> class Optional;

namespace types {
    template<typename T>
    class Some {
    public:
        template<typename U> friend class Pistache::Optional;

        Some(const T &val) : val_(val) { }
        Some(T &&val) : val_(std::move(val)) { }

    private:
        T val_;
    };

    class None { };

    namespace impl {
        template<typename Func> struct callable_trait :
            public callable_trait<decltype(&Func::operator())> { };

        template<typename Class, typename Ret, typename... Args>
        struct callable_trait<Ret (Class::*)(Args...) const>
        {
            static constexpr size_t Arity = sizeof...(Args);

            typedef std::tuple<Args...> ArgsType;
            typedef Ret ReturnType;

            template<size_t Index>
            struct Arg {
                static_assert(Index < Arity, "Invalid index");
                typedef typename std::tuple_element<Index, ArgsType>::type Type;
            };

        };
    }

    template<typename Func,
             bool IsBindExp = std::is_bind_expression<Func>::value>
    struct callable_trait;

    /* std::bind returns an unspecified type which contains several overloads
     * of operator(). Thus, decltype(&operator()) does not compile since operator()
     * is overloaded. To bypass that, we only define the ReturnType for bind
     * expressions
     */
    template<typename Func>
    struct callable_trait<Func, true> {
        typedef typename Func::result_type ReturnType;
    };

    template<typename Func>
    struct callable_trait<Func, false> : public impl::callable_trait<Func> {
    };

    template<typename T>
    struct is_nothrow_move_constructible :
        std::is_nothrow_constructible<T, typename std::add_rvalue_reference<T>::type> {};

    template<class T>
    struct is_move_constructible :
        std::is_constructible<T, typename std::add_rvalue_reference<T>::type> {};

}


inline types::None None() {
    return types::None();
}

template<typename T,
         typename CleanT = typename std::remove_reference<T>::type>
inline types::Some<CleanT> Some(T &&value) {
    return types::Some<CleanT>(std::forward<T>(value));
}


template<typename T>
class Optional {
public:
    Optional() {
        none_flag = NoneMarker;
    }

    // TODO: SFINAE-out if T is not trivially_copyable
    Optional(const Optional<T>& other)
    {
        if (!other.isEmpty()) {
            ::new (data()) T(*other.data());
        }
        else {
            none_flag = NoneMarker;
        }
    }

    Optional(Optional<T> &&other)
      noexcept(types::is_nothrow_move_constructible<T>::value)
    {
        *this = std::move(other);
    }

    template<typename U>
    Optional(types::Some<U> some) {
        static_assert(std::is_same<T, U>::value || std::is_convertible<U, T>::value,
                      "Types mismatch");
        from_some_helper(std::move(some), types::is_move_constructible<U>());
    }
    Optional(types::None) { none_flag = NoneMarker; }

    template<typename U>
    Optional<T> &operator=(types::Some<U> some) {
        static_assert(std::is_same<T, U>::value || std::is_convertible<U, T>::value,
                      "Types mismatch");
        if (none_flag != NoneMarker) {
            data()->~T();
        }
        from_some_helper(std::move(some), types::is_move_constructible<U>());
        return *this;
    }

    Optional<T> &operator=(types::None) {
        if (none_flag != NoneMarker) {
            data()->~T();
        }
        none_flag = NoneMarker;
        return *this;
    }

    // TODO: SFINAE-out if T is not trivially_copyable
    Optional<T>& operator=(const Optional<T>& other)
    {
        if (!other.isEmpty()) {
            if (none_flag != NoneMarker) {
                data()->~T();
            }
            ::new (data()) T(*other.data());
        }
        else {
            if (none_flag != NoneMarker) {
                data()->~T();
            }
            none_flag = NoneMarker;
        }

        return *this;
    }


    Optional<T> &operator=(Optional<T> &&other)
      noexcept(types::is_nothrow_move_constructible<T>::value)
    {
        if (!other.isEmpty()) {
            move_helper(std::move(other), types::is_move_constructible<T>());
            other.none_flag = NoneMarker;
        }
        else {
            none_flag = NoneMarker;
        }

        return *this;
    }


    bool isEmpty() const {
        return none_flag == NoneMarker;
    }

    T getOrElse(const T &defaultValue) {
        if (none_flag != NoneMarker) {
            return *constData();
        }

        return defaultValue;
    }

    const T& getOrElse(const T &defaultValue) const {
        if (none_flag != NoneMarker) {
            return *constData();
        }

        return defaultValue;
    }

    template<typename Func>
    void orElse(Func func) const {
        if (isEmpty()) {
            func();
        }
    }

    T get() {
        return *constData();
    }

    const T& get() const {
        return *constData();
    }

    T& unsafeGet() const {
        return *data();
    }

    ~Optional() {
        if (!isEmpty()) {
            data()->~T();
        }
    }


private:
    T *constData() const {
        return const_cast<T *const>(reinterpret_cast<const T *const>(bytes));
    }

    T *data() const {
        return const_cast<T *>(reinterpret_cast<const T *>(bytes));
    }

    void move_helper(Optional<T> &&other, std::true_type) {
        ::new (data()) T(std::move(*other.data()));
    }

    void move_helper(Optional<T> &&other, std::false_type) {
        ::new (data()) T(*other.data());
    }

    template<typename U>
    void from_some_helper(types::Some<U> some, std::true_type) {
        ::new (data()) T(std::move(some.val_));
    }

    template<typename U>
    void from_some_helper(types::Some<U> some, std::false_type) {
        ::new (data()) T(some.val_);
    }

    typedef uint8_t none_flag_t;
    static constexpr none_flag_t NoneMarker = 1;

    union {
        uint8_t bytes[sizeof(T)];
        none_flag_t none_flag;
    };
};

#define PistacheCheckSize(Type) \
    static_assert(sizeof(Optional<Type>) == sizeof(Type), "Size differs")

PistacheCheckSize(uint8_t);
PistacheCheckSize(uint16_t);
PistacheCheckSize(int);
PistacheCheckSize(void *);
PistacheCheckSize(std::string);

namespace details {
    template<typename T>
    struct RemoveOptional {
        typedef T Type;
    };

    template<typename T>
    struct RemoveOptional<Optional<T>> {
        typedef T Type;
    };

    template<typename T, typename Func>
    void do_static_checks(std::false_type) {
        static_assert(types::callable_trait<Func>::Arity == 1,
            "The function must take exactly 1 argument");

        typedef typename types::callable_trait<Func>::template Arg<0>::Type ArgType;
        typedef typename std::remove_cv<
                            typename std::remove_reference<ArgType>::type
                         >::type CleanArgType;

        static_assert(std::is_same<CleanArgType, T>::value
                   || std::is_convertible<CleanArgType, T>::value,
                      "Function parameter type mismatch");
    }

    template<typename T, typename Func>
    void do_static_checks(std::true_type) {
    }

    template<typename T, typename Func>
    void static_checks() {
        do_static_checks<T, Func>(std::is_bind_expression<Func>());
    }

    template<typename Func>
    struct IsArgMovable : public IsArgMovable<decltype(&Func::operator())> { };

    template<typename R, typename Class, typename Arg>
    struct IsArgMovable<R (Class::*)(Arg) const> : public std::is_rvalue_reference<Arg> { };

    template<typename Func, typename Arg>
    typename std::conditional<
        IsArgMovable<Func>::value,
        Arg&&,
        const Arg&
    >::type tryMove(Arg& arg) {
        return std::move(arg);
    }
}

template<typename T, typename Func>
const Optional<T>&
optionally_do(const Optional<T> &option, Func func) {
    details::static_checks<T, Func>();
    static_assert(std::is_same<typename types::callable_trait<Func>::ReturnType, void>::value,
                  "Use optionally_map if you want to return a value");
    if (!option.isEmpty()) {
        func(details::tryMove<Func>(option.unsafeGet()));
    }

    return option;
}


template<typename T, typename Func>
auto
optionally_map(const Optional<T> &option, Func func)
    -> Optional<typename types::callable_trait<Func>::ReturnType>
{
    details::static_checks<T, Func>();
    if (!option.isEmpty()) {
        return Some(func(details::tryMove<Func>(option.unsafeGet())));
    }

    return None();
}

template<typename T, typename Func>
auto
optionally_fmap(const Optional<T> &option, Func func)
    -> Optional<typename details::RemoveOptional<typename types::callable_trait<Func>::ReturnType>::Type>
{
    details::static_checks<T, Func>();
    if (!option.isEmpty()) {
        const auto &ret = func(details::tryMove<Func>(option.unsafeGet()));
        if (!ret.isEmpty()) {
            return Some(ret.get());
        }
    }

    return None();
}

template<typename T, typename Func>
Optional<T> optionally_filter(const Optional<T> &option, Func func) {
    details::static_checks<T, Func>();
    typedef typename types::callable_trait<Func>::ReturnType ReturnType;
    static_assert(std::is_same<ReturnType, bool>::value ||
                  std::is_convertible<ReturnType, bool>::value,
                  "The predicate must return a boolean value");
    if (!option.isEmpty() && func(option.get())) {
        return Some(option.get());
    }

    return None();
}

} // namespace Pistache
