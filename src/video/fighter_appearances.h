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
        entries={};entries[0]={0,0,++clock,std::move(model)};path=zip;failed={};return true;
    }
    size_t apply(Scene &scene,unsigned outfits,unsigned previews,unsigned fighter=0,
                 const fighter_expression::Poses &poses={}) {
        if(!enabled || !(outfits|previews))return 0;
        size_t replaced=0;
        for(unsigned outfit=0;outfit<2;++outfit) {
            const unsigned bit=1u<<outfit;
            if(!((outfits|previews)&bit))continue;
            auto *model=get(fighter,outfit);
            if(!model)continue;
            std::array<uint16_t,2> frames{};
            for(const auto &pose:poses)if(pose.valid && fighter_roster::from_actor(pose.actor)==fighter &&
                                         unsigned(pose.actor>=13)==outfit)
                frames[outfit]=model->expression_frame(pose);
            replaced+=model->apply(scene,outfits&bit,previews&bit,frames);
        }
        return replaced;
    }

    const vf1_models::Portrait &portrait(unsigned fighter=0) const {
        for(const auto &entry:entries)if(entry.fighter==fighter)return entry.model.portrait();
        static const vf1_models::Portrait empty;
        return empty;
    }
private:
    struct Entry {unsigned fighter=255,outfit=0;uint64_t used=0;Vf1Akira model;};
    AkiraSource source;
    std::string path;
    std::array<Entry,2> entries{};
    std::array<std::array<bool,2>,fighter_roster::count> failed{};
    uint64_t clock=0;
    Vf1Akira *get(unsigned fighter,unsigned outfit) {
        if(!ready(fighter) || failed[fighter][outfit])return nullptr;
        for(auto &entry:entries)if(entry.fighter==fighter && entry.outfit==outfit){entry.used=++clock;return &entry.model;}
        std::string error;vf1_models::Assets assets;
        const bool decoded=source==AkiraSource::VF1 ? vf1_models::load(path,assets,error,fighter,outfit) :
            vf2_models::load(path,assets,error,fighter,true,outfit);
        Vf1Akira model;
        if(!decoded || !model.load_assets(assets,source,error,fighter)) {
            failed[fighter][outfit]=true;
            fprintf(stderr,"%s %s appearance: %s\n",source==AkiraSource::VF1?"VF1":"VF2",fighter_roster::names[fighter],error.c_str());
            return nullptr;
        }
        model.enabled=true;
        auto &entry=*std::min_element(entries.begin(),entries.end(),[](const auto &a,const auto &b){return a.used<b.used;});
        entry={fighter,outfit,++clock,std::move(model)};
        return &entry.model;
    }
};
}
