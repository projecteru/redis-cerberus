#ifndef __CERBERUS_UTILITY_OBJECT_POOL_HPP__
#define __CERBERUS_UTILITY_OBJECT_POOL_HPP__

#include <memory>
#include <deque>

#include "common.hpp"

namespace util {

    template <typename T, cerb::msize_t chunk_size=32>
    class ObjectPool {
        typedef cerb::msize_t msize_t;

        msize_t _alloc_size;
        msize_t _using_size;

        static msize_t const obj_size = sizeof(T);

        typedef std::allocator<T> AllocType;
        AllocType _alloc;
        std::deque<T*> _pool;
        std::deque<T*> _chunk_heads;
    public:
        typedef typename AllocType::pointer pointer;
        typedef typename AllocType::reference reference;

        ObjectPool()
            : _alloc_size(0)
            , _using_size(0)
        {}

        ObjectPool(ObjectPool const&) = delete;

        msize_t alloc_size() const
        {
            return _alloc_size;
        }

        msize_t using_size() const
        {
            return _using_size;
        }

        template <typename... Params>
        pointer create(Params... params)
        {
            pointer p = _one();
            _using_size += obj_size;
            _alloc.construct(p, params...);
            return p;
        }

        void destroy(pointer p)
        {
            _alloc.destroy(p);
            _using_size -= obj_size;
            _pool.push_back(p);
        }

        ~ObjectPool()
        {
            for(auto h: _chunk_heads) {
                _alloc.deallocate(h, chunk_size);
            }
        }
    private:
        pointer _one()
        {
            if (_pool.empty()) {
                pointer p = _alloc.allocate(chunk_size);
                _chunk_heads.push_back(p);
                _alloc_size += chunk_size * obj_size;
                for (msize_t i = 1; i < chunk_size; ++i) {
                    _pool.push_back(p + i);
                }
                return p;
            }
            pointer p = _pool.front();
            _pool.pop_front();
            return p;
        }
    };

}

#endif /* __CERBERUS_UTILITY_OBJECT_POOL_HPP__ */
