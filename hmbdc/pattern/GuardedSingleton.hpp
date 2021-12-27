#include "hmbdc/Copyright.hpp"
#pragma once 

#include "hmbdc/Exception.hpp"
#include <stdexcept>
#include <type_traits>
#include <typeinfo>

namespace hmbdc { namespace pattern {
/**
 * @class SingletonGuardian<>
 * @brief RAII representing the lifespan of the underlying Singleton 
 * which also ganrantees the singularity of underlying Singleton
 * @details when the SingletonGuardian is constructored, the underlying Singleton is created;
 * when the SingletonGuardian goes out of scope the dtor of the Singleton is called.
 * 
 * @tparam Singleton the underlying type, which needs to be derived from GuardedSingleton
 */
template <typename Singleton>
struct SingletonGuardian {
    template <typename...Args>
    SingletonGuardian(Args&&...);

    SingletonGuardian(SingletonGuardian const&) = delete;
    SingletonGuardian& operator = (SingletonGuardian const&) = delete;
    virtual ~SingletonGuardian();
};

/**
 * @class SingletonPlacementGuardian<>
 * @brief similar to SingletonGuardian, but supports placement new of the underlying Singleton 
 * @details when the SingletonPlacementGuardian is constructored with an address, 
 * the underlying Singleton is created using placement new;
 * when the SingletonGuardian goes out of scope the dtor of the Singleton is called properly.
 * 
 * @tparam Singleton the underlying type, which needs to be derived from GuardedSingleton
 */
template <typename Singleton>
struct SingletonPlacementGuardian {
    template <typename...Args>
    SingletonPlacementGuardian(void* address, Args&&...);

    SingletonPlacementGuardian(void* address);

    SingletonPlacementGuardian(SingletonPlacementGuardian const&) = delete;
    SingletonPlacementGuardian& operator = (SingletonPlacementGuardian const&) = delete;
    virtual ~SingletonPlacementGuardian();
};

/**
 * @class GuardedSingleton<>
 * @brief base for the Singleton that works with SingletonGuardian 
 * @details a good practice is to declare the ctor of the derived class private
 * and friend the derived with the SingletonGuardian<derived>
 * 
 * @tparam Singleton the derived type
 */
template<typename Singleton>
struct GuardedSingleton {
    friend struct SingletonGuardian<Singleton>;
    friend struct SingletonPlacementGuardian<Singleton>;
    static Singleton& instance() {return *pInstance_s;}
    static bool initialized() {return pInstance_s;}
    using element_type = Singleton;

    GuardedSingleton(GuardedSingleton const&) = delete;
    GuardedSingleton& operator = (GuardedSingleton const&) = delete;

protected:
    GuardedSingleton() = default;

private:
    static Singleton* pInstance_s;
};

template <typename Singleton> Singleton*
    GuardedSingleton<Singleton>::pInstance_s = nullptr;

template <typename Singleton>
template <typename...Args>
SingletonGuardian<Singleton>::
SingletonGuardian(Args&&...args) {
    if (GuardedSingleton<Singleton>::pInstance_s) {
        HMBDC_THROW(std::runtime_error
            , "Cannot reinitialize typeid=" << typeid(Singleton).name());
    }
    GuardedSingleton<Singleton>::pInstance_s 
        = new Singleton(std::forward<Args>(args)...);
}

template <typename Singleton>
SingletonGuardian<Singleton>::
~SingletonGuardian() {
    delete GuardedSingleton<Singleton>::pInstance_s;
    GuardedSingleton<Singleton>::pInstance_s = nullptr;
}

template <typename Singleton>
SingletonPlacementGuardian<Singleton>::
SingletonPlacementGuardian(void* addr) {
    if (GuardedSingleton<Singleton>::pInstance_s) {
        HMBDC_THROW(std::runtime_error
            , "Cannot reinitialize typeid=" << typeid(Singleton).name());
    }
    GuardedSingleton<Singleton>::pInstance_s 
        = new (addr) Singleton;
}

template <typename Singleton>
template <typename...Args>
SingletonPlacementGuardian<Singleton>::
SingletonPlacementGuardian(void* addr, Args&&...args) {
    if (GuardedSingleton<Singleton>::pInstance_s) {
        HMBDC_THROW(std::runtime_error
            , "Cannot reinitialize typeid=" << typeid(Singleton).name());
    }
    GuardedSingleton<Singleton>::pInstance_s 
        = new (addr) Singleton{std::forward<Args>(args)...};
}

template <typename Singleton>
SingletonPlacementGuardian<Singleton>::
~SingletonPlacementGuardian() {
    GuardedSingleton<Singleton>::pInstance_s->~Singleton();
    // ::operator delete GuardedSingleton<Singleton>::pInstance_s;
    GuardedSingleton<Singleton>::pInstance_s = nullptr;
}

}}
