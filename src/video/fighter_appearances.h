// Decode companion assets on demand and retain only the two displayed fighters.
#pragma once
#include "vf1_akira.h"
#include "../host/vf2_models.h"
#include "../host/fighter_roster.h"
#include <algorithm>
#include <cstdio>

namespace video {
class FighterAppearances {
public:
    explicit FighterAppearances(AkiraSource source):source(source){}
    bool enabled=false;
    bool ready() const {return !path.empty();}
    bool ready(unsigned fighter) const {
        return ready() && fighter_roster::available(fighter,source==AkiraSource::VF1?1:2);
    }
    bool load(const std::string &zip,std::string &error) {
        vf1_models::Assets assets;
        if(!(source==AkiraSource::VF1 ? vf1_models::load(zip,assets,error) : vf2_models::load(zip,assets,error)))return false;
        Vf1Akira model;
        if(!model.load_assets(assets,source,error))return false;
        model.enabled=true;
        entries={};entries[0]={0,++clock,std::move(model)};path=zip;failed={};return true;
    }
    size_t apply(Scene &scene,unsigned outfits,unsigned previews,unsigned fighter=0) {
        if(!enabled || !(outfits|previews))return 0;
        auto *model=get(fighter);
        return model ? model->apply(scene,outfits,previews) : 0;
    }
    const vf1_models::Portrait &portrait(unsigned fighter=0) const {
        for(const auto &entry:entries)if(entry.fighter==fighter)return entry.model.portrait();
        static const vf1_models::Portrait empty;
        return empty;
    }
private:
    struct Entry {unsigned fighter=255;uint64_t used=0;Vf1Akira model;};
    AkiraSource source;
    std::string path;
    std::array<Entry,2> entries{};
    std::array<bool,fighter_roster::count> failed{};
    uint64_t clock=0;
    Vf1Akira *get(unsigned fighter) {
        if(!ready(fighter) || failed[fighter])return nullptr;
        for(auto &entry:entries)if(entry.fighter==fighter){entry.used=++clock;return &entry.model;}
        std::string error;vf1_models::Assets assets;
        const bool decoded=source==AkiraSource::VF1 ? vf1_models::load(path,assets,error,fighter) :
            vf2_models::load(path,assets,error,fighter);
        Vf1Akira model;
        if(!decoded || !model.load_assets(assets,source,error,fighter)) {
            failed[fighter]=true;
            fprintf(stderr,"%s %s appearance: %s\n",source==AkiraSource::VF1?"VF1":"VF2",fighter_roster::names[fighter],error.c_str());
            return nullptr;
        }
        model.enabled=true;
        auto &entry=*std::min_element(entries.begin(),entries.end(),[](const auto &a,const auto &b){return a.used<b.used;});
        entry={fighter,++clock,std::move(model)};
        return &entry.model;
    }
};
}
