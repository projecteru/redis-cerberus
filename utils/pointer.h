#ifndef __STEKIN_UTILITY_POINTER_H__
#define __STEKIN_UTILITY_POINTER_H__

#include <memory>
#include <string>

namespace util {

    struct id {
        explicit id(void const* i)
            : _id(i)
        {}

        std::string str() const;

        bool operator<(id const& rhs) const;
        bool operator==(id const& rhs) const;
        bool operator!=(id const& rhs) const;
    private:
        void const* const _id;
    };

    template <typename RawType>
    struct sref {
        typedef RawType value_type;
        typedef typename std::unique_ptr<RawType>::pointer pointer;

        explicit sref(pointer ptr)
            : _ptr(ptr)
        {}

        template <typename ConvertableType>
        sref(sref<ConvertableType> rhs)
            : _ptr(rhs.template convert<RawType>()._ptr)
        {}

        template <typename ConvertableType>
        sref operator=(sref<ConvertableType> rhs)
        {
            _ptr = rhs.template convert<RawType>()._ptr;
            return *this;
        }

        template <typename TargetType>
        sref<TargetType> convert() const
        {
            return sref<TargetType>(_ptr);
        }

        bool operator==(sref rhs) const
        {
            return *_ptr == *rhs._ptr;
        }

        bool operator!=(sref rhs) const
        {
            return *_ptr != *rhs._ptr;
        }

        bool operator<(sref rhs) const
        {
            return *_ptr < *rhs._ptr;
        }

        bool nul() const
        {
            return nullptr == _ptr;
        }

        bool not_nul() const
        {
            return !nul();
        }

        pointer operator->() const
        {
            return _ptr;
        }

        util::id id() const
        {
            return util::id(_ptr);
        }

        RawType cp() const
        {
            return *_ptr;
        }
    private:
        pointer _ptr;

        explicit sref(int) = delete;
    };

    template <typename RawType>
    struct sptr
        : std::unique_ptr<RawType>
    {
        typedef RawType value_type;
        typedef std::unique_ptr<RawType> base_type;
        typedef typename base_type::pointer pointer;
        typedef typename base_type::deleter_type deleter_type;

        explicit sptr(pointer p)
            : base_type(p)
        {}

        template <typename ConvertableType>
        sptr(sptr<ConvertableType>&& rhs)
            : base_type(std::move(rhs))
        {}

        template <typename ConvertableType>
        sptr& operator=(sptr<ConvertableType>&& rhs)
        {
            base_type::operator=(std::move(rhs));
            return *this;
        }

        sref<RawType> operator*() const
        {
            return sref<RawType>(base_type::get());
        }

        util::id id() const
        {
            return util::id(base_type::get());
        }

        std::string str() const
        {
            return id().str();
        }

        RawType cp() const
        {
            return *base_type::get();
        }

        bool nul() const
        {
            return nullptr == base_type::get();
        }

        bool not_nul() const
        {
            return !nul();
        }

        explicit sptr(int) = delete;
        pointer get() const = delete;
        explicit operator bool() const = delete;
    };

    template <typename RawType>
    sptr<RawType> mkptr(RawType* ptr)
    {
        return sptr<RawType>(ptr);
    }

    template <typename RawType>
    sref<RawType> mkref(RawType& obj)
    {
        return sref<RawType>(&obj);
    }

}

#endif /* __STEKIN_UTILITY_POINTER_H__ */
