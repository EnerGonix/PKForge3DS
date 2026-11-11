#include "mysterygift.hpp"
#include "files.hpp"

#include "utils/i18n.hpp"
#include "wcx/PCD.hpp"
#include "wcx/PGF.hpp"
#include "wcx/WC6.hpp"
#include "wcx/WC7.hpp"

#include <set>

namespace mysterygift
{
    namespace
    {
        struct Kind
        {
            const char* file;
            int size;
        };

        // The card files a game takes, the full ones (with their texts) first. Generations 4 to 7: PKForge 3DS
        // goes up to Ultra Sun and Ultra Moon.
        std::vector<Kind> kindsFor(const pksm::Sav& sav)
        {
            switch (sav.generation())
            {
                case pksm::Generation::FOUR: return {{"wc4", 856}};
                case pksm::Generation::FIVE: return {{"pgf", 204}};
                case pksm::Generation::SIX: return {{"wc6full", 784}, {"wc6", 264}};
                case pksm::Generation::SEVEN: return {{"wc7full", 784}, {"wc7", 264}};
                default: return {};
            }
        }

        int sizeOf(const std::string& file)
        {
            static const std::pair<const char*, int> sizes[] = {{"wc4", 856}, {"pgf", 204}, {"wc6full", 784},
                {"wc6", 264}, {"wc7full", 784}, {"wc7", 264}};
            for (auto& [name, size] : sizes)
            {
                if (file == name)
                {
                    return size;
                }
            }
            return 0;
        }

        std::unique_ptr<pksm::WCX> make(const std::string& file, u8* data)
        {
            if (file == "wc4")
            {
                return std::make_unique<pksm::PCD>(data);
            }
            if (file == "pgf")
            {
                return std::make_unique<pksm::PGF>(data);
            }
            bool full = file.size() > 4 && file.compare(file.size() - 4, 4, "full") == 0;
            if (file.rfind("wc6", 0) == 0)
            {
                return std::make_unique<pksm::WC6>(data, full);
            }
            if (file.rfind("wc7", 0) == 0)
            {
                return std::make_unique<pksm::WC7>(data, full);
            }
            return nullptr;
        }
    }

    std::vector<Gift> forSave(const pksm::Sav& sav)
    {
        std::vector<Gift> gifts;
        std::set<std::pair<u16, std::string>> seen; // the short files repeat cards the full ones have
        for (const Kind& kind : kindsFor(sav))
        {
            std::vector<u8> data;
            if (!files::read(std::string("romfs:/mg/") + kind.file + ".bin", data))
            {
                continue;
            }
            for (int i = 0; (i + 1) * kind.size <= int(data.size()); i++)
            {
                std::vector<u8> card(data.begin() + i * kind.size, data.begin() + (i + 1) * kind.size);
                auto wc = make(kind.file, card.data());
                if (!wc)
                {
                    continue;
                }
                std::string title = wc->title();
                // Blank cards (no ID, no title, nothing inside) are padding in the database.
                if ((wc->ID() == 0 && title.empty()) || (!wc->pokemon() && !wc->item() && title.empty()))
                {
                    continue;
                }
                if (!seen.insert({wc->ID(), title}).second)
                {
                    continue;
                }
                Gift g;
                g.file  = kind.file;
                g.index = i;
                g.title = title.empty() ? "Event #" + std::to_string(wc->ID()) : title;
                if (wc->pokemon())
                {
                    g.detail = i18n::species(sav.language(), wc->species()) + "  Lv. " + std::to_string(wc->level()) +
                               (wc->shiny() ? "  shiny" : "") + (wc->egg() ? "  egg" : "");
                }
                else if (wc->item())
                {
                    g.detail = "Item: " + i18n::item(sav.language(), wc->object());
                }
                g.detail += "   #" + std::to_string(wc->ID());
                gifts.push_back(std::move(g));
            }
        }
        return gifts;
    }

    std::unique_ptr<pksm::WCX> load(const Gift& gift)
    {
        int size = sizeOf(gift.file);
        std::vector<u8> data;
        if (!size || !files::read("romfs:/mg/" + gift.file + ".bin", data) || (gift.index + 1) * size > int(data.size()))
        {
            return nullptr;
        }
        std::vector<u8> card(data.begin() + gift.index * size, data.begin() + (gift.index + 1) * size);
        return make(gift.file, card.data());
    }
}
