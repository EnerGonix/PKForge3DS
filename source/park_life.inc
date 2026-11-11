// The rest of the Android PKForge's ParkLife (src/PKForge.Domain/ParkLife.*.cs, ParkCare, ParkGoals, ParkAbsence,
// ParkWilds, ParkTalk), included inside the Park screen: what each resident is (types, flyer, ghost, legend,
// troublemaker), wild visitors by place and time, evolution, the park's rhythms, Snorlax, the troublemakers, the
// famous things Pokémon do, needs and bond kept between visits, illness, games, goals, the chronicle, treasures,
// the daycare, the Wilds, the album and the credits statue. Lines are this port's own, after the original's.

        // ---------------------------------------------------------------- what a species is
        static bool hasType(int species, pksm::Type t)
        {
            return pksm::PersonalSV::type1(u16(species)) == t || pksm::PersonalSV::type2(u16(species)) == t;
        }
        static bool isIn(int species, std::initializer_list<int> list)
        {
            for (int s : list)
            {
                if (s == species)
                {
                    return true;
                }
            }
            return false;
        }
        // ParkLife.Flight: the true flyers (not the insects that flutter, nor Doduo and Dodrio).
        static bool flyerOf(int s) { return hasType(s, pksm::Type::Flying) && !hasType(s, pksm::Type::Bug) && !isIn(s, {84, 85}); }
        static bool ghostOf(int s) { return hasType(s, pksm::Type::Ghost); }
        static bool teleporterOf(int s) { return isIn(s, {63, 64, 65, 280, 281, 282, 475, 177, 178, 677, 678, 765}); }
        // ParkLife.Respect: legends, mythicals, pseudo-legendaries and the few with a presence.
        static bool legendOf(int s)
        {
            return isIn(s, {144, 145, 146, 150, 151, 243, 244, 245, 249, 250, 251, 377, 378, 379, 380, 381, 382, 383, 384,
                               385, 386, 480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 638,
                               639, 640, 641, 642, 643, 644, 645, 646, 647, 648, 649, 716, 717, 718, 719, 720, 721, 785,
                               786, 787, 788, 789, 790, 791, 792, 800, 801, 802, 807, 149, 248, 373, 376, 445, 635, 706,
                               784, 130, 6, 208, 248, 887, 998});
        }
        // ParkLife.Trolls: the troublemakers.
        static bool trollOf(int s) { return isIn(s, {570, 571, 215, 461, 198, 430, 52, 53, 56, 302, 509, 510, 859, 860, 861, 552, 553}); }
        static bool swimmerOf(int s) { return hasType(s, pksm::Type::Water) || flyerOf(s); }

        // ---------------------------------------------------------------- care: hunger and bond kept between visits
        static constexpr const char* CARE = "sdmc:/3ds/PKForge/park-care.txt";
        static constexpr const char* CHRONICLE = "sdmc:/3ds/PKForge/park-chronicle.txt";
        static constexpr const char* WILDS = "sdmc:/3ds/PKForge/wilds.txt";
        static constexpr const char* TREASURES = "sdmc:/3ds/PKForge/park-treasures.txt";
        static constexpr const char* DAYCARE = "sdmc:/3ds/PKForge/park-daycare.txt";

        std::map<std::string, int> chronicle; // what has happened, counted (Chronicle, Goals)
        std::vector<std::string> treasures, wildsList, daycareGuests;
        long lastVisit = 0;
        int snorlax = -1, snorlaxTimer = 0; // the resident asleep across the bridge
        float bridgeX = 470, bridgeY = 1050;
        bool storm = false;
        std::vector<std::string> awayStory;
        int game = 0, gameTimer = 0, gameTarget = -1; // 1 dig, 2 brush, 3 spar
        int lastHour = -1, morningCall = -1, partyDay = -1;

        static std::string keyOf(const Resident& r) { return std::to_string(r.species) + ":" + r.name; }

        void count(const std::string& what, int by = 1) { chronicle[what] += by; }

        void loadLife()
        {
            std::string text;
            if (files::readText(CARE, text))
            {
                std::stringstream in(text);
                std::string line;
                std::getline(in, line);
                lastVisit = atol(line.c_str());
                while (std::getline(in, line))
                {
                    size_t a = line.find('\t'), b = line.find('\t', a + 1);
                    if (a == std::string::npos || b == std::string::npos)
                    {
                        continue;
                    }
                    std::string key = line.substr(0, a);
                    for (Resident& r : residents)
                    {
                        if (keyOf(r) == key)
                        {
                            r.hunger = float(atof(line.c_str() + a + 1));
                            r.bond   = float(atof(line.c_str() + b + 1));
                        }
                    }
                }
            }
            if (files::readText(CHRONICLE, text))
            {
                std::stringstream in(text);
                std::string k;
                int v;
                while (in >> k >> v)
                {
                    chronicle[k] = v;
                }
            }
            auto lines = [](const char* path)
            {
                std::vector<std::string> out;
                std::string t;
                if (files::readText(path, t))
                {
                    std::stringstream in(t);
                    std::string l;
                    while (std::getline(in, l))
                    {
                        if (!l.empty())
                        {
                            out.push_back(l);
                        }
                    }
                }
                return out;
            };
            treasures     = lines(TREASURES);
            wildsList     = lines(WILDS);
            daycareGuests = lines(DAYCARE);
            for (Resident& r : residents)
            {
                r.guest = std::find(daycareGuests.begin(), daycareGuests.end(), keyOf(r)) != daycareGuests.end();
                if (r.guest && pickTarget(r, 830, 740, 50))
                {
                    r.x = r.tx;
                    r.y = r.ty;
                }
            }
        }

        void saveLife()
        {
            std::string out = std::to_string(long(time(nullptr))) + "\n";
            for (const Resident& r : residents)
            {
                if (!r.wild)
                {
                    char line[96];
                    snprintf(line, sizeof(line), "%s\t%.1f\t%.1f\n", keyOf(r).c_str(), r.hunger, r.bond);
                    out += line;
                }
            }
            files::writeText(CARE, out);
            std::string c;
            for (auto& [k, v] : chronicle)
            {
                c += k + " " + std::to_string(v) + "\n";
            }
            files::writeText(CHRONICLE, c);
            std::string t;
            for (auto& s : treasures)
            {
                t += s + "\n";
            }
            files::writeText(TREASURES, t);
            std::string d;
            for (auto& s : daycareGuests)
            {
                d += s + "\n";
            }
            files::writeText(DAYCARE, d);
        }

        void lifeInit(Resident& r)
        {
            r.flyer      = flyerOf(r.species);
            r.ghost      = ghostOf(r.species);
            r.teleporter = teleporterOf(r.species);
            r.legend     = legendOf(r.species);
            r.troll      = trollOf(r.species);
        }

        // ParkCare's weather: a block of six hours, mostly fair, sometimes rain, now and then a storm.
        void weatherBlock()
        {
            time_t now = time(nullptr);
            u32 block  = u32(now / (6 * 3600));
            u32 h      = hashOf(block * 2654435761u);
            rainy      = h % 5 == 0 || h % 13 == 1;
            storm      = h % 13 == 1;
        }

        // ParkAbsence: what happened while the player was away, from what the park knows.
        void absence()
        {
            long now = long(time(nullptr));
            if (!lastVisit || now - lastVisit < 3600 || residents.empty())
            {
                return;
            }
            long hours = (now - lastVisit) / 3600;
            int nights = int(hours / 24) + (hours % 24 > 8 ? 1 : 0);
            awayStory.push_back("You were away " + (hours < 48 ? std::to_string(hours) + " hours" : std::to_string(hours / 24) + " days") + ".");
            if (nights)
            {
                awayStory.push_back(std::to_string(nights) + (nights == 1 ? " night" : " nights") + " passed; everyone slept under the stars.");
            }
            u32 seed = u32(lastVisit) ^ u32(now);
            const Resident& a = residents[hashOf(seed) % residents.size()];
            const Resident& b = residents[hashOf(seed + 1) % residents.size()];
            awayStory.push_back(a.name + " got hungry and went looking for berries.");
            if (&a != &b)
            {
                awayStory.push_back(b.name + " kept looking at the arch, waiting for you.");
            }
            for (const Resident& r : residents)
            {
                if (r.troll)
                {
                    awayStory.push_back(r.name + " played a trick on someone while you were gone.");
                    break;
                }
            }
            if (hashOf(seed + 7) % 2)
            {
                awayStory.push_back("A wild Pokémon came by, looked around, and left the way it came.");
            }
            // Needs fell with the time away (ParkCare: hunger empties in about two days).
            for (Resident& r : residents)
            {
                r.hunger = std::max(0.0f, r.hunger - hours * 2.0f);
            }
            for (auto& line : awayStory)
            {
                note(line);
            }
        }

        // ---------------------------------------------------------------- wild visitors (ParkLife.Visitors)
        struct WildKind
        {
            const char* area;
            bool night;
            int species;
        };
        static const std::vector<WildKind>& wildKinds()
        {
            static const std::vector<WildKind> k = {
                {"", false, 16}, {"", false, 19}, {"", false, 10}, {"", false, 161}, {"", false, 263}, {"", false, 399},
                {"", false, 396}, {"", false, 506}, {"", false, 819}, {"", false, 915}, {"", false, 12}, {"", false, 731},
                {"", true, 163}, {"", true, 41}, {"", true, 92}, {"", true, 198}, {"", true, 43}, {"", true, 261}, {"", true, 714},
                {"Ember Peak", false, 218}, {"Ember Peak", false, 322}, {"Ember Peak", false, 324}, {"Ember Peak", false, 74},
                {"Ember Peak", false, 850}, {"Ember Peak", true, 607}, {"Ember Peak", true, 218},
                {"Snowcap Summit", false, 333}, {"Snowcap Summit", false, 627}, {"Snowcap Summit", false, 225},
                {"Snowcap Summit", false, 227}, {"Snowcap Summit", true, 164}, {"Snowcap Summit", true, 215},
                {"Frostfall", false, 361}, {"Frostfall", false, 220}, {"Frostfall", false, 459}, {"Frostfall", false, 613},
                {"Frostfall", false, 712}, {"Frostfall", true, 215}, {"Frostfall", true, 361},
                {"Amber Woods", false, 273}, {"Amber Woods", false, 216}, {"Amber Woods", false, 585}, {"Amber Woods", false, 285},
                {"Amber Woods", false, 819}, {"Amber Woods", true, 163}, {"Amber Woods", true, 167}, {"Amber Woods", true, 708},
                {"Harvest Shrine", false, 280}, {"Harvest Shrine", false, 669}, {"Harvest Shrine", false, 177},
                {"Harvest Shrine", true, 200}, {"Harvest Shrine", true, 92},
                {"Blissey's Daycare", false, 440}, {"Blissey's Daycare", false, 172}, {"Blissey's Daycare", false, 173},
                {"Blissey's Daycare", false, 174}, {"Blissey's Daycare", true, 517},
                {"Splash Pool", false, 270}, {"Splash Pool", false, 183}, {"Splash Pool", false, 54}, {"Splash Pool", false, 283},
                {"Splash Pool", true, 170}, {"Splash Pool", true, 270},
                {"Tidewater Coast", false, 278}, {"Tidewater Coast", false, 98}, {"Tidewater Coast", false, 341},
                {"Tidewater Coast", false, 120}, {"Tidewater Coast", true, 120}, {"Tidewater Coast", true, 170}};
            return k;
        }
        // ParkLife.Evolution: what a visitor may become.
        static int nextStage(int s)
        {
            static const std::pair<int, int> next[] = {{16, 17}, {19, 20}, {10, 11}, {161, 162}, {263, 264}, {399, 400},
                {396, 397}, {506, 507}, {819, 820}, {915, 916}, {731, 732}, {163, 164}, {41, 42}, {92, 93}, {43, 44}, {261, 262},
                {714, 715}, {270, 271}, {194, 195}, {54, 55}, {422, 423}, {218, 219}, {74, 75}, {850, 851}, {607, 608},
                {333, 334}, {627, 628}, {361, 362}, {220, 221}, {459, 460}, {613, 614}, {712, 713}, {273, 274}, {216, 217},
                {585, 586}, {285, 286}, {167, 168}, {280, 281}, {669, 670}, {177, 178}, {172, 25}, {173, 35}, {174, 39},
                {440, 113}, {517, 518}, {183, 184}, {283, 284}, {170, 171}, {278, 279}, {98, 99}, {341, 342}, {198, 430},
                {215, 461}, {200, 429}, {708, 709}};
            for (auto& [a, b] : next)
            {
                if (a == s)
                {
                    return b;
                }
            }
            return 0;
        }

        int visitorsHere() const
        {
            int n = 0;
            for (const Resident& r : residents)
            {
                n += r.wild;
            }
            return n;
        }

        void spawnVisitor()
        {
            if (land.empty() || visitorsHere() >= 4)
            {
                return;
            }
            // Sometimes one let go to the Wilds comes back through the arch to visit (ParkWilds).
            if (!wildsList.empty() && rand() % 6 == 0)
            {
                const std::string& w = wildsList[rand() % wildsList.size()];
                size_t tab = w.find('\t');
                Resident r;
                r.species = atoi(w.c_str());
                r.name    = tab == std::string::npos ? pkdata::species(r.species) : w.substr(tab + 1);
                r.wild = r.friendBack = true;
                r.stay = 2400 + rand() % 2400;
                arrive(r, 600, 960);
                note(r.name + " came back from the Wilds to visit! Everyone gathers round.");
                count("wilds-returned");
                for (Resident& o : residents)
                {
                    if (!o.wild && o.act == Act::Rest)
                    {
                        say(o, Emote::Exclaim);
                    }
                }
                return;
            }
            // A place and the time of day decide who comes.
            auto [cx, cy] = land[rand() % land.size()];
            std::string place = areaAt(cx * 8.0f, cy * 8.0f);
            std::vector<int> pool;
            for (const WildKind& k : wildKinds())
            {
                if (k.night == night && (place == k.area || (k.area[0] == 0 && pool.empty())))
                {
                    pool.push_back(k.species);
                }
            }
            if (pool.empty())
            {
                return;
            }
            Resident r;
            r.species = pool[rand() % pool.size()];
            r.name    = "Wild " + pkdata::species(r.species);
            r.wild    = true;
            r.stay    = 1800 + rand() % 3600;
            arrive(r, cx * 8 + 4.0f, cy * 8 + 7.0f);
            note("A wild " + pkdata::species(r.species) + " wandered into " + place + ".");
            count("visitors");
            countDay("visitors");
        }

        void arrive(Resident& r, float x, float y)
        {
            r.x = r.tx = r.homeX = x;
            r.y = r.ty = r.homeY = y;
            r.pace   = 0.35f + (rand() % 30) / 100.0f;
            r.range  = 60;
            r.timer  = 60;
            r.phase  = rand() % 100;
            r.level  = 5 + rand() % 20;
            lifeInit(r);
            residents.push_back(r);
            audio::cry(r.species, 0.5f);
        }

        // ---------------------------------------------------------------- the park ticking
        Resident* someone(std::function<bool(const Resident&)> ok)
        {
            std::vector<Resident*> pool;
            for (Resident& r : residents)
            {
                if (ok(r))
                {
                    pool.push_back(&r);
                }
            }
            return pool.empty() ? nullptr : pool[rand() % pool.size()];
        }

        bool awake(const Resident& r) const { return r.act != Act::Sleep && r.act != Act::Nap && r.act != Act::Hidden && r.act != Act::Hide; }

        bool nearWater(float x, float y) const
        {
            for (int dx = -16; dx <= 16; dx += 16)
            {
                for (int dy = -16; dy <= 16; dy += 16)
                {
                    if (!isLand(int(x + dx), int(y + dy)))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        void lifeTick()
        {
            time_t now = time(nullptr);
            tm* t      = localtime(&now);
            if (tick % 600 == 0)
            {
                weatherBlock();
            }
            // Needs: hunger falls (ParkCare), the unwell drag themselves until they eat and rest (Evenings).
            if (tick % 60 == 0)
            {
                for (Resident& r : residents)
                {
                    r.hunger = std::max(0.0f, r.hunger - (r.act == Act::Sleep ? 0.05f : 0.12f));
                    if (!r.wild && !r.unwell && r.hunger < 8 && r.energy < 15)
                    {
                        r.unwell = true;
                        note(r.name + " is unwell: too long hungry and worn out.");
                        if (Resident* h = someone([](const Resident& o) { return isIn(o.species, {113, 242, 531, 440}); }))
                        {
                            pickTarget(*h, r.x, r.y, 12);
                            h->act = Act::Walk;
                            note(h->name + " comes to look after " + r.name + ".");
                        }
                    }
                    if (r.unwell && r.hunger > 40 && r.energy > 40)
                    {
                        r.unwell = false;
                        say(r, Emote::Heart);
                        note(r.name + " is feeling better.");
                    }
                    if (!r.wild && r.hunger < 20 && r.emoteAge > 400 && awake(r))
                    {
                        say(r, Emote::Sweat);
                    }
                }
            }
            // Visitors come and go.
            if (tick % 900 == 450 && rand() % 2 == 0)
            {
                spawnVisitor();
            }
            for (size_t i = 0; i < residents.size();)
            {
                Resident& r = residents[i];
                if (r.wild && --r.stay <= 0 && awake(r))
                {
                    note((r.friendBack ? r.name : "The " + r.name.substr(r.name.find(' ') + 1)) + " left the way it came.");
                    if (int(i) == selected)
                    {
                        selected = 0;
                    }
                    else if (int(i) < selected)
                    {
                        selected--;
                    }
                    if (snorlax == int(i))
                    {
                        snorlax = -1;
                    }
                    residents.erase(residents.begin() + long(i));
                    continue;
                }
                i++;
            }
            if (residents.empty())
            {
                return;
            }
            // Evolution: a visitor glows into its next form for good; a resident starts and stops (B pressed).
            if (tick % 1800 == 900)
            {
                if (Resident* v = someone([this](const Resident& o) { return o.wild && !o.friendBack && nextStage(o.species) && awake(o); }))
                {
                    int into = nextStage(v->species);
                    note("The wild " + pkdata::species(v->species) + " started to glow... it evolved into " + pkdata::species(into) + "!");
                    v->species = into;
                    v->name    = "Wild " + pkdata::species(into);
                    lifeInit(*v);
                    say(*v, Emote::Sparkle);
                    audio::sfx(audio::Sfx::Win);
                    count("evolutions");
                }
                else if (rand() % 4 == 0)
                {
                    if (Resident* r = someone([this](const Resident& o) { return !o.wild && awake(o) && o.level >= 16; }))
                    {
                        say(*r, Emote::Sparkle);
                        note(r->name + " began to glow... and stopped, the way it does when B is pressed. It is happy as it is.");
                        count("evolutions-stopped");
                    }
                }
            }
            // The hour: Hoothoot hoots on the hour (Rhythms); the morning call at 8.
            if (t->tm_hour != lastHour)
            {
                if (lastHour >= 0)
                {
                    if (Resident* h = someone([](const Resident& o) { return isIn(o.species, {163, 164}); }))
                    {
                        say(*h, Emote::Note);
                        audio::cry(h->species, 0.6f);
                        note(h->name + " hoots: it is " + std::to_string(t->tm_hour) + " o'clock.");
                    }
                }
                lastHour = t->tm_hour;
                if (t->tm_hour == 8 && morningCall != day)
                {
                    morningCall = day;
                    Resident* late = someone([](const Resident& o) { return !o.wild; });
                    for (Resident& r : residents)
                    {
                        if (!r.wild && &r != late && pickTarget(r, 500, 860, 50))
                        {
                            r.act = Act::Come;
                        }
                    }
                    note("The morning call! Everyone lines up in Sunlit Commons: three cheers" +
                         (late ? "... and " + late->name + " sleeps right through it." : std::string(".")));
                }
            }
            // Rain chorus: Politoed leads, the Poliwag line up (Rhythms).
            if (rainy && tick % 2400 == 1200)
            {
                if (Resident* p = someone([](const Resident& o) { return o.species == 186; }))
                {
                    say(*p, Emote::Note);
                    for (Resident& r : residents)
                    {
                        if (isIn(r.species, {60, 61}) && pickTarget(r, p->x, p->y + 14, 10))
                        {
                            r.act = Act::Walk;
                        }
                    }
                    note(p->name + " leads the rain chorus.");
                }
                else if (Resident* w = someone([](const Resident& o) { return hasType(o.species, pksm::Type::Water) && !o.wild; }))
                {
                    say(*w, Emote::Note);
                    note(w->name + " dances in the rain.");
                }
            }
            if (storm && tick % 1500 == 700)
            {
                if (Resident* r = someone([this](const Resident& o) { return awake(o) && !hasType(o.species, pksm::Type::Electric); }))
                {
                    say(*r, Emote::Exclaim);
                    note("Thunder! " + r->name + " jumps.");
                }
            }
            snorlaxTick();
            if (tick % 300 == 150)
            {
                trollTick();
            }
            if (tick % 240 == 120)
            {
                iconTick();
            }
            if (tick % 200 == 100)
            {
                habitTick();
            }
            respectTick();
            powersTick();
            if (tick % 3000 == 1500)
            {
                sceneTick();
            }
            gameTick();
            goalsTick();
        }

        // ParkLife.Snorlax: by day it falls asleep across the bridge; walkers stop, a queue builds, a singer wakes it.
        void snorlaxTick()
        {
            if (snorlax < 0 && !night && tick % 1200 == 600)
            {
                for (int i = 0; i < int(residents.size()); i++)
                {
                    if (residents[i].species == 143 && awake(residents[i]) && rand() % 3 == 0)
                    {
                        snorlax = i;
                        Resident& s = residents[i];
                        s.x = s.tx = bridgeX;
                        s.y = s.ty = bridgeY;
                        s.act        = Act::Nap;
                        snorlaxTimer = 1500 + rand() % 1500;
                        say(s, Emote::Sleep);
                        note(s.name + " fell asleep right across the bridge. Nobody can get past!");
                        count("snorlax");
                    }
                }
                return;
            }
            if (snorlax < 0 || snorlax >= int(residents.size()))
            {
                snorlax = -1;
                return;
            }
            Resident& s = residents[snorlax];
            s.act       = Act::Nap;
            for (Resident& r : residents)
            {
                if (&r != &s && !r.flyer && r.act == Act::Walk && std::hypot(r.x - bridgeX, r.y - bridgeY) < 30)
                {
                    r.tx = r.x;
                    r.ty = r.y;
                    r.act   = Act::Rest;
                    r.timer = 200;
                    say(r, Emote::Question);
                }
            }
            if (--snorlaxTimer <= 0)
            {
                Resident* singer = someone([](const Resident& o) { return isIn(o.species, {39, 40, 174, 648}); });
                note(singer ? singer->name + " sings; " + s.name + " yawns, stretches and finally moves off the bridge."
                            : s.name + " woke up by itself and rolled off the bridge.");
                s.act   = Act::Rest;
                s.timer = 120;
                say(s, Emote::Ellipsis);
                snorlax = -1;
            }
        }

        // ParkLife.Trolls.
        void trollTick()
        {
            Resident* t = someone([this](const Resident& o) { return o.troll && awake(o); });
            if (!t || rand() % 3)
            {
                return;
            }
            if (ball && ballAge >= BALL_FLIGHT && rand() % 2 == 0)
            {
                ball = false;
                say(*t, Emote::Note);
                note(t->name + " ran off with the ball!");
                count("pranks");
                return;
            }
            if (night)
            {
                if (Resident* v = someone([](const Resident& o) { return o.act == Act::Sleep; }))
                {
                    say(*v, Emote::Anger);
                    say(*t, Emote::Note);
                    note(t->name + " woke " + v->name + " up for fun. " + v->name + " is not amused.");
                    v->bond = std::max(0.0f, v->bond - 1);
                    count("pranks");
                }
                return;
            }
            if (Resident* v = someone([t](const Resident& o) { return &o != t && o.act == Act::Walk; }))
            {
                say(*v, Emote::Sweat);
                say(*t, Emote::Note);
                note(t->species == 570 || t->species == 571 ? t->name + " took the shape of someone else and played a trick on " + v->name + "."
                                                            : t->name + " tripped " + v->name + " and ran off laughing.");
                count("pranks");
            }
        }

        // ParkLife.Icons: the famous things Pokémon do.
        void iconTick()
        {
            Resident* r = someone([this](const Resident& o) { return awake(o) && o.act == Act::Rest && nearWater(o.x, o.y); });
            if (!r)
            {
                return;
            }
            switch (r->species)
            {
                case 131:
                    if (Resident* p = someone([r](const Resident& o) { return &o != r && !swimmerOf(o.species) && o.act == Act::Rest; }))
                    {
                        note(r->name + " ferried " + p->name + " across the water.");
                        say(*p, Emote::Heart);
                        p->x = r->x + 4;
                        p->y = r->y;
                        count("ferries");
                    }
                    break;
                case 54: note(r->name + " waded in, forgot it cannot swim, and had to be brought back."); say(*r, Emote::Sweat); break;
                case 79:
                case 80: note(r->name + " is fishing with its tail."); say(*r, Emote::Ellipsis); break;
                case 129: note(r->name + " splashed about. Nothing happened."); r->move = Move::Hop; r->timer = 60; r->act = Act::Gesture; break;
                case 270: note(r->name + " carried a little one on its leaf."); say(*r, Emote::Note); break;
                default: break;
            }
        }

        // ParkLife.Habits: what each does because of what it is.
        void habitTick()
        {
            Resident* r = someone([](const Resident& o) { return o.act == Act::Rest && !o.wild; });
            if (!r || rand() % 3)
            {
                return;
            }
            const char* what = nullptr;
            Emote e          = Emote::Note;
            if (hasType(r->species, pksm::Type::Fire) && !night)
            {
                what = " basks on the warm stones.";
                e    = Emote::Sparkle;
            }
            else if (hasType(r->species, pksm::Type::Grass) && !night && !rainy)
            {
                what = " soaks up the sun.";
            }
            else if (hasType(r->species, pksm::Type::Ground))
            {
                what = " digs a little hole and looks pleased.";
                r->move = Move::Shake;
            }
            else if (hasType(r->species, pksm::Type::Bug))
            {
                what = " climbs into the bushes.";
            }
            else if (hasType(r->species, pksm::Type::Ghost) && night)
            {
                what = " fades into the shadows.";
                r->faded = 300;
            }
            else if (hasType(r->species, pksm::Type::Water) && rainy)
            {
                what = " loves this weather.";
                e    = Emote::Heart;
            }
            else if (hasType(r->species, pksm::Type::Psychic))
            {
                what = " stares at nothing, deep in thought.";
                e    = Emote::Ellipsis;
            }
            if (what)
            {
                say(*r, e);
                r->act   = Act::Gesture;
                r->timer = 150;
                if (rand() % 4 == 0)
                {
                    note(r->name + what);
                }
            }
        }

        // ParkLife.Respect: room for the legends; the others step aside and bow, the timid run.
        void respectTick()
        {
            if (tick % 20)
            {
                return;
            }
            for (Resident& l : residents)
            {
                if (!l.legend || !awake(l) || l.act != Act::Walk)
                {
                    continue;
                }
                for (Resident& o : residents)
                {
                    if (&o == &l || o.legend || !awake(o) || o.act == Act::Race || o.act == Act::Chase)
                    {
                        continue;
                    }
                    float d = std::hypot(o.x - l.x, o.y - l.y);
                    if (d < 26 && o.cooldown == 0)
                    {
                        float ax = o.x + (o.x - l.x) / std::max(d, 1.0f) * 30, ay = o.y + (o.y - l.y) / std::max(d, 1.0f) * 30;
                        if (pickTarget(o, ax, ay, 6))
                        {
                            o.act      = Act::Walk;
                            o.move     = Move::Shake;
                            o.cooldown = 600;
                            say(o, o.level < 15 ? Emote::Sweat : Emote::Ellipsis);
                        }
                    }
                }
            }
        }

        // ParkLife.Powers: ghosts fade and reappear in a puff; teleporters blink.
        void powersTick()
        {
            for (Resident& r : residents)
            {
                if (r.faded > 0 && --r.faded == 0)
                {
                    say(r, Emote::Sparkle);
                }
                if (tick % 600 != (r.phase * 6) % 600 || !awake(r))
                {
                    continue;
                }
                if (r.ghost && rand() % 3 == 0)
                {
                    r.faded = 240 + rand() % 240;
                }
                else if (r.teleporter && rand() % 3 == 0 && !land.empty())
                {
                    auto [cx, cy] = land[rand() % land.size()];
                    r.x = r.tx = cx * 8 + 4.0f;
                    r.y = r.ty = cy * 8 + 7.0f;
                    say(r, Emote::Sparkle);
                    if (rand() % 3 == 0)
                    {
                        note(r.name + " teleported to " + std::string(areaAt(r.x, r.y)) + ".");
                    }
                }
            }
        }

        // ParkLife.Scenes: several acting something out together.
        void sceneTick()
        {
            std::vector<Resident*> eevee, magnet, dig;
            for (Resident& r : residents)
            {
                if (!awake(r))
                {
                    continue;
                }
                if (isIn(r.species, {133, 134, 135, 136, 196, 197, 470, 471, 700}))
                {
                    eevee.push_back(&r);
                }
                if (isIn(r.species, {81, 82, 462}))
                {
                    magnet.push_back(&r);
                }
                if (isIn(r.species, {50, 51}))
                {
                    dig.push_back(&r);
                }
            }
            auto gather = [this](std::vector<Resident*>& g, const std::string& line)
            {
                Resident* lead = g[0];
                for (size_t i = 1; i < g.size(); i++)
                {
                    if (pickTarget(*g[i], lead->x + float(i) * 10, lead->y, 4))
                    {
                        g[i]->act = Act::Walk;
                    }
                    say(*g[i], Emote::Note);
                }
                say(*lead, Emote::Sparkle);
                note(line);
                count("scenes");
            };
            if (eevee.size() >= 2)
            {
                gather(eevee, "The Eevee family gathered together: " + eevee[0]->name + " and " + eevee[1]->name + " compare their looks.");
            }
            else if (magnet.size() >= 2)
            {
                gather(magnet, "Magnemite click together for a moment, almost a Magneton.");
            }
            else if (dig.size() >= 1)
            {
                gather(dig, dig[0]->name + " pops up in a row of holes, bob, bob, bob.");
            }
        }

        // ---------------------------------------------------------------- the player (ParkLife.Player, Games)
        // A head pat: some cannot help zapping, singeing or licking you; wary ones step back.
        void pat(Resident& r)
        {
            if (!awake(r))
            {
                say(r, Emote::Sleep);
                return;
            }
            r.bond = std::min(100.0f, r.bond + 2);
            std::string what = r.name + " enjoys a head pat.";
            Emote e          = Emote::Heart;
            if (hasType(r.species, pksm::Type::Electric) && rand() % 3 == 0)
            {
                what = r.name + " could not help it: zap! Your hair stands on end.";
                e    = Emote::Exclaim;
            }
            else if (hasType(r.species, pksm::Type::Fire) && rand() % 3 == 0)
            {
                what = r.name + " is warm... a little too warm. Ouch!";
                e    = Emote::Sweat;
            }
            else if (isIn(r.species, {108, 463}) || (hasType(r.species, pksm::Type::Normal) && rand() % 5 == 0))
            {
                what = r.name + " licks your hand.";
            }
            else if (r.bond < 15)
            {
                what = r.name + " steps out of reach, still unsure about you.";
                e    = Emote::Question;
            }
            say(r, e);
            note(what);
            count("pats");
            countDay("pats");
        }

        void startGame(int which)
        {
            if (residents.empty())
            {
                return;
            }
            Resident& r = residents[selected];
            if (!awake(r) || r.energy < 15)
            {
                say(r, Emote::Sweat);
                talk(r, "tired");
                return;
            }
            game       = which;
            gameTarget = selected;
            gameTimer  = which == 3 ? 150 + rand() % 120 : 240;
            if (which == 1)
            {
                note(r.name + " starts digging for treasure...");
                r.move = Move::Shake;
            }
            else if (which == 2)
            {
                note("You brush " + r.name + "'s coat. It closes its eyes.");
                say(r, Emote::Heart);
                r.bond = std::min(100.0f, r.bond + 4);
                count("brushes");
                countDay("brushes");
            }
            else
            {
                note(r.name + " squares up for a spar: press A right as the strike lands!");
                say(r, Emote::Exclaim);
            }
            r.act   = Act::Gesture;
            r.timer = gameTimer + 30;
        }

        void gameTick()
        {
            if (!game || gameTarget < 0 || gameTarget >= int(residents.size()))
            {
                game = 0;
                return;
            }
            Resident& r = residents[gameTarget];
            if (--gameTimer > 0)
            {
                return;
            }
            if (game == 1)
            {
                static const char* finds[] = {"an Oran Berry", "a Pearl", "a Nugget", "a Star Piece", "an old coin",
                    "a pretty stone", "a Big Mushroom", "nothing but mud"};
                std::string f = finds[rand() % 8];
                note(r.name + " dug up " + f + "!");
                if (f != "nothing but mud")
                {
                    treasures.push_back(f);
                    count("treasures");
                    countDay("treasures");
                    say(r, Emote::Sparkle);
                }
                else
                {
                    say(r, Emote::Ellipsis);
                }
            }
            else if (game == 3)
            {
                note(r.name + " struck; you were not ready. A feint got you!");
                say(r, Emote::Note);
            }
            r.move = Move::None;
            game   = 0;
        }

        // A in a spar: blocking right as the strike lands counts; during the wind-up it is too early.
        bool sparBlock()
        {
            if (game != 3 || gameTarget < 0 || gameTarget >= int(residents.size()))
            {
                return false;
            }
            Resident& r = residents[gameTarget];
            if (gameTimer < 25)
            {
                note("Blocked! " + r.name + " is impressed.");
                say(r, Emote::Heart);
                r.bond = std::min(100.0f, r.bond + 3);
                count("spars-won");
                countDay("spars-won");
            }
            else
            {
                note("Too early: " + r.name + " feinted and got you.");
                say(r, Emote::Note);
            }
            game = 0;
            return true;
        }

        // ---------------------------------------------------------------- goals, chronicle, treasures
        // ParkGoals: three small goals a day, the same all day; all three throw a party and leave a rare find.
        struct Goal
        {
            const char* text;
            const char* counter;
            int need;
        };
        std::vector<Goal> todaysGoals() const
        {
            static const Goal all[] = {{"Throw the ball 3 times", "balls", 3}, {"Feed 3 berries", "berries", 3},
                {"Give 5 head pats", "pats", 5}, {"Brush a coat twice", "brushes", 2}, {"Take 2 photos", "photos", 2},
                {"Play 4 times", "plays", 4}, {"Dig up a treasure", "treasures", 1}, {"Win a spar", "spars-won", 1},
                {"Find everyone in hide and seek", "seeks-won", 1}, {"Meet a wild visitor", "visitors", 1}};
            std::vector<Goal> out;
            u32 h = hashOf(u32(day) * 7919u);
            for (int i = 0; out.size() < 3 && i < 30; i++)
            {
                const Goal& g = all[(h + u32(i) * 3u) % 10];
                bool dup      = false;
                for (auto& o : out)
                {
                    dup |= o.counter == g.counter;
                }
                if (!dup)
                {
                    out.push_back(g);
                }
            }
            return out;
        }
        int progress(const Goal& g) const
        {
            auto it = chronicle.find(std::string("day-") + std::to_string(day) + "-" + g.counter);
            return it == chronicle.end() ? 0 : it->second;
        }
        void countDay(const std::string& what) { chronicle["day-" + std::to_string(day) + "-" + what]++; }
        void goalsTick()
        {
            if (partyDay == day || tick % 120)
            {
                return;
            }
            for (const Goal& g : todaysGoals())
            {
                if (progress(g) < g.need)
                {
                    return;
                }
            }
            partyDay = day;
            if (chronicle["party-" + std::to_string(day)]++)
            {
                return;
            }
            note("All three goals done! The whole park throws a party, and someone leaves a rare find: a Rare Candy.");
            treasures.push_back("a Rare Candy (party gift)");
            for (Resident& r : residents)
            {
                if (awake(r))
                {
                    say(r, Emote::Sparkle);
                    r.fun = std::min(100.0f, r.fun + 20);
                }
            }
            audio::sfx(audio::Sfx::Win);
        }

        void goalsMenu()
        {
            std::vector<screens::PickItem> items;
            int i = 0;
            for (const Goal& g : todaysGoals())
            {
                int p = std::min(progress(g), g.need);
                items.push_back({i++, std::string(p >= g.need ? "[Done]  " : "[ ]  ") + g.text + "  (" + std::to_string(p) + "/" +
                                          std::to_string(g.need) + ")", ""});
            }
            app::push(screens::picker("Today's goals", std::move(items), -1, [](int) {}));
        }

        void chronicleMenu()
        {
            static const std::pair<const char*, const char*> labels[] = {{"balls", "Balls thrown"}, {"berries", "Berries given"},
                {"pats", "Head pats"}, {"brushes", "Coats brushed"}, {"photos", "Photos taken"}, {"plays", "Games played"},
                {"races", "Races run"}, {"visitors", "Wild visitors"}, {"evolutions", "Evolutions seen"},
                {"evolutions-stopped", "Evolutions stopped"}, {"pranks", "Pranks played"}, {"ferries", "Lapras ferry rides"},
                {"snorlax", "Snorlax on the bridge"}, {"scenes", "Scenes acted out"}, {"treasures", "Treasures dug up"},
                {"spars-won", "Spars won"}, {"seeks-won", "Hide and seek won"}, {"wilds-returned", "Friends back from the Wilds"}};
            std::vector<screens::PickItem> items;
            int i = 0;
            for (auto& [k, label] : labels)
            {
                auto it = chronicle.find(k);
                items.push_back({i++, std::string(label) + ":  " + std::to_string(it == chronicle.end() ? 0 : it->second), ""});
            }
            app::push(screens::picker("Park chronicle", std::move(items), -1, [](int) {}));
        }

        void treasuresMenu()
        {
            if (treasures.empty())
            {
                app::message("Treasures", "Nothing found yet. Dig with a resident (More > Dig for treasure).");
                return;
            }
            std::vector<screens::PickItem> items;
            for (int i = int(treasures.size()) - 1; i >= 0; i--)
            {
                items.push_back({i, treasures[size_t(i)], ""});
            }
            app::push(screens::picker("Treasures", std::move(items), -1, [](int) {}));
        }

        // ---------------------------------------------------------------- the daycare (ParkLife.Daycare)
        void daycareToggle(Resident& r)
        {
            if (r.wild)
            {
                app::message("Daycare", "Wild visitors come and go as they please.");
                return;
            }
            std::string k = keyOf(r);
            auto it       = std::find(daycareGuests.begin(), daycareGuests.end(), k);
            if (it != daycareGuests.end())
            {
                daycareGuests.erase(it);
                r.guest = false;
                note("You took " + r.name + " back from Blissey's Daycare.");
                say(r, Emote::Heart);
            }
            else
            {
                daycareGuests.push_back(k);
                r.guest = true;
                if (pickTarget(r, 830, 740, 50))
                {
                    r.act = Act::Walk;
                }
                note("You left " + r.name + " with Blissey at the daycare. Audino will look after it.");
            }
        }

        void daycareTick(Resident& r)
        {
            if (!r.guest || tick % 600 != (r.phase * 6) % 600)
            {
                return;
            }
            // Guests stay round the house, are fed when hungry, and play together.
            if (std::hypot(r.x - 830, r.y - 740) > 90 && awake(r) && pickTarget(r, 830, 740, 50))
            {
                r.act = Act::Walk;
            }
            if (r.hunger < 40)
            {
                r.hunger = 90;
                note("Audino fed " + r.name + " at the daycare.");
            }
            r.fun = std::min(100.0f, r.fun + 5);
        }

        // ---------------------------------------------------------------- the album and the credits statue
        void album()
        {
            std::string folder = std::string(files::ROOT) + "/photos";
            std::vector<std::string> photos;
            files::list(folder, [&](const std::string& name, bool dir, u64)
                {
                    if (!dir && name.size() > 4 && name.compare(name.size() - 4, 4, ".bmp") == 0)
                    {
                        photos.push_back(folder + "/" + name);
                    }
                });
            std::sort(photos.begin(), photos.end());
            if (photos.empty())
            {
                app::message("Album", "No photos yet: press START in the park to take one.");
                return;
            }
            app::push(parkAlbum(std::move(photos)));
        }

        void credits() { app::push(parkCredits()); }
