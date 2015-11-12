/* async.h
   Mathieu Stefani, 05 novembre 2015
   
  This header brings a Promise<T> class inspired by the Promises/A+
  specification for asynchronous operations
*/

#pragma once

#include <type_traits>
#include <functional>
#include <memory>
#include "optional.h"

namespace Async {

    class Error : public std::runtime_error {
    public:
        explicit Error(const char* what) : std::runtime_error(what) { }
        explicit Error(const std::string& what) : std::runtime_error(what) { }
    };

    /*
      - Direct: The continuation will be directly called in the context
        of the same thread
      - Deferred: The continuation will be called in an asynchronous way
     
        Note that Deferred requires the Promise to be bound to an event
        loop
    */
    enum class Continuation {
        Direct,
        Deferred
    };

    enum class State {
        Pending, Fulfilled, Rejected
    };

    namespace Private {
        struct IgnoreException {
            void operator()(std::exception_ptr) const { }
        };

        struct NoExcept {
            void operator()(std::exception_ptr) const { std::terminate(); }
        };
    }

    static constexpr Private::IgnoreException IgnoreException;
    static constexpr Private::NoExcept NoExcept;

    template<typename T> class Promise;

    class PromiseBase {
    public:
        virtual ~PromiseBase() { }
        virtual bool isPending() const = 0;
        virtual bool isFulfilled() const = 0;
        virtual bool isRejected() const = 0;

        bool isSettled() const { return isFulfilled() || isRejected(); }
    };

    namespace Private {

        class Core;
        class Request {
        public:

            virtual void resolve(const std::shared_ptr<Private::Core>& core) = 0;
            virtual void reject(const std::shared_ptr<Private::Core>& core) = 0;

        };

        struct Core {
            Core(State state)
                : state(state)
            { }

            State state;
            std::exception_ptr exc;
            std::vector<std::shared_ptr<Request>> requests;

            virtual void* memory() = 0;

            template<typename T, typename... Args>
            void construct(Args&&... args) {
                void *mem = memory();
                new (mem) T(std::forward<Args>(args)...);
                state = State::Fulfilled;
            }
        };

    }

    namespace detail {
        template<typename T>
        struct RemovePromise {
            typedef T Type;
        };

        template<typename T>
        struct RemovePromise<Promise<T>> {
            typedef T Type;
        };

        template<typename Func>
        struct result_of : public result_of<decltype(&Func::operator())> { };

        template<typename R, typename Class, typename... Args>
        struct result_of<R (Class::*) (Args...) const> {
            typedef R Type;
        };
    }

    class Resolver {
    public:
        Resolver(const std::shared_ptr<Private::Core> &core)
            : core_(core)
        { }

        template<typename Arg>
        bool operator()(Arg&& arg) {
            typedef typename std::remove_reference<Arg>::type Type;

            if (core_->state != State::Pending)
                throw Error("Attempt to resolve a fulfilled promise");

            core_->construct<Type>(std::forward<Arg>(arg));
            for (const auto& req: core_->requests) {
                req->resolve(core_);
            }

            return true;
        }

    private:
        std::shared_ptr<Private::Core> core_;
    };

    class Rejection {
    public:
        Rejection(const std::shared_ptr<Private::Core>& core)
            : core_(core)
        { }


        template<typename Exc>
        bool operator()(Exc exc) {
            if (core_->state != State::Pending)
                throw Error("Attempt to reject a fulfilled promise");

            core_->exc = std::make_exception_ptr(exc);
            core_->state = State::Rejected;
            for (const auto& req: core_->requests) {
                req->reject(core_);
            }

            return true;
        }

    private:
        std::shared_ptr<Private::Core> core_;

    };

    namespace detail {
        template<typename Func, typename T>
        struct IsCallable {

            template<typename U>
            static auto test(U *) -> decltype(std::declval<Func>()(std::declval<T>()), std::true_type());

            template<typename U>
            static auto test(...) -> std::false_type;

            static constexpr bool value = std::is_same<decltype(test<T>(0)), std::true_type>::value;
        };
    }

    template<typename T>
    class Promise : public PromiseBase
    {
    public:
        template<typename U> friend class Promise;

        typedef std::function<void (Resolver&, Rejection&)> ResolveFunc;

        Promise(ResolveFunc func)
            : core_(std::make_shared<CoreT>())
            , resolver_(core_)
            , rejection_(core_)
        { 
            func(resolver_, rejection_);
        }

        Promise(const Promise<T>& other) = delete;
        Promise& operator=(const Promise<T>& other) = delete;

        Promise(Promise<T>&& other) = default;
        Promise& operator=(Promise<T>&& other) = default;

        ~Promise()
        {
        }

        bool isPending() const { return core_->state == State::Pending; }
        bool isFulfilled() const { return core_->state == State::Fulfilled; }
        bool isRejected() const { return core_->state == State::Rejected; }

        template<typename ResolveFunc, typename RejectFunc>
        auto
        then(ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc, Continuation type = Continuation::Direct)
            -> Promise<
                typename detail::RemovePromise<
                    typename detail::result_of<ResolveFunc>::Type
                >::Type
              >
        {
            static_assert(detail::IsCallable<ResolveFunc, T>::value, "Function is not compatible with underlying promise type");

           typedef typename detail::RemovePromise<
                typename detail::result_of<ResolveFunc>::Type
           >::Type RetType;

           Promise<RetType> promise;

            // Due to how template argument deduction works on universal references, we need to remove any reference from
            // the deduced function type, fun fun fun
            typedef typename std::remove_reference<ResolveFunc>::type ResolveFuncType;
            typedef typename std::remove_reference<RejectFunc>::type RejectFuncType;

            std::shared_ptr<Private::Request> req;
            req.reset(ContinuationFactory<ResolveFuncType>::create(
                          promise.core_,
                          std::forward<ResolveFunc>(resolveFunc),
                          std::forward<RejectFunc>(rejectFunc)));

            if (isFulfilled()) {
                req->resolve(core_);
            }
            else if (isRejected()) {
                req->reject(core_);
            }

           core_->requests.push_back(req);

           return promise;
        }

    private:
        template<typename U>
        struct Core : public Private::Core {
            Core()
                : Private::Core(State::Pending)
            { }

            template<class Other>
            struct Rebind {
                typedef Core<Other> Type;
            };

            typedef typename std::aligned_storage<sizeof(U), alignof(U)>::type Storage;
            Storage storage;

            const U& value() const {
                if (state != State::Fulfilled)
                    throw Error("Attempted to take the value of a not fulfilled promise");

                return *reinterpret_cast<const U*>(&storage);
            }

            void *memory() {
                return &storage;
            }
        };

        typedef Core<T> CoreT;

        Promise()
          : core_(std::make_shared<Core<T>>())
          , resolver_(core_)
          , rejection_(core_)
        {
        }

        struct Continuable : public Private::Request {
            Continuable()
                : resolveCount_(0)
                , rejectCount_(0)
            { }

            void resolve(const std::shared_ptr<Private::Core>& core) {
                if (resolveCount_ >= 1)
                    throw Error("Resolve must not be called more than once");

                doResolve(coreCast(core));
                ++resolveCount_;
            }

            void reject(const std::shared_ptr<Private::Core>& core) {
                if (rejectCount_ >= 1)
                    throw Error("Reject must not be called more than once");

                doReject(coreCast(core));
                ++rejectCount_;
            }

            std::shared_ptr<CoreT> coreCast(const std::shared_ptr<Private::Core>& core) const {
                return std::static_pointer_cast<CoreT>(core);
            }

            virtual void doResolve(const std::shared_ptr<CoreT>& core) const = 0;
            virtual void doReject(const std::shared_ptr<CoreT>& core) const = 0;

            size_t resolveCount_;
            size_t rejectCount_;
        };

        template<typename ResolveFunc, typename RejectFunc>
        struct ThenContinuation : public Continuable {
            ThenContinuation(
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc)
                : resolveFunc_(std::forward<ResolveFunc>(resolveFunc))
                , rejectFunc_(std::forward<RejectFunc>(rejectFunc))
            { 
            }

            void doResolve(const std::shared_ptr<CoreT>& core) const {
                resolveFunc_(core->value());
            }

            void doReject(const std::shared_ptr<CoreT>& core) const {
                rejectFunc_(core->exc);
            }

            std::shared_ptr<Private::Core> chain_;
            ResolveFunc resolveFunc_;
            RejectFunc rejectFunc_;
        };

        template<typename ResolveFunc, typename RejectFunc, typename Return>
        struct ThenReturnContinuation : public Continuable {
            ThenReturnContinuation(
                    const std::shared_ptr<Private::Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc)
                : chain_(chain)
                , resolveFunc_(std::forward<ResolveFunc>(resolveFunc))
                , rejectFunc_(std::forward<RejectFunc>(rejectFunc))
            {
            }

            void doResolve(const std::shared_ptr<CoreT>& core) const {
                auto ret = resolveFunc_(core->value());
                chain_->construct<decltype(ret)>(std::move(ret));
            }

            void doReject(const std::shared_ptr<CoreT>& core) const {
                rejectFunc_(core->exc);
            }

            std::shared_ptr<Private::Core> chain_;
            ResolveFunc resolveFunc_;
            RejectFunc rejectFunc_;
        };

        template<typename ResolveFunc, typename RejectFunc>
        struct ThenChainContinuation : public Continuable {
            ThenChainContinuation(
                    const std::shared_ptr<Private::Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc)
                : chain_(chain)
                , resolveFunc_(std::forward<ResolveFunc>(resolveFunc))
                , rejectFunc_(std::forward<RejectFunc>(rejectFunc))
            { 
            }

            void doResolve(const std::shared_ptr<CoreT>& core) const {
                auto promise = resolveFunc_(core->value());
            }

            void doReject(const std::shared_ptr<CoreT>& core) const {
                rejectFunc_(core->exc);
            }

            std::shared_ptr<Private::Core> chain_;
            ResolveFunc resolveFunc_;
            RejectFunc rejectFunc_;

        };

        template<typename ResolveFunc>
            struct ContinuationFactory : public ContinuationFactory<decltype(&ResolveFunc::operator())> { };

        template<typename R, typename Class, typename... Args>
        struct ContinuationFactory<R (Class::*)(Args...) const> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable *create(
                    const std::shared_ptr<Private::Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenReturnContinuation<ResolveFunc, RejectFunc, R>(
                        chain,
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };

        template<typename Class, typename... Args>
        struct ContinuationFactory<void (Class::*)(Args ...) const> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable *create(
                    const std::shared_ptr<Private::Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenContinuation<ResolveFunc, RejectFunc>(
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };

        template<typename U, typename Class, typename... Args>
        struct ContinuationFactory<Promise<U> (Class::*)(Args...) const> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable* create(
                    const std::shared_ptr<Private::Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenChainContinuation<ResolveFunc, RejectFunc>(
                        chain,
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };

        std::shared_ptr<CoreT> core_;
        Resolver resolver_;
        Rejection rejection_;
    };

    template<>
    class Promise<void>
    {
    public:
        Promise() :
            core_(std::make_shared<Core>())
        { }
    public:
        struct Core : public Private::Core {
            Core() :
                Private::Core(State::Pending)
            { }

            void *memory() { return nullptr; }
        };
        std::shared_ptr<Core> core_;
    };

} // namespace Async
