#include "pkdata.hpp"
#include "romhack.hpp"

#include "enums/Ability.hpp"
#include "enums/Ball.hpp"
#include "enums/GameVersion.hpp"
#include "enums/Move.hpp"
#include "enums/Nature.hpp"
#include "enums/Species.hpp"
#include "enums/Type.hpp"
#include "pkx/PA8.hpp"
#include "pkx/PA9.hpp"
#include "pkx/PB7.hpp"
#include "pkx/PK1.hpp"
#include "pkx/PK2.hpp"
#include "pkx/PK3.hpp"
#include "pkx/PK4.hpp"
#include "pkx/PK5.hpp"
#include "pkx/PK6.hpp"
#include "pkx/PK7.hpp"
#include "pkx/PK8.hpp"
#include "pkx/PK9.hpp"
#include "utils/i18n.hpp"

#include <cctype>
#include <vector>

namespace pkdata
{
    pksm::Language lang = pksm::Language::ENG;

    const std::string& species(int id) { return i18n::species(lang, pksm::Species{u16(id)}); }
    const std::string& move(int id) { return i18n::move(lang, pksm::Move{u16(id)}); }
    const std::string& item(int id) { return i18n::item(lang, u16(id)); }
    const std::string& ability(int id) { return i18n::ability(lang, pksm::Ability{u16(id)}); }
    const std::string& nature(int id) { return i18n::nature(lang, pksm::Nature{u8(id)}); }
    const std::string& ball(int id) { return i18n::ball(lang, pksm::Ball{u8(id)}); }
    const std::string& type(int id) { return i18n::type(lang, pksm::Type{u8(id)}); }

    std::string form(u8 version, int sp, int f)
    {
        return i18n::form(lang, pksm::GameVersion{version}, pksm::Species{u16(sp)}, u8(f));
    }

    std::string game(u8 version)
    {
        using V = pksm::GameVersion;
        switch (V::EnumType(version))
        {
            case V::EnumType::FR:
            case V::EnumType::LG:
                return "FireRed / LeafGreen";
            case V::EnumType::S:
            case V::EnumType::R:
                return "Ruby / Sapphire";
            case V::EnumType::D:
            case V::EnumType::P:
                return "Diamond / Pearl";
            case V::EnumType::HG:
            case V::EnumType::SS:
                return "HeartGold / SoulSilver";
            case V::EnumType::B:
            case V::EnumType::W:
                return "Black / White";
            case V::EnumType::B2:
            case V::EnumType::W2:
                return "Black 2 / White 2";
            case V::EnumType::RD:
            case V::EnumType::GN:
            case V::EnumType::BU:
                return "Red / Blue";
            case V::EnumType::GD:
            case V::EnumType::SV:
                return "Gold / Silver";
            case V::EnumType::CXD:
                return "Colosseum / XD";
            default:
                return i18n::game(lang, V{version});
        }
    }

    std::string location(pksm::Generation gen, int id) { return i18n::location(lang, gen, u16(id)); }

    std::string language(pksm::Language l)
    {
        switch (l)
        {
            case pksm::Language::JPN: return "Japanese";
            case pksm::Language::ENG: return "English";
            case pksm::Language::FRE: return "French";
            case pksm::Language::ITA: return "Italian";
            case pksm::Language::GER: return "German";
            case pksm::Language::SPA: return "Spanish";
            case pksm::Language::KOR: return "Korean";
            case pksm::Language::CHS: return "Chinese (S)";
            case pksm::Language::CHT: return "Chinese (T)";
            default: return "-";
        }
    }

    bool inScope(int gen, int version) { return gen >= 1 && gen <= 7 && version != 42 && version != 43; }

    int genNumber(pksm::Generation g)
    {
        switch (g)
        {
            case pksm::Generation::ONE: return 1;
            case pksm::Generation::TWO: return 2;
            case pksm::Generation::THREE: return 3;
            case pksm::Generation::FOUR: return 4;
            case pksm::Generation::FIVE: return 5;
            case pksm::Generation::SIX: return 6;
            case pksm::Generation::SEVEN: return 7;
            case pksm::Generation::LGPE: return 7;
            case pksm::Generation::EIGHT: return 8;
            case pksm::Generation::NINE: return 9;
            default: return 0;
        }
    }

    std::string format(const pksm::PKX& pk)
    {
        if (romhack::is(pk))
        {
            return romhack::bankFormat(pk);
        }
        std::string ext(pk.extension());
        std::string out;
        for (char c : ext)
        {
            if (c != '.')
            {
                out += toupper((unsigned char)c);
            }
        }
        return out;
    }

    const char* gameArt(u8 version)
    {
        using E = pksm::GameVersion::EnumType;
        switch (E(version))
        {
            case E::S:
            case E::R: return "ruby---sapphire";
            case E::E: return "emerald";
            case E::FR:
            case E::LG: return "firered---leafgreen";
            case E::CXD: return "colosseum";
            case E::D: return "diamond";
            case E::P: return "pearl";
            case E::Pt: return "platinum";
            case E::HG: return "heartgold";
            case E::SS: return "soulsilver";
            case E::B: return "black";
            case E::W: return "white";
            case E::B2: return "black-2";
            case E::W2: return "white-2";
            case E::X: return "x";
            case E::Y: return "y";
            case E::AS: return "alpha-sapphire";
            case E::OR: return "omega-ruby";
            case E::SN: return "sun";
            case E::MN: return "moon";
            case E::US: return "ultra-sun";
            case E::UM: return "ultra-moon";
            case E::RD: return "red";
            case E::GN: return "blue";
            case E::BU: return "blue--jp-";
            case E::YW: return "yellow";
            case E::GD: return "gold";
            case E::SV: return "silver";
            case E::C: return "crystal";
            case E::GP: return "let-s-go-pikachu";
            case E::GE: return "let-s-go-eevee";
            case E::SW: return "sword";
            case E::SH: return "shield";
            case E::PLA: return "legends-arceus";
            case E::SL: return "scarlet";
            case E::VL: return "violet";
            case E::ZA: return "legends-z-a";
            default: return "";
        }
    }

    void natureEffect(int nature, int& up, int& down)
    {
        // Natures run Hardy..Quirky: up = n / 5, down = n % 5 over Atk, Def, Spe, SpA, SpD.
        up   = nature / 5;
        down = nature % 5;
        if (up == down || nature < 0 || nature >= 25)
        {
            up = down = -1;
        }
    }

    std::unique_ptr<pksm::PKX> fromBytes(const std::string& format, const u8* data, size_t size)
    {
        if (format == "PK3RR" || format == "PK3UB" || format == "PK3GSC")
        {
            return romhack::fromBank(format, std::vector<u8>(data, data + size));
        }
        std::vector<u8> copy(data, data + size);
        u8* d = copy.data();
        std::unique_ptr<pksm::PKX> pk;
        if (format == "PK1")
        {
            pk = pksm::PKX::getPKM<pksm::Generation::ONE>(d, size);
        }
        else if (format == "PK2")
        {
            pk = pksm::PKX::getPKM<pksm::Generation::TWO>(d, size);
        }
        else if (format == "PK3")
        {
            pk = pksm::PKX::getPKM<pksm::Generation::THREE>(d, size);
        }
        else if (format == "PK4")
        {
            pk = pksm::PKX::getPKM<pksm::Generation::FOUR>(d, size);
        }
        else if (format == "PK5")
        {
            pk = pksm::PKX::getPKM<pksm::Generation::FIVE>(d, size);
        }
        else if (format == "PK6")
        {
            pk = pksm::PKX::getPKM<pksm::Generation::SIX>(d, size);
        }
        else if (format == "PK7")
        {
            pk = pksm::PKX::getPKM<pksm::Generation::SEVEN>(d, size);
        }
        else if (format == "PB7")
        {
            pk = pksm::PKX::getPKM<pksm::Generation::LGPE>(d, size);
        }
        else if (format == "PK8")
        {
            pk = pksm::PKX::getPKM<pksm::Generation::EIGHT>(d, size);
        }
        else if (format == "PA8")
        {
            pk = pksm::PKX::getPKM<pksm::PA8>(d, size);
        }
        else if (format == "PK9")
        {
            pk = pksm::PKX::getPKM<pksm::Generation::NINE>(d, size);
        }
        else if (format == "PA9")
        {
            pk = pksm::PKX::getPKM<pksm::PA9>(d, size);
        }
        // getPKM copies the bytes unless asked for direct access, so the buffer may go.
        return pk;
    }

    std::string formatFromExtension(std::string ext)
    {
        if (!ext.empty() && ext[0] == '.')
        {
            ext.erase(0, 1);
        }
        for (auto& c : ext)
        {
            c = toupper((unsigned char)c);
        }
        static const char* known[] = {"PK1", "PK2", "PK3", "PK4", "PK5", "PK6", "PK7", "PB7", "PK8", "PA8", "PK9", "PA9",
            "PK3RR", "PK3UB", "PK3GSC"};
        for (auto k : known)
        {
            if (ext == k)
            {
                return ext;
            }
        }
        return "";
    }
}
