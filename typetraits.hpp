#ifndef __TYPE_TRAITS_HPP__
#define __TYPE_TRAITS_HPP__

namespace tp {

    struct __this_platform_does_not_support_such_a_type__ {
        typedef void type;
    };

    struct c_char {
        typedef char type;
        typedef unsigned char utype;
        typedef __this_platform_does_not_support_such_a_type__ next;
    };

    struct c_short {
        typedef short type;
        typedef short unsigned utype;
        typedef c_char next;
    };

    struct c_int {
        typedef int type;
        typedef unsigned int utype;
        typedef c_short next;
    };

    struct c_long {
        typedef long type;
        typedef unsigned long utype;
        typedef c_int next;
    };

    struct c_long_long {
        typedef long long type;
        typedef unsigned long long utype;
        typedef c_long next;
    };

    template <typename TypeNode, int SizeOf, bool SizeMatched>
    struct type_finder;

    template <typename TypeNode, int SizeOf>
    struct type_finder<TypeNode, SizeOf, true>
    {
        typedef typename TypeNode::type type;
        typedef typename TypeNode::utype utype;
    };

    template <typename TypeNode, int SizeOf>
    struct type_finder<TypeNode, SizeOf, false>
    {
        typedef typename type_finder<
            typename TypeNode::next
          , SizeOf
          , SizeOf == sizeof(typename TypeNode::next::type)>::type type;
        typedef typename type_finder<
            typename TypeNode::next
          , SizeOf
          , SizeOf == sizeof(typename TypeNode::next::utype)>::utype utype;
    };

    template <int SizeOf>
    class Int {
        struct __head__ {
            typedef c_long_long next;
        };
    public:
        typedef typename type_finder<__head__, SizeOf, false>::type type;

        operator type&()
        {
            return value;
        }

        operator type const&() const
        {
            return value;
        }

        Int(type init_value=0)
            : value(init_value)
        {}
    private:
        type value;
    };

    template <int SizeOf>
    class UInt {
        struct __head__ {
            typedef c_long_long next;
        };
    public:
        typedef typename type_finder<__head__, SizeOf, false>::utype type;

        operator type&()
        {
            return value;
        }

        operator type const&() const
        {
            return value;
        }

        UInt(type init_value=0)
            : value(init_value)
        {}
    private:
        type value;
    };

}

#endif /* __TYPE_TRAITS_HPP__ */
