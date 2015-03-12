#include <gtest/gtest.h>
#include <fstream>

#include "core/slot_calc.hpp"

using namespace cerb;

void calc_slot_for(KeySlotCalc& c, std::string const& s) 
{
    for (auto b: s) {
        c.next_byte(byte(b));
    }
}

TEST(SlotCalc, CoverEachSlot)
{
    std::ifstream f("test/asset/each-key-in-slots.txt", std::ifstream::in);
    ASSERT_TRUE(f.good());
    KeySlotCalc slot_calc;
    for (slot s = 0; s < 16384; ++s) {
        std::string key;
        f >> key;
        calc_slot_for(slot_calc, key);
        ASSERT_EQ(s, slot_calc.get_slot());
        slot_calc.reset();
    }
}

TEST(SlotCalc, KeyWithMatchedBraces)
{
    slot slot_a = 15495; /* slot for "a" */

    KeySlotCalc slot_calc;
    calc_slot_for(slot_calc, "{a}");
    ASSERT_EQ(slot_a, slot_calc.get_slot());

    slot_calc.reset();
    calc_slot_for(slot_calc, "{a}b");
    ASSERT_EQ(slot_a, slot_calc.get_slot());

    slot_calc.reset();
    calc_slot_for(slot_calc, "b{a}");
    ASSERT_EQ(slot_a, slot_calc.get_slot());

    slot_calc.reset();
    calc_slot_for(slot_calc, "b{a}b");
    ASSERT_EQ(slot_a, slot_calc.get_slot());

    slot_calc.reset();
    calc_slot_for(slot_calc, "{a}{");
    ASSERT_EQ(slot_a, slot_calc.get_slot());

    slot_calc.reset();
    calc_slot_for(slot_calc, "}{a}");
    ASSERT_EQ(slot_a, slot_calc.get_slot());

    slot_calc.reset();
    calc_slot_for(slot_calc, "}{a}{");
    ASSERT_EQ(slot_a, slot_calc.get_slot());

    slot_calc.reset();
    calc_slot_for(slot_calc, "{a}{b}");
    ASSERT_EQ(slot_a, slot_calc.get_slot());

    slot_calc.reset();
    calc_slot_for(slot_calc, "{a}{}");
    ASSERT_EQ(slot_a, slot_calc.get_slot());

    slot slot_ao = 14311; /* slot for "a{" */

    slot_calc.reset();
    calc_slot_for(slot_calc, "{a{}");
    ASSERT_EQ(slot_ao, slot_calc.get_slot());

    slot_calc.reset();
    calc_slot_for(slot_calc, "}{a{}");
    ASSERT_EQ(slot_ao, slot_calc.get_slot());

    slot_calc.reset();
    calc_slot_for(slot_calc, "}{a{}{");
    ASSERT_EQ(slot_ao, slot_calc.get_slot());

    slot slot_o = 4092; /* slot for "{" */

    slot_calc.reset();
    calc_slot_for(slot_calc, "{{}");
    ASSERT_EQ(slot_o, slot_calc.get_slot());

    slot_calc.reset();
    calc_slot_for(slot_calc, "{");
    ASSERT_EQ(slot_o, slot_calc.get_slot());
}

TEST(SlotCalc, KeyWithUnmatchedBraces)
{
    slot slot_oa = 10276; /* slot for "{a" */

    KeySlotCalc slot_calc;
    calc_slot_for(slot_calc, "{a");
    ASSERT_EQ(slot_oa, slot_calc.get_slot());

    slot slot_coa = 12925; /* slot for "}{a" */

    slot_calc.reset();
    calc_slot_for(slot_calc, "}{a");
    ASSERT_EQ(slot_coa, slot_calc.get_slot());
}

TEST(SlotCalc, Special)
{
    slot slot_oc = 15257; /* slot for "{}" */

    KeySlotCalc slot_calc;
    calc_slot_for(slot_calc, "{}");
    ASSERT_EQ(slot_oc, slot_calc.get_slot());

    slot slot_ocoac = 13650; /* slot for "{}{a}" */
    slot_calc.reset();
    calc_slot_for(slot_calc, "{}{a}");
    ASSERT_EQ(slot_ocoac, slot_calc.get_slot());
}
