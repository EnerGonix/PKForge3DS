#pragma once

#include "sav/Item.hpp"

// A bag slot for saves PKSM-Core does not know (Colosseum/XD, the ROM hacks): a national item number and a
// count. The save turns it into its own numbering when it is written back.
class ItemPlain : public pksm::Item
{
public:
    ItemPlain(u16 id = 0, u16 count = 0) : itemId(id), itemCount(count) {}
    [[nodiscard]] pksm::Generation generation(void) const override { return pksm::Generation::THREE; }
    [[nodiscard]] u16 maxCount(void) const override { return 999; }
    [[nodiscard]] u16 id(void) const override { return itemId; }
    [[nodiscard]] u16 count(void) const override { return itemCount; }
    [[nodiscard]] SmallVector<u8, 16> bytes(void) const override
    {
        std::array<u8, 4> b = {u8(itemId), u8(itemId >> 8), u8(itemCount), u8(itemCount >> 8)};
        return {std::span(b)};
    }
    void id(u16 v) override { itemId = v; }
    void count(u16 v) override { itemCount = v; }

private:
    u16 itemId, itemCount;
};
