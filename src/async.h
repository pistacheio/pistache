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

            virtual void disconnect() = 0;
        };

        struct Core {
            Core(State state)
                : state(state)
                , promise(nullptr)
            { }

            State state;
            std::exception_ptr exc;
            PromiseBase* promise;
            std::vector<std::shared_ptr<Request>> requests;

            void attach(PromiseBase* p) {
                if (promise)
                    throw Error("Trying to double-attach a Promise");

                promise = p;
            }

            void detach() {
                if (!promise)
                    throw Error("Trying to detach a non attached Promise");

                promise = nullptr;
            }

            virtual void* memory() = 0;
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

            auto promise = core_->promise;
            if (!promise) return false;

            if (core_->state != State::Pending)
                throw Error("Attempt to resolve a fulfilled promise");

            void *mem = core_->memory();
            new (mem) Type(std::forward<Arg>(arg));
            core_->state = State::Fulfilled;
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
            auto promise = core_->promise;
            if (!promise)
                return false;

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
        typedef std::function<void (Resolver&, Rejection&)> ResolveFunc;

        struct Request : public Private::Request {
            virtual Promise<T> chain() = 0;

            void disconnect() {
            }

        };

        Promise(ResolveFunc func)
            : core_(std::make_shared<CoreT>())
            , resolver_(core_)
            , rejection_(core_)
        { 
            core_->attach(this);
            func(resolver_, rejection_);
        }

        Promise(const Promise<T>& other) = delete;
        Promise& operator=(const Promise<T>& other) = delete;

        Promise(Promise<T>&& other) = default;
        Promise& operator=(Promise<T>&& other) = default;

        ~Promise()
        {
            core_->detach();
        }

        bool isPending() const { return core_->state == State::Pending; }
        bool isFulfilled() const { return core_->state == State::Fulfilled; }
        bool isRejected() const { return core_->state == State::Rejected; }

        template<typename ResolveFunc, typename RejectFunc>
        std::shared_ptr<Request> then(ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc, Continuation type = Continuation::Direct) {
            static_assert(detail::IsCallable<ResolveFunc, T>::value, "Function is not compatible with underlying promise type");

            // Due to how template argument deduction works on universal references, we need to remove any reference from
            // the deduced function type, fun fun fun
            typedef typename std::remove_reference<ResolveFunc>::type ResolveFuncType;
            typedef typename std::remove_reference<RejectFunc>::type RejectFuncType;

            std::shared_ptr<Request> req;
            req.reset(ContinuationFactory<ResolveFuncType>::create(
                        std::forward<ResolveFunc>(resolveFunc), std::forward<RejectFunc>(rejectFunc)));

            if (isFulfilled()) {
                req->resolve(core_);
            }
            else if (isRejected()) {
                req->reject(core_);
            }

            core_->requests.push_back(req);
            return req;
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

        Promise(const std::shared_ptr<CoreT>& core) 
          : core_(core)
          , resolver_(core)
          , rejection_(core)
        {
            core_->attach(this);
        }


        struct Continuable : public Request {
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
            ThenContinuation(ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc)
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

            Promise<T> chain() {
                throw Error("The request is not chainable");
            }

            ResolveFunc resolveFunc_;
            RejectFunc rejectFunc_;
        };

        template<typename ResolveFunc, typename RejectFunc, typename Return>
        struct ThenReturnContinuation : public Continuable {
            ThenReturnContinuation(ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc)
                : resolveFunc_(std::forward<ResolveFunc>(resolveFunc))
                , rejectFunc_(std::forward<RejectFunc>(rejectFunc))
            {
            }

            void doResolve(const std::shared_ptr<CoreT>& core) const {
                auto ret = resolveFunc_(core->value());
                result = Some(std::move(ret));
            }

            void doReject(const std::shared_ptr<CoreT>& core) const {
                rejectFunc_(core->exc);
            }

            Promise<Return> chain() {
                typedef typename CoreT::template Rebind<Return>::Type CoreType;
                auto core = std::make_shared<CoreType>();
                optionally_do(result, [&core](Return&& result) {
                    void *mem = core->memory();
                    new (mem) Return(std::move(result));
                    core->state = State::Fulfilled;
                });

                return Promise<Return>(core);
            }

            ResolveFunc resolveFunc_;
            RejectFunc rejectFunc_;

            mutable Optional<Return> result;
        };

        template<typename ResolveFunc, typename RejectFunc>
        struct ThenChainContinuation : public Continuable {
            ThenChainContinuation(ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc)
                : resolveFunc_(std::forward<ResolveFunc>(resolveFunc))
                , rejectFunc_(std::forward<RejectFunc>(rejectFunc))
            { 
            }

            void doResolve(const std::shared_ptr<CoreT>& core) const {
                auto promise = resolveFunc_(core->value());
            }

            void doReject(const std::shared_ptr<CoreT>& core) const {
                rejectFunc_(core->exc);
            }

            Promise<T> chain() {
                auto core = std::make_shared<CoreT>();
                return Promise<T>(core);
            }

            ResolveFunc resolveFunc_;
            RejectFunc rejectFunc_;

        };

        template<typename ResolveFunc>
            struct ContinuationFactory : public ContinuationFactory<decltype(&ResolveFunc::operator())> { };

        template<typename R, typename Class, typename... Args>
        struct ContinuationFactory<R (Class::*)(Args...) const> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable *create(ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenReturnContinuation<ResolveFunc, RejectFunc, R>(
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };

        template<typename Class, typename... Args>
        struct ContinuationFactory<void (Class::*)(Args ...) const> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable *create(ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenContinuation<ResolveFunc, RejectFunc>(
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };

        template<typename U, typename Class, typename... Args>
        struct ContinuationFactory<Promise<U> (Class::*)(Args...) const> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable* create(ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenChainContinuation<ResolveFunc, RejectFunc>(
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };

        std::shared_ptr<CoreT> core_;
        Resolver resolver_;
        Rejection rejection_;
    };

} // namespace Async
