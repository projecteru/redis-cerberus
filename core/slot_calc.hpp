#ifndef __CERBERUS_SLOT_CALCULATOR_HPP__
#define __CERBERUS_SLOT_CALCULATOR_HPP__

#include "common.hpp"

#include <functional>

namespace cerb {

    class KeySlotCalc {
        static void _direct_byte(KeySlotCalc& sc, byte next_byte);
        static void _between_braces(KeySlotCalc& sc, byte next_byte);
        static void _whole_key(KeySlotCalc& sc, byte next_byte);
        static void _ignore(KeySlotCalc&, byte) {}

        slot _key_slot;
        slot _key_slot_after_brace;
        std::function<void(KeySlotCalc&, byte)> _next_byte;
        bool _matched_close_brace;
        bool _last_byte_is_open_brace;
    public:
        KeySlotCalc();
        KeySlotCalc(KeySlotCalc const&) = delete;
        KeySlotCalc(KeySlotCalc&& rhs)
            : _key_slot(rhs._key_slot)
            , _key_slot_after_brace(rhs._key_slot_after_brace)
            , _next_byte(std::move(rhs._next_byte))
            , _matched_close_brace(rhs._matched_close_brace)
            , _last_byte_is_open_brace(rhs._last_byte_is_open_brace)
        {}

        void reset();
        slot get_slot() const;

        void next_byte(byte b)
        {
            _next_byte(*this, b);
        }
    };

}

#endif /* __CERBERUS_SLOT_CALCULATOR_HPP__ */
