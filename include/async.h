/* async.h
   Mathieu Stefani, 05 novembre 2015
   
  This header brings a Promise<T> class inspired by the Promises/A+
  specification for asynchronous operations
*/

#pragma once

#include <type_traits>
#include <functional>
#include <memory>
#include <atomic>
#include <vector>
#include "optional.h"
#include "typeid.h"

namespace Async {

    class Error : public std::runtime_error {
    public:
        explicit Error(const char* what) : std::runtime_error(what) { }
        explicit Error(const std::string& what) : std::runtime_error(what) { }
    };

    class BadType : public Error {
    public:
        BadType(TypeId id)
            : Error("Argument type can not be used to resolve the promise "
                  " (TypeId does not match)")
            , id_(std::move(id))
        { }

      TypeId typeId() const {
          return id_;
      }

    private:
        TypeId id_;
    };

    class BadAnyCast : public std::bad_cast {
    public:
        virtual const char* what() const noexcept { return "Bad any cast"; }
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

    template<typename T> class Promise;

    class PromiseBase {
    public:
        virtual ~PromiseBase() { }
        virtual bool isPending() const = 0;
        virtual bool isFulfilled() const = 0;
        virtual bool isRejected() const = 0;

        bool isSettled() const { return isFulfilled() || isRejected(); }
    };

    namespace detail {
        template<typename Func, typename T>
        struct IsCallable {

            template<typename U>
            static auto test(U *) -> decltype(std::declval<Func>()(std::declval<U>()), std::true_type());

            template<typename U>
            static auto test(...) -> std::false_type;

            static constexpr bool value = std::is_same<decltype(test<T>(0)), std::true_type>::value;
        };

        template<typename Func>
        struct IsMoveCallable : public IsMoveCallable<decltype(&Func::operator())> { };

        template<typename R, typename Class, typename Arg>
        struct IsMoveCallable<R (Class::*)(Arg) const> : public std::is_rvalue_reference<Arg> { };

        template<typename Func, typename Arg>
        typename std::conditional<
            IsMoveCallable<Func>::value,
            Arg&&,
            const Arg&
        >::type tryMove(Arg& arg) {
            return std::move(arg);
        }

        template<typename Func>
        struct FunctionTrait : public FunctionTrait<decltype(&Func::operator())> { };

        template<typename R, typename Class, typename... Args>
        struct FunctionTrait<R (Class::*)(Args...) const> {
            typedef R ReturnType;

            static constexpr size_t ArgsCount = sizeof...(Args);
        };

        template<typename R, typename Class, typename... Args>
        struct FunctionTrait<R (Class::*)(Args...)> {
            typedef R ReturnType;

            static constexpr size_t ArgsCount = sizeof...(Args);
        };

        template<typename T>
        struct RemovePromise {
            typedef T Type;
        };

        template<typename T>
        struct RemovePromise<Promise<T>> {
            typedef T Type;
        };

    }

    namespace Private {

        struct InternalRethrow {
            InternalRethrow(std::exception_ptr exc)
                : exc(std::move(exc))
            { }

            std::exception_ptr exc;
        };

        struct IgnoreException {
            void operator()(std::exception_ptr) const { }
        };

        struct NoExcept {
            void operator()(std::exception_ptr) const { std::terminate(); }
        };

        struct Throw {
            void operator()(std::exception_ptr exc) const {
                throw InternalRethrow(std::move(exc));
            }
        };

        class Core;

        class Request {
        public:
            virtual void resolve(const std::shared_ptr<Core>& core) = 0;
            virtual void reject(const std::shared_ptr<Core>& core) = 0;
        };

        struct Core {
            Core(State state, TypeId id)
                : state(state)
                , id(id)
            { }

            State state;
            std::exception_ptr exc;
            std::vector<std::shared_ptr<Request>> requests;
            TypeId id;

            virtual void* memory() = 0;

            virtual bool isVoid() const = 0;

            template<typename T, typename... Args>
            void construct(Args&&... args) {
                if (isVoid())
                    throw Error("Can not construct a void core");

                if (id != TypeId::of<T>()) {
                    throw BadType(id);
                }

                void *mem = memory();
                new (mem) T(std::forward<Args>(args)...);
                state = State::Fulfilled;
            }

        };

        template<typename T>
        struct CoreT : public Core {
            CoreT()
                : Core(State::Pending, TypeId::of<T>())
            { }

            template<class Other>
            struct Rebind {
                typedef CoreT<Other> Type;
            };

            typedef typename std::aligned_storage<sizeof(T), alignof(T)>::type Storage;
            Storage storage;

            T& value() {
                if (state != State::Fulfilled)
                    throw Error("Attempted to take the value of a not fulfilled promise");

                return *reinterpret_cast<T*>(&storage);
            }

            bool isVoid() const { return false; }

            void *memory() {
                return &storage;
            }
        };

        template<>
        struct CoreT<void> : public Core {
            CoreT()
                : Core(State::Pending, TypeId::of<void>())
            { }

            bool isVoid() const { return true; }

            void *memory() {
                return nullptr;
            }
        };

        template<typename T>
        struct Continuable : public Request {
            Continuable(const std::shared_ptr<Core>& chain)
                : chain_(chain)
                , resolveCount_(0)
                , rejectCount_(0)
            { }

            void resolve(const std::shared_ptr<Core>& core) {
                if (resolveCount_ >= 1)
                    throw Error("Resolve must not be called more than once");

                doResolve(coreCast(core));
                ++resolveCount_;
            }

            void reject(const std::shared_ptr<Core>& core) {
                if (rejectCount_ >= 1)
                    throw Error("Reject must not be called more than once");

                try {
                    doReject(coreCast(core));
                } catch (const InternalRethrow& e) {
                    chain_->exc = std::move(e.exc);
                    chain_->state = State::Rejected;
                    for (const auto& req: chain_->requests) {
                        req->reject(chain_);
                    }
                }

                ++rejectCount_;
            }

            std::shared_ptr<CoreT<T>> coreCast(const std::shared_ptr<Core>& core) const {
                return std::static_pointer_cast<CoreT<T>>(core);
            }

            virtual void doResolve(const std::shared_ptr<CoreT<T>>& core) = 0;
            virtual void doReject(const std::shared_ptr<CoreT<T>>& core) = 0;

            size_t resolveCount_;
            size_t rejectCount_;
            std::shared_ptr<Core> chain_;
        };

        template<typename T, typename ResolveFunc, typename RejectFunc>
        struct ThenContinuation : public Continuable<T> {
            ThenContinuation(
                    const std::shared_ptr<Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc)
                : Continuable<T>(chain)
                , resolveFunc_(std::forward<ResolveFunc>(resolveFunc))
                , rejectFunc_(std::forward<RejectFunc>(rejectFunc))
            { 
            }

            void doResolve(const std::shared_ptr<CoreT<T>>& core) {
                doResolveImpl(core, std::is_void<T>());
            }

            void doReject(const std::shared_ptr<CoreT<T>>& core) {
                rejectFunc_(core->exc);
            }

            void doResolveImpl(const std::shared_ptr<CoreT<T>>& core, std::true_type /* is_void */) {
                resolveFunc_();
            }

            void doResolveImpl(const std::shared_ptr<CoreT<T>>& core, std::false_type /* is_void */) {
                resolveFunc_(core->value());
            }

            ResolveFunc resolveFunc_;
            RejectFunc rejectFunc_;
        };

        template<typename T, typename ResolveFunc, typename RejectFunc, typename Return>
        struct ThenReturnContinuation : public Continuable<T> {
            ThenReturnContinuation(
                    const std::shared_ptr<Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc)
                : Continuable<T>(chain)
                , resolveFunc_(std::forward<ResolveFunc>(resolveFunc))
                , rejectFunc_(std::forward<RejectFunc>(rejectFunc))
            {
            }

            void doResolve(const std::shared_ptr<CoreT<T>>& core) {
                doResolveImpl(core, std::is_void<T>());
            }

            void doReject(const std::shared_ptr<CoreT<T>>& core) {
                rejectFunc_(core->exc);
                for (const auto& req: this->chain_->requests) {
                    req->reject(this->chain_);
                }
            }

            void doResolveImpl(const std::shared_ptr<CoreT<T>>& core, std::true_type /* is_void */) {
                finishResolve(resolveFunc_());
            }

            void doResolveImpl(const std::shared_ptr<CoreT<T>>& core, std::false_type /* is_void */) {
                finishResolve(resolveFunc_(detail::tryMove<ResolveFunc>(core->value())));
            }

            template<typename Ret>
            void finishResolve(Ret&& ret) const {
                typedef typename std::remove_reference<Ret>::type CleanRet;
                this->chain_->template construct<CleanRet>(std::forward<Ret>(ret));
                for (const auto& req: this->chain_->requests) {
                    req->resolve(this->chain_);
                }
            }

            ResolveFunc resolveFunc_;
            RejectFunc rejectFunc_;
        };

        template<typename T, typename ResolveFunc, typename RejectFunc>
        struct ThenChainContinuation : public Continuable<T> {
            ThenChainContinuation(
                    const std::shared_ptr<Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc)
                : Continuable<T>(chain)
                , resolveFunc_(std::forward<ResolveFunc>(resolveFunc))
                , rejectFunc_(std::forward<RejectFunc>(rejectFunc))
            { 
            }

            void doResolve(const std::shared_ptr<CoreT<T>>& core) {
                doResolveImpl(core, std::is_void<T>());
            }

            void doReject(const std::shared_ptr<CoreT<T>>& core) {
                rejectFunc_(core->exc);
                for (const auto& req: core->requests) {
                    req->reject(core);
                }
            }

            void doResolveImpl(const std::shared_ptr<CoreT<T>>& core, std::true_type /* is_void */) {
                auto promise = resolveFunc_();
                finishResolve(promise);
            }

            void doResolveImpl(const std::shared_ptr<CoreT<T>>& core, std::false_type /* is_void */) {
                auto promise = resolveFunc_(detail::tryMove<ResolveFunc>(core->value()));
                finishResolve(promise);
            }

            template<typename P>
            void finishResolve(P& promise) {
                auto chainer = makeChainer(promise);
                promise.then(std::move(chainer), [=](std::exception_ptr exc) {
                    auto core = this->chain_;
                    core->exc = std::move(exc);
                    core->state = State::Rejected;

                    for (const auto& req: core->requests) {
                        req->reject(core);
                    }
                });
            }

            ResolveFunc resolveFunc_;
            RejectFunc rejectFunc_;

            template<typename PromiseType>
            struct Chainer {
                Chainer(const std::shared_ptr<Private::Core>& core)
                    : chainCore(core)
                { }

                void operator()(const PromiseType& val) {
                    chainCore->construct<PromiseType>(val);
                    for (const auto& req: chainCore->requests) {
                        req->resolve(chainCore);
                    }
                }

                std::shared_ptr<Core> chainCore;
            };

            template<
                typename Promise,
                typename Type = typename detail::RemovePromise<Promise>::Type>
            Chainer<Type>
            makeChainer(const Promise&) {
                return Chainer<Type>(this->chain_);
            }

        };

        template<typename T, typename ResolveFunc>
            struct ContinuationFactory : public ContinuationFactory<T, decltype(&ResolveFunc::operator())> { };

        /* Const */
        template<typename T, typename R, typename Class, typename... Args>
        struct ContinuationFactory<T, R (Class::*)(Args...) const> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable<T>* create(
                    const std::shared_ptr<Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenReturnContinuation<T, ResolveFunc, RejectFunc, R>(
                        chain,
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };

        template<typename T, typename Class, typename... Args>
        struct ContinuationFactory<T, void (Class::*)(Args ...) const> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable<T>* create(
                    const std::shared_ptr<Private::Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenContinuation<T, ResolveFunc, RejectFunc>(
                        chain,
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };

        template<typename T, typename U, typename Class, typename... Args>
        struct ContinuationFactory<T, Promise<U> (Class::*)(Args...) const> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable<T>* create(
                    const std::shared_ptr<Private::Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenChainContinuation<T, ResolveFunc, RejectFunc>(
                        chain,
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };

        /* Non-const */
        template<typename T, typename R, typename Class, typename... Args>
        struct ContinuationFactory<T, R (Class::*)(Args...)> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable<T>* create(
                    const std::shared_ptr<Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenReturnContinuation<T, ResolveFunc, RejectFunc, R>(
                        chain,
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };

        template<typename T, typename Class, typename... Args>
        struct ContinuationFactory<T, void (Class::*)(Args ...)> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable<T>* create(
                    const std::shared_ptr<Private::Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenContinuation<T, ResolveFunc, RejectFunc>(
                        chain,
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };

        template<typename T, typename U, typename Class, typename... Args>
        struct ContinuationFactory<T, Promise<U> (Class::*)(Args...)> {
            template<typename ResolveFunc, typename RejectFunc>
            static Continuable<T>* create(
                    const std::shared_ptr<Private::Core>& chain,
                    ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc) {
                return new ThenChainContinuation<T, ResolveFunc, RejectFunc>(
                        chain,
                        std::forward<ResolveFunc>(resolveFunc),
                        std::forward<RejectFunc>(rejectFunc));
            }
        };
    }

    class Resolver {
    public:

        Resolver(const std::shared_ptr<Private::Core> &core)
            : core_(core)
        { }

        Resolver(const Resolver& other) = delete;
        Resolver& operator=(const Resolver& other) = delete;

        Resolver(Resolver&& other) = default;
        Resolver& operator=(Resolver&& other) = default; 

        template<typename Arg>
        bool operator()(Arg&& arg) const {
            typedef typename std::remove_reference<Arg>::type Type;

            if (core_->state != State::Pending)
                throw Error("Attempt to resolve a fulfilled promise");

            /* In a ideal world, this should be checked at compile-time rather
             * than runtime. However, since types are erased, this looks like
             * a difficult task
             */
            if (core_->isVoid())
                throw Error("Attempt to resolve a void promise with arguments");

            core_->construct<Type>(std::forward<Arg>(arg));
            for (const auto& req: core_->requests) {
                req->resolve(core_);
            }

            return true;
        }

        bool operator()() const {
            if (core_->state != State::Pending)
                throw Error("Attempt to resolve a fulfilled promise");

            if (!core_->isVoid())
                throw Error("Attempt ro resolve a non-void promise with no argument");

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
        bool operator()(Exc exc) const {
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

    struct Holder {
        Holder(Resolver resolver, Rejection rejection)
            : resolver(std::move(resolver))
            , rejection(std::move(rejection))
        { }

        template<typename Arg>
        void resolve(Arg&& arg) const {
            resolver(std::forward<Arg>(arg));
        }

        void resolve() const {
            resolver();
        }

        template<typename Exc>
        void reject(Exc exc) const {
            rejection(std::move(exc));
        }

    private:
        Resolver resolver;
        Rejection rejection;
    };


    static constexpr Private::IgnoreException IgnoreException{};
    static constexpr Private::NoExcept NoExcept{};
    static constexpr Private::Throw Throw{};

    template<typename T>
    class Promise : public PromiseBase
    {
    public:
        template<typename U> friend class Promise;

        typedef std::function<void (Resolver&, Rejection&)> ResolveFunc;
        typedef Private::CoreT<T> Core;


        Promise(ResolveFunc func)
            : core_(std::make_shared<Core>())
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

        template<typename U>
        static
        Promise<T>
        resolved(U&& value) {
            static_assert(!std::is_void<T>::value,
                         "Can not resolve a void promise with parameters");
            static_assert(std::is_same<T, U>::value || std::is_convertible<U, T>::value,
                         "Incompatible value type");

            auto core = std::make_shared<Core>();
            core->template construct<T>(std::forward<U>(value));
            return Promise<T>(std::move(core));
        }

        static
        Promise<void>
        resolved() {
            static_assert(std::is_void<T>::value,
                         "Resolving a non-void promise requires parameters");

            auto core = std::make_shared<Core>();
            core->state = State::Fulfilled;
            return Promise<T>(std::move(core));
        }

        template<typename Exc>
        static Promise<T>
        rejected(Exc exc) {
            auto core = std::make_shared<Core>();
            core->exc = std::make_exception_ptr(exc);
            core->state = State::Rejected;
            return Promise<T>(std::move(core));
        }

        bool isPending() const { return core_->state == State::Pending; }
        bool isFulfilled() const { return core_->state == State::Fulfilled; }
        bool isRejected() const { return core_->state == State::Rejected; }

        template<typename ResolveFunc, typename RejectFunc>
        auto
        then(ResolveFunc&& resolveFunc, RejectFunc&& rejectFunc, Continuation type = Continuation::Direct)
            -> Promise<
                typename detail::RemovePromise<
                    typename detail::FunctionTrait<ResolveFunc>::ReturnType
                >::Type
              >
        {

            thenStaticChecks<ResolveFunc>(std::is_void<T>());
            typedef typename detail::RemovePromise<
                 typename detail::FunctionTrait<ResolveFunc>::ReturnType
            >::Type RetType;

            Promise<RetType> promise;

            // Due to how template argument deduction works on universal references, we need to remove any reference from
            // the deduced function type, fun fun fun
            typedef typename std::remove_reference<ResolveFunc>::type ResolveFuncType;
            typedef typename std::remove_reference<RejectFunc>::type RejectFuncType;

            std::shared_ptr<Private::Request> req;
            req.reset(Private::ContinuationFactory<T, ResolveFuncType>::create(
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
        Promise()
          : core_(std::make_shared<Core>())
          , resolver_(core_)
          , rejection_(core_)
        {
        }

        Promise(std::shared_ptr<Core>&& core)
          : core_(core)
          , resolver_(core_)
          , rejection_(core_)
        { }

        template<typename ResolveFunc>
        void thenStaticChecks(std::true_type /* is_void */) {
            static_assert(detail::FunctionTrait<ResolveFunc>::ArgsCount == 0,
                          "Continuation function of a void promise should not take any argument");
        }

        template<typename ResolveFunc>
        void thenStaticChecks(std::false_type /* is_void */) {
            static_assert(detail::IsCallable<ResolveFunc, T>::value,
                        "Function is not compatible with underlying promise type");
        }

        std::shared_ptr<Core> core_;
        Resolver resolver_;
        Rejection rejection_;
    };

    namespace Impl {
        struct Any;
    }

    class Any {
    public:
        friend class Impl::Any;

        Any(const Any& other) = default;
        Any& operator=(const Any& other) = default;

        Any(Any&& other) = default;
        Any& operator=(Any&& other) = default;

        template<typename T>
        bool is() const {
            return core_->id == TypeId::of<T>();
        }

        template<typename T>
        T cast() const {
            if (!is<T>()) throw BadAnyCast();

            auto core = std::static_pointer_cast<Private::CoreT<T>>(core_);
            return core->value();
        }

    private:
        Any(const std::shared_ptr<Private::Core>& core)
            : core_(core)
        { }
        std::shared_ptr<Private::Core> core_;

    };

    namespace Impl {

        /* Instead of duplicating the code between whenAll and whenAny functions, the main implementation
         * is in the When class below and we configure the class with a policy instead,  depending if we
         * are executing an "all" or "any" operation, how cool is that ?
         */
        struct All {

            struct Data {
                Data(const size_t total, Resolver resolver, Rejection rejection)
                    : total(total)
                    , resolved(0)
                    , rejected(false)
                    , resolve(std::move(resolver))
                    , reject(std::move(rejection))
                { }

                const size_t total;
                std::atomic<size_t> resolved;
                std::atomic<bool> rejected;

                Resolver resolve;
                Rejection reject;
            };

            template<size_t Index, typename T, typename Data>
            static void resolveT(const T& val, Data& data) {
                if (data->rejected) return;

                // @Check thread-safety of std::get ?
                std::get<Index>(data->results) = val;
                data->resolved.fetch_add(1, std::memory_order_relaxed);

                if (data->resolved == data->total) {
                    data->resolve(data->results);
                }
            }

            template<typename Data>
            static void resolveVoid(Data& data) {
                if (data->rejected) return;

                data->resolved.fetch_add(1, std::memory_order_relaxed);

                if (data->resolved == data->total) {
                    data->resolve(data->results);
                }
            }

            template<typename Data>
            static void reject(std::exception_ptr exc, Data& data) {
                data->rejected.store(true);
                data->reject(exc);
            }
        };

        struct Any {

            struct Data {
                Data(size_t, Resolver resolver, Rejection rejection)
                    : done(false)
                    , resolve(std::move(resolver))
                    , reject(std::move(rejection))
                { }

                std::atomic<bool> done;

                Resolver resolve;
                Rejection reject;
            };

            template<size_t Index, typename T, typename Data>
            static void resolveT(const T& val, Data& data) {
                if (data->done) return;

                // Instead of allocating a new core, ideally we could share the same core as
                // the relevant promise but we do not have access to the promise here is so meh
                auto core = std::make_shared<Private::CoreT<T>>();
                core->template construct<T>(val);
                data->resolve(Async::Any(std::move(core)));

                data->done = true;
            }

            template<typename Data>
            static void resolveVoid(Data& data) {
                if (data->done) return;

                auto core = std::make_shared<Private::CoreT<void>>();
                data->resolve(Async::Any(std::move(core)));

                data->done = true;
            }

            template<typename Data>
            static void reject(std::exception_ptr exc, Data& data) {
                data->done.store(true);
                data->reject(exc);
            }
        };

        template<typename ContinuationPolicy>
        struct When {
            When(Resolver resolver, Rejection rejection)
                : resolve(std::move(resolver))
                , reject(std::move(rejection))
            { }

            template<typename... Args>
            void operator()(Args&&... args) {
                whenArgs(std::forward<Args>(args)...);
            }

        private:
            template<typename T, size_t Index, typename Data>
            struct WhenContinuation {
                WhenContinuation(Data data)
                    : data(std::move(data))
                { }

                void operator()(const T& val) const {
                    ContinuationPolicy::template resolveT<Index>(val, data);
                }

                Data data;
            };

            template<size_t Index, typename Data>
            struct WhenContinuation<void, Index, Data> {
                WhenContinuation(Data data)
                    : data(std::move(data))
                { }

                void operator()() const {
                    ContinuationPolicy::resolveVoid(data);
                }

                Data data;

            };

            template<typename T, size_t Index, typename Data>
            WhenContinuation<T, Index, Data>
            makeContinuation(const Data& data) {
                return WhenContinuation<T, Index, Data>(data);
            }

            template<size_t Index, typename Data, typename T>
            void when(const Data& data, Promise<T>& promise) {
                promise.then(makeContinuation<T, Index>(data), [=](std::exception_ptr ptr) {
                    ContinuationPolicy::reject(std::move(ptr), data);
                });
            }

            template<size_t Index, typename Data, typename T>
            void when(const Data& data, T&& arg) {
                typedef typename std::remove_reference<T>::type CleanT;
                auto promise = Promise<CleanT>::resolved(std::forward<T>(arg));
                when<Index>(data, promise);
            }

            template<typename... Args>
            void whenArgs(Args&& ...args) {
                typedef std::tuple<
                            typename detail::RemovePromise<
                                typename std::remove_reference<Args>::type
                            >::Type...
                        > Results;
                /* We need to keep the results alive until the last promise
                 * finishes its execution
                 */

                /* See the trick here ? Basically, we only have access to the real type of the results
                 * in this function. The policy classes do not have access to the full type (std::tuple),
                 * but, instead, take a generic template data type as a parameter. They only need to know
                 * that results is a tuple, they do not need to know the real type of the results.
                 *
                 * This is some sort of compile-time template type-erasing, hue
                 */
                struct Data : public ContinuationPolicy::Data {
                    Data(size_t total, Resolver resolver, Rejection rejection)
                        : ContinuationPolicy::Data(total, std::move(resolver), std::move(rejection))
                    { }

                    Results results;
                };

                auto data = std::make_shared<Data>(sizeof...(Args), std::move(resolve), std::move(reject));
                whenArgs<0>(data, std::forward<Args>(args)...);
            }

            template<size_t Index, typename Data, typename Head,  typename... Rest>
            void whenArgs(const Data& data, Head&& head, Rest&& ...rest) {
                when<Index>(data, std::forward<Head>(head));
                whenArgs<Index + 1>(data, std::forward<Rest>(rest)...);
            }

            template<size_t Index, typename Data, typename Head>
            void whenArgs(const Data& data, Head&& head) {
                when<Index>(data, std::forward<Head>(head));
            }

            Resolver resolve;
            Rejection reject;
        };

        template<
                 typename T,
                 typename Results
                >
        struct WhenAllRange {

            WhenAllRange(Resolver resolve, Rejection reject)
                : resolve(std::move(resolve))
                , reject(std::move(reject))
            { }

            template<typename Iterator>
            void operator()(Iterator first, Iterator last) {

                auto data = std::make_shared<DataT<T>>(
                   static_cast<size_t>(std::distance(first, last)),
                   std::move(resolve),
                   std::move(reject)
                );

                size_t index = 0;
                for (auto it = first; it != last; ++it) {

                    WhenContinuation<T> cont(data, index);

                    it->then(std::move(cont), [=](std::exception_ptr ptr) {
                        data->rejected.store(true);
                        data->reject(std::move(ptr));
                    });

                    ++index;
                }
            }

        private:
            struct Data {
                Data(size_t total, Resolver resolver, Rejection rejection)
                    : total(total)
                    , resolved(0)
                    , rejected(false)
                    , resolve(std::move(resolver))
                    , reject(std::move(rejection))
                { }

                const size_t total;
                std::atomic<size_t> resolved;
                std::atomic<bool> rejected;

                Resolver resolve;
                Rejection reject;

            };

            /* Ok so apparently I can not fully specialize a template structure
             * here, so you know what, compiler ? Take that Dummy type and leave
             * me alone
             */
            template<typename ValueType, typename Dummy = void> struct DataT : public Data {
                DataT(size_t total, Resolver resolver, Rejection rejection)
                    : Data(total, std::move(resolver), std::move(rejection))
                {
                    results.resize(total);
                }

                Results results;
            };

            /* For a vector of void promises, we do not have any results, that's
             * why we need a distinct specialization for the void case
             */
            template<typename Dummy> struct DataT<void, Dummy> : public Data {
                DataT(size_t total, Resolver resolver, Rejection rejection)
                    : Data(total, std::move(resolver), std::move(rejection))
                {
                }
            };

            template<typename ValueType, typename Dummy = void>
            struct WhenContinuation {

                typedef std::shared_ptr<DataT<ValueType>> D;

                WhenContinuation(const D& data, size_t index)
                    : data(data)
                    , index(index)
                { }

                void operator()(const ValueType& val) const {
                    if (data->rejected) return;

                    data->results[index] = val;
                    data->resolved.fetch_add(1);

                    if (data->resolved == data->total) {
                        data->resolve(data->results);
                    }
                }

                D data;
                size_t index;
            };

            template<typename Dummy>
            struct WhenContinuation<void, Dummy> {

                typedef std::shared_ptr<DataT<void>> D;

                WhenContinuation(const D& data, size_t)
                    : data(data)
                { }

                void operator()() const {
                    if (data->rejected) return;

                    data->resolved.fetch_add(1);

                    if (data->resolved == data->total) {
                        data->resolve();
                    }
                }

                D data;
            };

            Resolver resolve;
            Rejection reject;
        };

    }

    template<
        typename... Args,
        typename Results =
        std::tuple<
              typename detail::RemovePromise<
                  typename std::remove_reference<Args>::type
              >::Type...
        >
    >
    Promise<Results> whenAll(Args&& ...args) {
        // As ugly as it looks, this is needed to bypass a bug of gcc < 4.9
        // whereby template parameters pack inside a lambda expression are not
        // captured correctly and can not be expanded inside the lambda.
        Resolver* resolve;
        Rejection* reject;

        Promise<Results> promise([&](Resolver& resolver, Rejection& rejection) {
            resolve = &resolver;
            reject = &rejection;
        });

        Impl::When<Impl::All> impl(std::move(*resolve), std::move(*reject));
        // So we capture everything we need inside the lambda and then call the
        // implementation and expand the parameters pack here
        impl(std::forward<Args>(args)...);

        return promise;
    }

    template<typename... Args>
    Promise<Any> whenAny(Args&& ...args) {
        // Same trick as above;
        Resolver* resolve;
        Rejection* reject;

        Promise<Any> promise([&](Resolver& resolver, Rejection& rejection) {
            resolve = &resolver;
            reject = &rejection;
        });

        Impl::When<Impl::Any> impl(std::move(*resolve), std::move(*reject));
        impl(std::forward<Args>(args)...);
        return promise;
    }

    template<
        typename Iterator,
        typename ValueType
            = typename detail::RemovePromise<
                  typename std::iterator_traits<Iterator>::value_type
              >::Type,
        typename Results
            = typename std::conditional<
                        std::is_same<void, ValueType>::value,
                    void, std::vector<ValueType>
              >::type
    >
    Promise<Results> whenAll(Iterator first, Iterator last) {
        /* @Security, assert that last >= first */

        return Promise<Results>([=](Resolver& resolve, Rejection& rejection) {

            Impl::WhenAllRange<ValueType, Results> impl(
                std::move(resolve), std::move(rejection));

            impl(first, last);

        });
    }

} // namespace Async
