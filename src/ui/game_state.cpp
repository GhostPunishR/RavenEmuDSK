// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_state.hpp>
#include <ravenemudsk/achievements/client.hpp>
#include <ravenemu/bridge.h>
#include <ravenemu/sha256.hpp>
#include <algorithm>
#include <span>
#include <stdexcept>
#include <string_view>
namespace ravenemudsk_ui {
namespace {
constexpr std::string_view magic="RavenEmuState2\n";
constexpr std::size_t max_core=16u<<20,max_ra=2u<<20,header=magic.size()+8+32;
void length(std::vector<std::uint8_t>& out,std::size_t n){for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(n>>(i*8)));}
std::size_t length(const std::vector<std::uint8_t>& in,std::size_t at){
    std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(in[at+i])<<(i*8);return value;
}
}
std::vector<std::uint8_t> encode_game_state(const GameState& state){
    if(state.core.empty()||state.core.size()>max_core||state.achievements.size()>max_ra)
        throw std::runtime_error("Taille de l’état invalide");
    std::vector<std::uint8_t> out(magic.begin(),magic.end());length(out,state.core.size());length(out,state.achievements.size());
    std::vector<std::uint8_t> payload=state.core;payload.insert(payload.end(),state.achievements.begin(),state.achievements.end());
    const auto hash=ravenemu::detail::sha256(payload);out.insert(out.end(),hash.begin(),hash.end());
    out.insert(out.end(),payload.begin(),payload.end());return out;
}
GameState decode_game_state(const std::vector<std::uint8_t>& bytes){
    if(bytes.empty())throw std::runtime_error("État vide");
    if(bytes.size()<magic.size()||!std::equal(magic.begin(),magic.end(),bytes.begin())){
        if(bytes.size()>max_core)throw std::runtime_error("État trop volumineux");
        return {bytes,{}};
    }
    if(bytes.size()<header)throw std::runtime_error("État incomplet");
    const auto core=length(bytes,magic.size()),ra=length(bytes,magic.size()+4);
    if(!core||core>max_core||ra>max_ra||bytes.size()!=header+core+ra)throw std::runtime_error("État incomplet ou invalide");
    const auto hash=ravenemu::detail::sha256(std::span(bytes).subspan(header));
    if(!std::equal(hash.begin(),hash.end(),bytes.begin()+magic.size()+8))throw std::runtime_error("État endommagé");
    return {{bytes.begin()+header,bytes.begin()+header+core},{bytes.begin()+header+core,bytes.end()}};
}
GameState capture_game_state(ravenemu_bridge* core,ravenemudsk_ra::Client& client){
    const auto size=ravenemu_state_size(core);
    if(!size||size>max_core)throw std::runtime_error("État trop volumineux");
    GameState state;state.core.resize(size);
    if(!ravenemu_save_state(core,state.core.data(),state.core.size()))throw std::runtime_error("Sauvegarde d’état impossible");
    state.achievements=client.save_progress();return state;
}
bool restore_game_state(ravenemu_bridge* core,ravenemudsk_ra::Client& client,const std::vector<std::uint8_t>& bytes){
    if(!client.can_load_state())throw std::runtime_error("Chargement d’état interdit en Hardcore");
    const auto state=decode_game_state(bytes);
    if(!ravenemu_load_state(core,state.core.data(),state.core.size()))throw std::runtime_error("Chargement d’état impossible");
    return client.load_progress(state.achievements);
}
}
